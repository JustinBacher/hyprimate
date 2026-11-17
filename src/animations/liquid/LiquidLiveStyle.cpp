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
    const Vec2 BEAD = state.transition.beadCentre();
    return {.mesh = state.mesh,
        .transition = state.transition,
        .compensation = {.x = 1.0, .y = 1.0},
        .openOffset = {.x = -BEAD.x, .y = -BEAD.y},
        .displacementGain = state.displacementGain,
        .boxPose = true};
}

CBox liveExtents(const SLiquidStyle &state, const CBox &box) {
    const auto POSE = livePose(state);
    const auto LOGICAL = toBox(box);
    auto extents = hyprimate::liquidRenderExtents(POSE, LOGICAL, state.grid);
    hyprimate::expandBox(extents, 4.0);
    if (const auto VISIBLE = hyprimate::liquidVisibleBox(POSE, LOGICAL)) {
        extents = hyprimate::intersectBoxes(extents, *VISIBLE);
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
    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, g.canvas.size());
    if (!OUT) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }

    const auto POSE = livePose(state);
    const auto LOGICAL = toBox(context.currentBox);
    const auto VISIBLE = hyprimate::liquidVisibleBox(POSE, LOGICAL);
    const auto MENISCUS = hyprimate::liquidMeniscusExtents(POSE, LOGICAL);
    const SP<ITexture> SOLID = MENISCUS && solidFill ? solidFill->texture() : SP<ITexture>{};
    const CBox LIQUID_BOX = MENISCUS ? pixelBoxForLogical(toCBox(*MENISCUS), g.scale) : CBox{};
    hyprimate::fillLiquidRenderMesh(POSE,
        toBox(g.source),
        g.scale,
        toBox(g.output),
        Vec2{.x = in.box.x, .y = in.box.y},
        g.textureSize,
        state.grid,
        state.vertices);
    if (SOLID) {
        hyprimate::fillLiquidMeniscusMesh(
            POSE, toBox(g.source), g.scale, toBox(LIQUID_BOX), state.meniscusVertices);
    }

    const GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    auto &renderData = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    renderData.damage = CRegion{0.0, 0.0, g.canvas.w, g.canvas.h};
    renderData.fbSize = g.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (VISIBLE) {
        CBox clip = pixelBoxForLogical(toCBox(*VISIBLE), g.scale);
        clip.translate(-g.canvas.pos());
        renderData.damage.intersect(CRegion{clip});
    }
    GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &renderData.damage;
    textureData.a = 1.F;
    textureData.allowCustomUV = true;
    if (SOLID) {
        GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            SOLID,
            LIQUID_BOX.copy().translate(-g.canvas.pos()),
            state.meniscusVertices,
            textureData);
    }
    GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, in.framebuffer->getTexture(), g.localOutput, state.vertices, textureData);
    return {.framebuffer = OUT, .box = g.canvas};
}

bool SLiquidLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const auto FRAME = hyprimate::resolveStyleOpenEdge(
        runtime.edgeInputFor(window, true), runtime.m_config.open.liquid);
    if (!FRAME) {
        return false;
    }
    entry.openEdge = FRAME->edge;
    const auto &open = runtime.m_config.open.liquid;
    if (open.strength <= 0.0) {
        return false;
    }
    auto *transformer = runtime.acquireTransformer(window);
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    auto &state = std::get<SLiquidStyle>(transformer->m_style);
    state.transition.begin(hyprimate::Transition::Open,
        *FRAME,
        {.elasticity = open.elasticity, .momentum = open.momentum},
        open.strength);
    state.boxDriven = state.transition.active();
    state.warped = false;
    if (state.boxDriven) {
        state.goalBody = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    }
    refreshPose(state, {.x = CAPTURE.w, .y = CAPTURE.h});
    return true;
}

void SLiquidLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double,
    bool enabled,
    bool moving) {
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    const auto &current = decision.current_box;
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    if (moving) {
        state.mesh.recordTranslation(
            {.x = current.x - decision.previous_box.x, .y = current.y - decision.previous_box.y},
            {.x = CAPTURE.w, .y = CAPTURE.h},
            runtime.movementStrength());
    } else if (enabled) {
        state.mesh.advance(0.0, {.x = CAPTURE.w, .y = CAPTURE.h});
    } else {
        state.mesh.reset();
    }
    refreshPose(state, {.x = CAPTURE.w, .y = CAPTURE.h});
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
        const auto WINDOW = windowRef.lock();
        if (!WINDOW || !(state.goalBody.w > 0.0 && state.goalBody.h > 0.0)) {
            return;
        }
        WINDOW->positionAnimation()->setValueAndWarp(state.goalBody.pos());
        WINDOW->sizeAnimation()->setValueAndWarp(state.goalBody.size());
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
        const auto WINDOW = windowRef.lock();
        if (!WINDOW) {
            return;
        }

        auto &positionVar = WINDOW->positionAnimation();
        auto &sizeVar = WINDOW->sizeAnimation();
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
        const CBox CURRENT = WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox CAPTURE = WINDOW->getFullWindowBoundingBox();
        const double LEFT = CURRENT.x - CAPTURE.x;
        const double TOP = CURRENT.y - CAPTURE.y;
        const double RIGHT = (CAPTURE.x + CAPTURE.w) - (CURRENT.x + CURRENT.w);
        const double BOTTOM = (CAPTURE.y + CAPTURE.h) - (CURRENT.y + CURRENT.h);
        if (!(std::isfinite(LEFT) && std::isfinite(TOP) && std::isfinite(RIGHT) &&
                std::isfinite(BOTTOM))) {
            return;
        }

        const CBox REST = {state.goalBody.x - LEFT,
            state.goalBody.y - TOP,
            state.goalBody.w + LEFT + RIGHT,
            state.goalBody.h + TOP + BOTTOM};
        const Vec2 HALF = state.transition.beadHalf({.x = REST.w, .y = REST.h});
        const Vec2 CENTRE = state.transition.beadCentre();
        const double CENTRE_X = REST.x + (0.5 * REST.w) + CENTRE.x;
        const double CENTRE_Y = REST.y + (0.5 * REST.h) + CENTRE.y;
        constexpr double MIN_SIDE = 2.0;
        const double WIDTH = std::max(MIN_SIDE, (2.0 * HALF.x) - LEFT - RIGHT);
        const double HEIGHT = std::max(MIN_SIDE, (2.0 * HALF.y) - TOP - BOTTOM);
        const Vector2D POSITION{CENTRE_X - HALF.x + LEFT, CENTRE_Y - HALF.y + TOP};
        const Vector2D SIZE{WIDTH, HEIGHT};

        positionVar->setValueAndWarp(POSITION);
        sizeVar->setValueAndWarp(SIZE);
        state.warpPosition = POSITION;
        state.warpSize = SIZE;
        state.warped = true;
    }
    void refreshPose(SLiquidStyle &state, Vec2 captureSize) {
        // Gain only: the shared grid is built by the pose that draws it, so nothing is pre-filled
        // here.
        const hyprimate::LiquidPose FIT{.mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .openOffset = {.x = 0.0, .y = 0.0},
            .displacementGain = 1.0,
            .boxPose = state.boxDriven};
        state.displacementGain = hyprimate::fitLiquidPoseGain(FIT, captureSize, nullptr);
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
    const auto PARAMS = runtime.effectiveMaterialParams();
    return std::isfinite(PARAMS.viscosity) && std::isfinite(PARAMS.elasticity) &&
           std::isfinite(PARAMS.momentum) && std::isfinite(PARAMS.variation);
}
