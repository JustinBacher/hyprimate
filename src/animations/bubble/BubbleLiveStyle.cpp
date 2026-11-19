#include <cstdint>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "config_generated.hpp"

namespace {
    hyprimate::BubbleShape bubble_shape(hyprimate::config::Shape shape) {
        switch (shape) {
            case hyprimate::config::Shape::Ellipse:
                return hyprimate::BubbleShape::Ellipse;
            case hyprimate::config::Shape::Circle:
                return hyprimate::BubbleShape::Circle;
            case hyprimate::config::Shape::Pillow:
                break;
        }
        return hyprimate::BubbleShape::Pillow;
    }
}  // namespace

CBox live_extents(const SBubbleStyle &state, const CBox &box) {
    const auto BOUNDS = state.bubble.bounds({.x = box.w, .y = box.h});
    return CBox{box.x + BOUNDS.x, box.y + BOUNDS.y, BOUNDS.w, BOUNDS.h}.expand(2.0);
}

Render::SWindowTransformBuffer render_live(SBubbleStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SBubbleResources &resources) {
    const hyprimate::Vec2 SIZE{
        .x = geometry.source.w / geometry.scale,
        .y = geometry.source.h / geometry.scale,
    };
    const auto FRAME = state.bubble.frame(SIZE);
    const auto LAYER = resources.renderer->render(ctx,
        SBubbleDraw{
            .window = input.framebuffer->getTexture(),
            .wallpaper = state.wallpaper,
            .frame = FRAME,
            .canvas = geometry.canvas.size(),
            .source = geometry.source,
            .texture_origin = {.x = input.box.x, .y = input.box.y},
            .texture_size = geometry.texture_size,
            .output_origin = {.x = geometry.canvas.x, .y = geometry.canvas.y},
            .wake = {},
        });
    if (!LAYER) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot) {
        const auto MONITOR = context.monitor.lock();
        const auto CENTRE = context.currentBox.middle();
        deposit_bubble_impacts(state, resources.vestige, FRAME, MONITOR, CENTRE);
        // Film and microbubbles the movement shed since the last frame stay where they were laid,
        // relative to the window drawn now.
        const auto &motion = state.bubble.motion();
        if (!motion.trail().empty() || !motion.releases().empty()) {
            if (MONITOR) {
                state.wake = resources.vestige.trail(MONITOR,
                    state.window.lock(),
                    state.wake,
                    CENTRE,
                    motion.trail(),
                    motion.releases());
            }
            state.bubble.clear_emissions();
        }
    }
    return {.framebuffer = LAYER, .box = geometry.canvas};
}

bool SBubbleLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window,
    [[maybe_unused]] SBubbleResources &resources) {
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    const hyprimate::Box REST{
        .x = GOAL.x + CAPTURE.x - CURRENT.x,
        .y = GOAL.y + CAPTURE.y - CURRENT.y,
        .w = GOAL.w + CAPTURE.w - CURRENT.w,
        .h = GOAL.h + CAPTURE.h - CURRENT.h,
    };
    if (!hyprimate::is_usable(to_box(GOAL)) || !hyprimate::is_usable(REST)) {
        return false;
    }
    auto *transformer = runtime.acquire_transformer(window);
    auto &state = std::get<SBubbleStyle>(transformer->m_style);
    const auto &options = runtime.m_config.bubble.open;
    state.bubble.begin(hyprimate::Transition::Open,
        hyprimate::BubbleDuration{options.duration},
        runtime.m_next_seed++,
        {.x = REST.w, .y = REST.h},
        {.count = options.count, .birthRate = options.birthRate,
            .sizeVariation = options.sizeVariation, .growthVariation = options.growthVariation,
            .interaction = options.interaction, .damping = options.damping,
            .buoyancy = options.buoyancy, .linger = options.linger});
    if (!state.bubble.active()) {
        return false;
    }
    state.wallpaper = makeShared<SBubbleWallpaper>();
    state.shell_impacts_deposited = state.window_impacts_deposited = false;
    state.impacts.start();
    // Bubble owns formation, placement and opacity. Keep focus/workspace/user alpha factors intact.
    g_pHyprRenderer->damageBox(CAPTURE);
    window->positionAnimation()->setValueAndWarp(GOAL.pos());
    window->sizeAnimation()->setValueAndWarp(GOAL.size());
    auto &fade = window->presentation().alpha(Desktop::View::WINDOW_ALPHA_FADE);
    fade->setValueAndWarp(fade->goal());
    CPluginRuntime::rebaseline(entry, window);
    return true;
}

void SBubbleLiveStyle::movement([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    [[maybe_unused]] double elapsed,
    [[maybe_unused]] bool enabled,
    bool moving) {
    if (!moving) {
        return;
    }
    // The fluid follows the body's centre: an edge dragged by a resize carries it by half as much.
    // A layout animating the window moves it faster than a bubble travels; the film follows.
    const auto &current = decision.current_box;
    const auto &was = decision.previous_box;
    std::get<SBubbleStyle>(transformer.m_style)
        .bubble.drive(
            {
                .x = current.x + (current.w * .5) - (was.x + (was.w * .5)),
                .y = current.y + (current.h * .5) - (was.y + (was.h * .5)),
            },
            !decision.workspace_moving && window->positionAnimation()->isBeingAnimated());
}

void SBubbleLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &snapshot) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SBubbleStyle>(&transformer->m_style) : nullptr;
    // A landed opening is already the window's film: movement acts on it while it settles.
    snapshot.transition_busy =
        (state != nullptr) && state->bubble.active() && !state->bubble.landed();
}

void SBubbleLiveStyle::step(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled) {
    auto &state = std::get<SBubbleStyle>(transformer.m_style);
    if (!enabled && state.bubble.stirred()) {
        cancelMovement(transformer);
    }
    hyprimate::Box body{.x = -size.x * .5, .y = -size.y * .5, .w = size.x, .h = size.y};
    state.window = transformer.m_window;
    if (const auto WINDOW = transformer.m_window.lock()) {
        const CBox CURRENT = WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox CAPTURE = WINDOW->getFullWindowBoundingBox();
        body = {
            .x = CURRENT.x - CAPTURE.middle().x,
            .y = CURRENT.y - CAPTURE.middle().y,
            .w = CURRENT.w,
            .h = CURRENT.h,
        };
    }
    const auto &options = runtime.m_config.bubble.movement;
    state.bubble.advance(elapsed,
        size,
        body,
        enabled ? hyprimate::BubbleMotionParams{.strength = options.strength,
                      .microbubbles = options.microbubbles,
                      .trail = options.trail,
                      .roundness = options.roundness,
                      .shape = bubble_shape(options.shape),
                      .shape_speed = options.shapeSpeed,
                      .transparency = options.transparency,}
                : hyprimate::BubbleMotionParams{});
    state.impacts.advance(elapsed);
    if (!state.bubble.active()) {
        state.wallpaper.reset();
        state.impacts.release();  // beads still in flight keep their own time from here
    }
}

void SBubbleLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &state = std::get<SBubbleStyle>(transformer.m_style);
    state.bubble.reset();
    state.wallpaper.reset();
    // Droplets of a cancelled opening vanish with it, so none of them may still land on the glass.
    state.impacts.cancel();
}

void SBubbleLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &state = std::get<SBubbleStyle>(transformer.m_style);
    // Film already on the glass dissolves on its own; the window's film comes to rest at once.
    state.bubble.stop_motion();
    state.wake = 0;
}

bool SBubbleLiveStyle::available([[maybe_unused]] const SBubbleStyle &state) noexcept {
    return true;
}

bool SBubbleLiveStyle::deforming(const SBubbleStyle &state) noexcept {
    const auto &bubble = state.bubble;
    return bubble.active() || bubble.stirred();
}

bool SBubbleLiveStyle::builtFor([[maybe_unused]] const SBubbleStyle &state,
    [[maybe_unused]] const Inputs &inputs) noexcept {
    return true;
}

SBubbleStyle SBubbleLiveStyle::initial([[maybe_unused]] const Inputs &inputs, std::uint64_t seed) {
    SBubbleStyle state;
    state.bubble.seed_motion(seed);
    return state;
}

double SBubbleLiveStyle::strength(const CPluginRuntime &runtime) {
    return runtime.m_config.bubble.movement.strength;
}
bool SBubbleLiveStyle::usable([[maybe_unused]] const CPluginRuntime &runtime, [[maybe_unused]] const Resources &resources) {
    return true;
}

SBubbleLiveStyle::Inputs SBubbleLiveStyle::inputs([[maybe_unused]] const CPluginRuntime &runtime) { return {}; }
void SBubbleLiveStyle::prepare([[maybe_unused]] CPluginRuntime &runtime, Resources &resources, bool selected) {
    if (selected) resources.renderer->warm_up();
}
bool SBubbleLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated;
}
