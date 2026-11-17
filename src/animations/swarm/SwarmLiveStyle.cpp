#include <cstdint>
#include <optional>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
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
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmConfig.hpp"
#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

using hyprimate::Transition;

namespace {

    // Where the window is (or, opening, where it will rest) on its monitor; nullopt without a
    // monitor.
    std::optional<hyprimate::SwarmPlacement> placementOf(const PHLWINDOW &window, bool atGoal) {
        const auto MONITOR = window->m_monitor.lock();
        if (!MONITOR) {
            return std::nullopt;
        }
        const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox CAPTURE = window->getFullWindowBoundingBox();
        if (!atGoal) {
            return hyprimate::SwarmPlacement{.capture = toBox(CAPTURE),
                .body = toBox(CURRENT),
                .screen = toBox(MONITOR->logicalBox())};
        }
        // Decoration margins (border, shadow) do not scale with native popin, so they carry over to
        // the rest box.
        const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const CBox REST{GOAL.x + (CAPTURE.x - CURRENT.x),
            GOAL.y + (CAPTURE.y - CURRENT.y),
            GOAL.w + (CAPTURE.w - CURRENT.w),
            GOAL.h + (CAPTURE.h - CURRENT.h)};
        return hyprimate::SwarmPlacement{
            .capture = toBox(REST), .body = toBox(GOAL), .screen = toBox(MONITOR->logicalBox())};
    }

}  // namespace

CBox liveExtents(const SSwarmStyle &state, const CBox &box) {
    const hyprimate::Box BOUNDS = state.swarm.bounds();
    // Nothing on screen yet (or any more): a token box keeps the window itself from being drawn in
    // its place.
    if (!(BOUNDS.w > 0.0 && BOUNDS.h > 0.0)) {
        return CBox{box.x, box.y, 1.0, 1.0};
    }
    return CBox{box.x + BOUNDS.x, box.y + BOUNDS.y, BOUNDS.w, BOUNDS.h}.expand(2.0);
}

Render::SWindowTransformBuffer renderLive(SSwarmStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const Render::SWindowTransformContext &,
    const SLiveRenderGeometry &g,
    const SP<CSolidFill> &,
    const SP<CPaperShade> &,
    const SP<CSwarmSprites> &sprites) {
    const auto *sheet = sprites ? sprites->sheet(state.creature) : nullptr;
    const auto ATLAS =
        (sheet != nullptr) ? sprites->texture(state.creature) : SP<Render::ITexture>{};
    if ((sheet == nullptr) || !ATLAS) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }
    if (state.captureWorkspaceFrame) {
        state.workspaceSnapshot = in.framebuffer;
        state.snapshotOffset = {.x = in.box.x - g.source.x, .y = in.box.y - g.source.y};
        state.captureWorkspaceFrame = false;
    }

    fillSwarmMeshes(state.swarm,
        *sheet,
        g.source,
        g.output,
        {.x = in.box.x, .y = in.box.y},
        g.textureSize,
        state.meshes);
    const auto LAYER = renderSwarmLayer(ctx,
        in.framebuffer->getTexture(),
        ATLAS,
        swarmLayerSize(g.source),
        state.meshes,
        sprites->surface());
    if (!LAYER) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }
    // Requested while the layer is held, so the pool cannot return the same buffer twice.
    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, g.canvas.size());
    if (!OUT) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }

    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    auto &renderData = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    renderData.damage = CRegion{0.0, 0.0, g.canvas.w, g.canvas.h};
    renderData.fbSize = g.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (!state.meshes.pieces.empty()) {
        Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
        textureData.damage = &renderData.damage;
        textureData.allowCustomUV = true;
        textureData.allowDim = false;  // the layer is already dimmed
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, LAYER->getTexture(), g.localOutput, state.meshes.pieces, textureData);
    }
    return {.framebuffer = OUT, .box = g.canvas};
}

bool SSwarmLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const auto SIDES = runtime.swarmSides(window, true);
    const auto PLACEMENT = placementOf(window, true);
    const auto CREATURE = swarmCreature(runtime.m_config.open.swarm);
    if (!PLACEMENT || SIDES.empty() || (runtime.m_sprites->sheet(CREATURE) == nullptr)) {
        return false;
    }

    auto *transformer = runtime.acquireTransformer(window);
    auto &state = std::get<SSwarmStyle>(transformer->m_style);
    state.creature = CREATURE;
    state.swarm.reset();
    state.swarm.begin(Transition::Open,
        swarmParams(runtime.m_config.open.swarm),
        runtime.m_nextSeed++,
        *PLACEMENT,
        SIDES);
    if (state.swarm.active()) {
        // The swarm assembles the window where it will rest, so the native popin is taken straight
        // to its goal.
        const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        g_pHyprRenderer->damageBox(window->getFullWindowBoundingBox());
        window->positionAnimation()->setValueAndWarp(GOAL.pos());
        window->sizeAnimation()->setValueAndWarp(GOAL.size());
        runtime.rebaseline(entry, window);
    }
    return true;
}

void SSwarmLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double,
    bool enabled,
    bool moving) {
    auto &state = std::get<SSwarmStyle>(transformer.m_style);
    auto placement = placementOf(window, false);
    if (!placement) {
        return;
    }
    placement->capture = decision.current_capture;
    placement->body = decision.current_box;
    state.workspaceCapture = decision.current_capture;
    state.captureWorkspaceFrame = decision.workspace_moving;
    if (moving && !state.swarm.active()) {
        // Formed where the window was, so the first update carries this frame's movement.
        state.creature = swarmCreature(runtime.m_config.movement.swarm);
        state.swarm.reset();
        state.swarm.beginMovement(swarmParams(runtime.m_config.movement.swarm),
            runtime.m_nextSeed++,
            {.capture = decision.previous_capture,
                .body = decision.previous_box,
                .screen = placement->screen});
    }
    // An opening follows its window even with movement off; its agents are in flight either way.
    if (state.swarm.opening() || (enabled && state.swarm.moving())) {
        state.swarm.updateMovement(*placement);
    }
}

void SSwarmLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SSwarmStyle>(&transformer->m_style) : nullptr;
    s.handoff = (state != nullptr) && state->swarm.active();
    s.transition_busy = false;
    s.handoff_moving = (state != nullptr) && state->swarm.moving();
}

void SSwarmLiveStyle::step(CPluginRuntime &,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2,
    bool enabled) {
    auto &state = std::get<SSwarmStyle>(transformer.m_style);
    if (!enabled && state.swarm.moving()) {
        cancelMovement(transformer);
    }
    const auto TARGET = g_layoutManager->dragController()->target();
    const bool HELD = enabled && TARGET && TARGET->window() == transformer.m_window.lock() &&
                      g_layoutManager->dragController()->mode() == MBIND_MOVE;
    state.swarm.advance(elapsed, HELD);
}

void SSwarmLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &swarm = std::get<SSwarmStyle>(transformer.m_style).swarm;
    if (swarm.opening()) {
        swarm.reset();
    }
}

void SSwarmLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &swarm = std::get<SSwarmStyle>(transformer.m_style).swarm;
    if (swarm.moving()) {
        swarm.reset();
    }
}

bool SSwarmLiveStyle::deforming(const SSwarmStyle &state) noexcept {
    return state.swarm.active();
}

// Swarm settings are read when an animation begins, so a state is never stale for them.
bool SSwarmLiveStyle::builtFor(const SSwarmStyle &,
    hyprimate::config::Style style,
    hyprimate::MaterialParams,
    hyprimate::ClothMovementParams) noexcept {
    return style == hyprimate::config::Style::Swarm;
}

bool SSwarmLiveStyle::available(const SSwarmStyle &) noexcept {
    return true;
}

SStyleState SSwarmLiveStyle::initial(
    hyprimate::MaterialParams, hyprimate::ClothMovementParams, std::uint64_t) {
    return SSwarmStyle{};
}

// Movement has no strength setting: the window either holds together or is carried by its swarm.
double SSwarmLiveStyle::strength(const CPluginRuntime &) {
    return 1.0;
}
bool SSwarmLiveStyle::usable(const CPluginRuntime &runtime) {
    return runtime.m_sprites->sheet(swarmCreature(runtime.m_config.movement.swarm)) != nullptr;
}
