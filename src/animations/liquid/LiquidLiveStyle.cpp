#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

using namespace Render;
using hyprimate::Vec2;
namespace {
    void refreshPose(SLiquidStyle &state, Vec2 captureSize);
    void restoreBox(SLiquidStyle &state, const PHLWINDOWREF &window);
    void driveBox(SLiquidStyle &state, const PHLWINDOWREF &window);
}  // namespace

hyprimate::LiquidPose livePose(const SLiquidStyle &state) {
    if (!state.boxDriven) {
        return {.mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .openOffset = {.x = 0.0, .y = 0.0},
            .displacementGain = state.displacementGain,
            .boxPose = false};
    }
    const Vec2 bead = state.transition.beadCentre();
    return {.mesh = state.mesh,
        .transition = state.transition,
        .compensation = {.x = 1.0, .y = 1.0},
        .openOffset = {.x = -bead.x, .y = -bead.y},
        .displacementGain = state.displacementGain,
        .boxPose = true};
}

CBox liveExtents(const SLiquidStyle &state, const CBox &box) {
    const auto pose = livePose(state);
    const auto logical = toBox(box);
    auto extents = hyprimate::liquidRenderExtents(pose, logical, state.grid);
    hyprimate::expandBox(extents, 4.0);
    if (const auto visible = hyprimate::liquidVisibleBox(pose, logical)) {
        extents = hyprimate::intersectBoxes(extents, *visible);
    }
    return toCBox(extents);
}

SWindowTransformBuffer renderLive(SLiquidStyle &state,
    CRenderContext &ctx,
    const SWindowTransformBuffer &in,
    const SWindowTransformContext &context,
    const SLiveRenderGeometry &g,
    const SP<CSolidFill> &solidFill,
    const SP<CPaperShade> &,
    const SP<CSwarmSprites> &) {
    const auto out = g_pHyprRenderer->getWorkBuffer(ctx, g.canvas.size());
    if (!out) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }

    const auto pose = livePose(state);
    const auto logical = toBox(context.currentBox);
    const auto visible = hyprimate::liquidVisibleBox(pose, logical);
    const auto meniscus = hyprimate::liquidMeniscusExtents(pose, logical);
    const SP<ITexture> solid = meniscus && solidFill ? solidFill->texture() : SP<ITexture>{};
    const CBox liquidBox = meniscus ? pixelBoxForLogical(toCBox(*meniscus), g.scale) : CBox{};
    hyprimate::fillLiquidRenderMesh(pose,
        toBox(g.source),
        g.scale,
        toBox(g.output),
        Vec2{.x = in.box.x, .y = in.box.y},
        g.textureSize,
        state.grid,
        state.vertices);
    if (solid) {
        hyprimate::fillLiquidMeniscusMesh(
            pose, toBox(g.source), g.scale, toBox(liquidBox), state.meniscusVertices);
    }

    const GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    const auto drawState = ctx.saveDrawState();
    auto &renderData = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, out);
    renderData.damage = CRegion{0.0, 0.0, g.canvas.w, g.canvas.h};
    renderData.fbSize = g.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (visible) {
        CBox clip = pixelBoxForLogical(toCBox(*visible), g.scale);
        clip.translate(-g.canvas.pos());
        renderData.damage.intersect(CRegion{clip});
    }
    GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &renderData.damage;
    textureData.a = 1.F;
    textureData.allowCustomUV = true;
    if (solid) {
        GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            solid,
            liquidBox.copy().translate(-g.canvas.pos()),
            state.meniscusVertices,
            textureData);
    }
    GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, in.framebuffer->getTexture(), g.localOutput, state.vertices, textureData);
    return {.framebuffer = out, .box = g.canvas};
}

bool SLiquidLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const auto frame = hyprimate::resolveStyleOpenEdge(
        runtime.edgeInputFor(window, true), runtime.m_config.open.liquid);
    if (!frame) {
        return false;
    }
    entry.openEdge = frame->edge;
    const auto &open = runtime.m_config.open.liquid;
    if (open.strength <= 0.0) {
        return false;
    }
    auto *transformer = runtime.acquireTransformer(window);
    const CBox capture = window->getFullWindowBoundingBox();
    auto &state = std::get<SLiquidStyle>(transformer->m_style);
    state.transition.begin(hyprimate::Transition::Open,
        *frame,
        {.elasticity = open.elasticity, .momentum = open.momentum},
        open.strength);
    state.boxDriven = state.transition.active();
    state.warped = false;
    if (state.boxDriven) {
        state.goalBody = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    }
    refreshPose(state, {.x = capture.w, .y = capture.h});
    return true;
}

void SLiquidLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double,
    bool enabled,
    bool moving) {
    const CBox capture = window->getFullWindowBoundingBox();
    const auto &current = decision.current_box;
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    if (moving) {
        state.mesh.recordTranslation(
            {.x = current.x - decision.previous_box.x, .y = current.y - decision.previous_box.y},
            {.x = capture.w, .y = capture.h},
            runtime.movementStrength());
    } else if (enabled) {
        state.mesh.advance(0.0, {.x = capture.w, .y = capture.h});
    } else {
        state.mesh.reset();
    }
    refreshPose(state, {.x = capture.w, .y = capture.h});
}

void SLiquidLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SLiquidStyle>(&transformer->m_style) : nullptr;
    s.transition_busy = (state != nullptr) && state->transition.active();
}

void SLiquidLiveStyle::step(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    if (!enabled) {
        state.mesh.reset();
    }
    hyprimate::advanceLiquidPose(state.mesh,
        state.transition,
        state.grid,
        elapsed,
        size,
        runtime.movementStrength(),
        enabled ? runtime.m_config.open.liquid.emergeDrag : 0.0);
    refreshPose(state, size);
    driveBox(state, transformer.m_window);
}
namespace {
    void restoreBox(SLiquidStyle &state, const PHLWINDOWREF &windowRef) {
        if (!state.boxDriven || !state.warped) {
            return;
        }
        state.warped = false;
        const auto window = windowRef.lock();
        if (!window || !(state.goalBody.w > 0.0 && state.goalBody.h > 0.0)) {
            return;
        }
        window->positionAnimation()->setValueAndWarp(state.goalBody.pos());
        window->sizeAnimation()->setValueAndWarp(state.goalBody.size());
    }

    void driveBox(SLiquidStyle &state, const PHLWINDOWREF &windowRef) {
        if (!state.boxDriven) {
            return;
        }
        if (!state.transition.active()) {
            restoreBox(state, windowRef);
            state.boxDriven = false;
            return;
        }
        const auto window = windowRef.lock();
        if (!window) {
            return;
        }

        auto &positionVar = window->positionAnimation();
        auto &sizeVar = window->sizeAnimation();
        // The layout retargets the real box through the same variables. A goal other than the one
        // this wrote is the layout's.
        if (state.warped &&
            (positionVar->goal() != state.warpPosition || sizeVar->goal() != state.warpSize)) {
            state.goalBody = CBox{positionVar->goal(), sizeVar->goal()};
        }
        if (!(state.goalBody.w > 0.0 && state.goalBody.h > 0.0)) {
            return;
        }

        // Decorations (border, shadow) do not scale with the box, so the live margins hold at any
        // size.
        const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox capture = window->getFullWindowBoundingBox();
        const double left = current.x - capture.x;
        const double top = current.y - capture.y;
        const double right = (capture.x + capture.w) - (current.x + current.w);
        const double bottom = (capture.y + capture.h) - (current.y + current.h);
        if (!(std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
                std::isfinite(bottom))) {
            return;
        }

        const CBox rest = {state.goalBody.x - left,
            state.goalBody.y - top,
            state.goalBody.w + left + right,
            state.goalBody.h + top + bottom};
        const Vec2 half = state.transition.beadHalf({.x = rest.w, .y = rest.h});
        const Vec2 centre = state.transition.beadCentre();
        const double cx = rest.x + (0.5 * rest.w) + centre.x;
        const double cy = rest.y + (0.5 * rest.h) + centre.y;
        constexpr double MIN_SIDE = 2.0;
        const double w = std::max(MIN_SIDE, (2.0 * half.x) - left - right);
        const double h = std::max(MIN_SIDE, (2.0 * half.y) - top - bottom);
        const Vector2D position{cx - half.x + left, cy - half.y + top};
        const Vector2D size{w, h};

        positionVar->setValueAndWarp(position);
        sizeVar->setValueAndWarp(size);
        state.warpPosition = position;
        state.warpSize = size;
        state.warped = true;
    }
    void refreshPose(SLiquidStyle &state, Vec2 captureSize) {
        // Gain only: the shared grid is built by the pose that draws it, so nothing is pre-filled
        // here.
        const hyprimate::LiquidPose fit{.mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .openOffset = {.x = 0.0, .y = 0.0},
            .displacementGain = 1.0,
            .boxPose = state.boxDriven};
        state.displacementGain = hyprimate::fitLiquidPoseGain(fit, captureSize, nullptr);
        state.grid.invalidate();
    }
}  // namespace

void SLiquidLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    restoreBox(state, transformer.m_window);
    state.transition.reset();
    state.boxDriven = false;
    state.displacementGain = 1.0;
    state.grid.invalidate();
}

void SLiquidLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    state.mesh.reset();
    state.grid.invalidate();
}

bool SLiquidLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    const auto &state = std::get<SLiquidStyle>(transformer.m_style);
    return !(state.mesh.settled() && !state.transition.active());
}

bool SLiquidLiveStyle::builtFor(const CAnimationTransformer &transformer,
    hyprimate::config::Style style,
    hyprimate::MaterialParams liquid,
    hyprimate::ClothMovementParams) noexcept {
    return style == hyprimate::config::Style::Liquid &&
           std::get<SLiquidStyle>(transformer.m_style).mesh.params() == liquid;
}

bool SLiquidLiveStyle::available(const CAnimationTransformer &) noexcept {
    return true;
}

SStyleState SLiquidLiveStyle::initial(
    hyprimate::MaterialParams liquid, hyprimate::ClothMovementParams, std::uint64_t seed) {
    return SLiquidStyle{.mesh = hyprimate::LiquidMesh{liquid, seed}};
}

double SLiquidLiveStyle::strength(const CPluginRuntime &runtime) {
    return runtime.m_config.movement.liquid.strength;
}
bool SLiquidLiveStyle::usable(const CPluginRuntime &runtime) {
    const auto params = runtime.effectiveMaterialParams();
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) &&
           std::isfinite(params.momentum) && std::isfinite(params.variation);
}
