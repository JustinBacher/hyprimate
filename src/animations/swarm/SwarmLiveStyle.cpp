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
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmConfig.hpp"
#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

using hyprimate::Transition;

namespace {

    // Where the window is (or, opening, where it will rest) on its monitor; nullopt without a
    // monitor.
    std::optional<hyprimate::SwarmPlacement> placement_of(const PHLWINDOW &window, bool at_goal) {
        const auto MONITOR = window->m_monitor.lock();
        if (!MONITOR) {
            return std::nullopt;
        }
        const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox CAPTURE = window->getFullWindowBoundingBox();
        if (!at_goal) {
            return hyprimate::SwarmPlacement{
                .capture = to_box(CAPTURE),
                .body = to_box(CURRENT),
                .screen = to_box(MONITOR->logicalBox()),
            };
        }
        // Decoration margins (border, shadow) do not scale with native popin, so they carry over to
        // the rest box.
        const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const CBox REST{GOAL.x + (CAPTURE.x - CURRENT.x),
            GOAL.y + (CAPTURE.y - CURRENT.y),
            GOAL.w + (CAPTURE.w - CURRENT.w),
            GOAL.h + (CAPTURE.h - CURRENT.h)};
        return hyprimate::SwarmPlacement{
            .capture = to_box(REST),
            .body = to_box(GOAL),
            .screen = to_box(MONITOR->logicalBox()),
        };
    }

}  // namespace

CBox live_extents(const SSwarmStyle &state, const CBox &box) {
    const hyprimate::Box BOUNDS = state.swarm.bounds();
    // Nothing on screen yet (or any more): a token box keeps the window itself from being drawn in
    // its place.
    if (!(BOUNDS.w > 0.0) || !(BOUNDS.h > 0.0)) {
        return CBox{box.x, box.y, 1.0, 1.0};
    }
    return CBox{box.x + BOUNDS.x, box.y + BOUNDS.y, BOUNDS.w, BOUNDS.h}.expand(2.0);
}

Render::SWindowTransformBuffer render_live(SSwarmStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    [[maybe_unused]] const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SSwarmResources &resources) {
    const auto &sprites = resources.sprites;
    const auto *sheet = sprites ? sprites->sheet(state.creature) : nullptr;
    const auto ATLAS =
        (sheet != nullptr) ? sprites->texture(state.creature) : SP<Render::ITexture>{};
    if ((sheet == nullptr) || !ATLAS) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    if (state.capture_workspace_frame) {
        state.workspace_snapshot = input.framebuffer;
        state.snapshot_offset = {
            .x = input.box.x - geometry.source.x,
            .y = input.box.y - geometry.source.y,
        };
        state.capture_workspace_frame = false;
    }

    fill_swarm_meshes(state.swarm,
        *sheet,
        geometry.source,
        geometry.output,
        {.x = input.box.x, .y = input.box.y},
        geometry.texture_size,
        state.meshes);
    const auto LAYER = render_swarm_layer(ctx,
        input.framebuffer->getTexture(),
        ATLAS,
        swarm_layer_size(geometry.source),
        state.meshes,
        sprites->surface());
    if (!LAYER) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    // Requested while the layer is held, so the pool cannot return the same buffer twice.
    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, geometry.canvas.size());
    if (!OUT) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }

    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    auto &render_data = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    render_data.damage = CRegion{0.0, 0.0, geometry.canvas.w, geometry.canvas.h};
    render_data.fbSize = geometry.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (!state.meshes.pieces.empty()) {
        Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
        texture_data.damage = &render_data.damage;
        texture_data.allowCustomUV = true;
        texture_data.allowDim = false;  // the layer is already dimmed
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, LAYER->getTexture(), geometry.local_output, state.meshes.pieces, texture_data);
    }
    return {.framebuffer = OUT, .box = geometry.canvas};
}

bool SSwarmLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window, SSwarmResources &resources) {
    const auto SIDES = swarm_sides(runtime.edge_input_for(window, true),
        runtime.m_config.swarm.open.loneWindowSides);
    const auto PLACEMENT = placement_of(window, true);
    const auto CREATURE = swarm_creature(runtime.m_config.swarm.open);
    if (!PLACEMENT || SIDES.empty() || (resources.sprites->sheet(CREATURE) == nullptr)) {
        return false;
    }

    auto *transformer = runtime.acquire_transformer(window);
    auto &state = std::get<SSwarmStyle>(transformer->m_style);
    state.creature = CREATURE;
    state.swarm.reset();
    state.swarm.begin(Transition::Open,
        swarm_params(runtime.m_config.swarm.open),
        runtime.m_next_seed++,
        *PLACEMENT,
        SIDES);
    if (state.swarm.active()) {
        // The swarm assembles the window where it will rest, so the native popin is taken straight
        // to its goal.
        const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        g_pHyprRenderer->damageBox(window->getFullWindowBoundingBox());
        window->positionAnimation()->setValueAndWarp(GOAL.pos());
        window->sizeAnimation()->setValueAndWarp(GOAL.size());
        CPluginRuntime::rebaseline(entry, window);
    }
    return true;
}

void SSwarmLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    [[maybe_unused]] double elapsed,
    bool enabled,
    bool moving) {
    auto &state = std::get<SSwarmStyle>(transformer.m_style);
    auto placement = placement_of(window, false);
    if (!placement) {
        return;
    }
    placement->capture = decision.current_capture;
    placement->body = decision.current_box;
    state.workspace_capture = decision.current_capture;
    state.capture_workspace_frame = decision.workspace_moving;
    if (moving && !state.swarm.active()) {
        // Formed where the window was, so the first update carries this frame's movement.
        state.creature = swarm_creature(runtime.m_config.swarm.movement);
        state.swarm.reset();
        state.swarm.begin_movement(swarm_params(runtime.m_config.swarm.movement),
            runtime.m_next_seed++,
            {
                .capture = decision.previous_capture,
                .body = decision.previous_box,
                .screen = placement->screen,
            });
    }
    // An opening follows its window even with movement off; its agents are in flight either way.
    if (state.swarm.opening() || (enabled && state.swarm.moving())) {
        state.swarm.update_movement(*placement);
    }
}

void SSwarmLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &snapshot) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SSwarmStyle>(&transformer->m_style) : nullptr;
    snapshot.handoff = (state != nullptr) && state->swarm.active();
    snapshot.transition_busy = false;
    snapshot.handoff_moving = (state != nullptr) && state->swarm.moving();
}

void SSwarmLiveStyle::step([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    [[maybe_unused]] hyprimate::Vec2 size,
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
bool SSwarmLiveStyle::builtFor([[maybe_unused]] const SSwarmStyle &state,
    [[maybe_unused]] const Inputs &inputs) noexcept {
    return true;
}

bool SSwarmLiveStyle::available([[maybe_unused]] const SSwarmStyle &state) noexcept {
    return true;
}

SSwarmStyle SSwarmLiveStyle::initial([[maybe_unused]] const Inputs &inputs,
    [[maybe_unused]] std::uint64_t seed) {
    return SSwarmStyle{};
}

// Movement has no strength setting: the window either holds together or is carried by its swarm.
double SSwarmLiveStyle::strength([[maybe_unused]] const CPluginRuntime &runtime) {
    return 1.0;
}
bool SSwarmLiveStyle::usable(const CPluginRuntime &runtime, const Resources &resources) {
    return resources.sprites->sheet(swarm_creature(runtime.m_config.swarm.movement)) != nullptr;
}

SSwarmLiveStyle::Inputs SSwarmLiveStyle::inputs([[maybe_unused]] const CPluginRuntime &runtime) { return {}; }
void SSwarmLiveStyle::prepare([[maybe_unused]] CPluginRuntime &runtime, [[maybe_unused]] Resources &resources, [[maybe_unused]] bool selected) {}
bool SSwarmLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated;
}
