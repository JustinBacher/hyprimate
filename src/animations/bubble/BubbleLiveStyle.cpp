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

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

namespace {
    hyprimate::BubbleShape bubbleShape(hyprimate::config::Shape shape) {
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

CBox liveExtents(const SBubbleStyle &state, const CBox &box) {
    const auto BOUNDS = state.bubble.bounds({.x = box.w, .y = box.h});
    return CBox{box.x + BOUNDS.x, box.y + BOUNDS.y, BOUNDS.w, BOUNDS.h}.expand(2.0);
}

Render::SWindowTransformBuffer renderLive(SBubbleStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &g,
    const SP<CSolidFill> &,
    const SP<CPaperShade> &,
    const SP<CSwarmSprites> &) {
    if (!state.renderer) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }
    const hyprimate::Vec2 SIZE{.x = g.source.w / g.scale, .y = g.source.h / g.scale};
    const auto FRAME = state.bubble.frame(SIZE);
    const auto LAYER = state.renderer->render(ctx,
        SBubbleDraw{
            .window = in.framebuffer->getTexture(),
            .wallpaper = state.wallpaper,
            .frame = FRAME,
            .canvas = g.canvas.size(),
            .source = g.source,
            .textureOrigin = {.x = in.box.x, .y = in.box.y},
            .textureSize = g.textureSize,
            .outputOrigin = {.x = g.canvas.x, .y = g.canvas.y},
        });
    if (!LAYER) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }
    if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot) {
        const auto MONITOR = context.monitor.lock();
        const auto CENTRE = context.currentBox.middle();
        depositBubbleImpacts(state, FRAME, MONITOR, CENTRE);
        // Film and microbubbles the movement shed since the last frame stay where they were laid,
        // relative to the window drawn now.
        const auto &motion = state.bubble.motion();
        if (!motion.trail().empty() || !motion.releases().empty()) {
            if (state.residue && MONITOR) {
                state.wake = state.residue->trail(MONITOR,
                    state.window.lock(),
                    state.wake,
                    CENTRE,
                    motion.trail(),
                    motion.releases());
            }
            state.bubble.clearEmissions();
        }
    }
    return {.framebuffer = LAYER, .box = g.canvas};
}

bool SBubbleLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    const hyprimate::Box REST{.x = GOAL.x + CAPTURE.x - CURRENT.x,
        .y = GOAL.y + CAPTURE.y - CURRENT.y,
        .w = GOAL.w + CAPTURE.w - CURRENT.w,
        .h = GOAL.h + CAPTURE.h - CURRENT.h};
    if (!hyprimate::is_usable(toBox(GOAL)) || !hyprimate::is_usable(REST)) {
        return false;
    }
    auto *transformer = runtime.acquireTransformer(window);
    auto &state = std::get<SBubbleStyle>(transformer->m_style);
    state.bubble.begin(hyprimate::Transition::Open,
        runtime.m_config.open.bubble.duration,
        runtime.m_nextSeed++,
        {.x = REST.w, .y = REST.h});
    if (!state.bubble.active()) {
        return false;
    }
    state.wallpaper = makeShared<SBubbleWallpaper>();
    state.residue = runtime.m_bubbleResidue;
    state.shellImpactsDeposited = state.windowImpactsDeposited = false;
    state.impacts.start();
    // Bubble owns formation, placement and opacity. Keep focus/workspace/user alpha factors intact.
    g_pHyprRenderer->damageBox(CAPTURE);
    window->positionAnimation()->setValueAndWarp(GOAL.pos());
    window->sizeAnimation()->setValueAndWarp(GOAL.size());
    auto &fade = window->presentation().alpha(Desktop::View::WINDOW_ALPHA_FADE);
    fade->setValueAndWarp(fade->goal());
    runtime.rebaseline(entry, window);
    return true;
}

void SBubbleLiveStyle::movement(CPluginRuntime &,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double,
    bool,
    bool moving) {
    if (!moving) {
        return;
    }
    // The fluid follows the body's centre: an edge dragged by a resize carries it by half as much.
    // A layout animating the window moves it faster than a bubble travels; the film follows.
    const auto &current = decision.current_box;
    const auto &was = decision.previous_box;
    std::get<SBubbleStyle>(transformer.m_style)
        .bubble.drive({.x = current.x + (current.w * .5) - (was.x + (was.w * .5)),
                          .y = current.y + (current.h * .5) - (was.y + (was.h * .5))},
            !decision.workspace_moving && window->positionAnimation()->isBeingAnimated());
}

void SBubbleLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SBubbleStyle>(&transformer->m_style) : nullptr;
    // A landed opening is already the window's film: movement acts on it while it settles.
    s.transition_busy = (state != nullptr) && state->bubble.active() && !state->bubble.landed();
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
        body = {.x = CURRENT.x - CAPTURE.middle().x,
            .y = CURRENT.y - CAPTURE.middle().y,
            .w = CURRENT.w,
            .h = CURRENT.h};
    }
    const auto &options = runtime.m_config.movement.bubble;
    state.bubble.advance(elapsed,
        size,
        body,
        enabled ? hyprimate::BubbleMotionParams{.strength = options.strength,
                      .microbubbles = options.microbubbles,
                      .trail = options.trail,
                      .roundness = options.roundness,
                      .shape = bubbleShape(options.shape),
                      .shapeSpeed = options.shapeSpeed,
                      .transparency = options.transparency}
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
    state.bubble.stopMotion();
    state.wake = 0;
}

bool SBubbleLiveStyle::available(const CAnimationTransformer &transformer) noexcept {
    return bool(std::get<SBubbleStyle>(transformer.m_style).renderer);
}

bool SBubbleLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    const auto &bubble = std::get<SBubbleStyle>(transformer.m_style).bubble;
    return bubble.active() || bubble.stirred();
}

bool SBubbleLiveStyle::builtFor(const CAnimationTransformer &,
    hyprimate::config::Style style,
    hyprimate::MaterialParams,
    hyprimate::ClothMovementParams) noexcept {
    return style == hyprimate::config::Style::Bubble;
}

SStyleState SBubbleLiveStyle::initial(
    hyprimate::MaterialParams, hyprimate::ClothMovementParams, std::uint64_t seed) {
    SBubbleStyle state;
    state.bubble.seedMotion(seed);
    return state;
}

double SBubbleLiveStyle::strength(const CPluginRuntime &runtime) {
    return runtime.m_config.movement.bubble.strength;
}
bool SBubbleLiveStyle::usable(const CPluginRuntime &) {
    return true;
}
