#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "LiveRender.hpp"
#include "LiveStyle.hpp"

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
    const auto bounds = state.bubble.bounds({box.w, box.h});
    return CBox{box.x + bounds.x, box.y + bounds.y, bounds.w, bounds.h}.expand(2.0);
}

Render::SWindowTransformBuffer renderLive(SBubbleStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &g,
    const SP<CSolidFill> &,
    const SP<CPaperShade> &,
    const SP<CSwarmSprites> &) {
    const Render::SWindowTransformBuffer failed{
        .framebuffer = in.framebuffer, .box = in.box, .success = false};
    if (!state.renderer) return failed;
    const hyprimate::Vec2 size{g.source.w / g.scale, g.source.h / g.scale};
    const auto frame = state.bubble.frame(size);
    const auto layer = state.renderer->render(ctx,
        SBubbleDraw{
            .window = in.framebuffer->getTexture(),
            .wallpaper = state.wallpaper,
            .frame = frame,
            .canvas = g.canvas.size(),
            .source = g.source,
            .textureOrigin = {in.box.x, in.box.y},
            .textureSize = g.textureSize,
            .outputOrigin = {g.canvas.x, g.canvas.y},
        });
    if (!layer) return failed;
    if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot) {
        const auto monitor = context.monitor.lock();
        const auto centre = context.currentBox.middle();
        depositBubbleImpacts(state, frame, monitor, centre);
        // Film and microbubbles the movement shed since the last frame stay where they were laid,
        // relative to the window drawn now.
        const auto &motion = state.bubble.motion();
        if (!motion.trail().empty() || !motion.releases().empty()) {
            if (state.residue && monitor)
                state.wake = state.residue->trail(monitor,
                    state.window.lock(),
                    state.wake,
                    centre,
                    motion.trail(),
                    motion.releases());
            state.bubble.clearEmissions();
        }
    }
    return {.framebuffer = layer, .box = g.canvas};
}

bool SBubbleLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const CBox capture = window->getFullWindowBoundingBox();
    const hyprimate::Box rest{goal.x + capture.x - current.x,
        goal.y + capture.y - current.y,
        goal.w + capture.w - current.w,
        goal.h + capture.h - current.h};
    if (!hyprimate::isUsable(toBox(goal)) || !hyprimate::isUsable(rest)) return false;
    auto *transformer = runtime.acquireTransformer(window);
    auto &state = std::get<SBubbleStyle>(transformer->m_style);
    const auto &options = runtime.m_config.open.bubble;
    state.bubble.begin(hyprimate::Transition::Open,
        runtime.m_config.open.bubble.duration,
        runtime.m_nextSeed++,
        {rest.w, rest.h},
        {options.count, options.birthRate, options.sizeVariation, options.growthVariation,
            options.interaction, options.damping, options.buoyancy, options.linger});
    if (!state.bubble.active()) return false;
    state.wallpaper = makeShared<SBubbleWallpaper>();
    state.residue = runtime.m_bubbleResidue;
    state.shellImpactsDeposited = state.windowImpactsDeposited = false;
    state.impacts.start();
    // Bubble owns formation, placement and opacity. Keep focus/workspace/user alpha factors intact.
    g_pHyprRenderer->damageBox(capture);
    window->positionAnimation()->setValueAndWarp(goal.pos());
    window->sizeAnimation()->setValueAndWarp(goal.size());
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
    if (!moving) return;
    // The fluid follows the body's centre: an edge dragged by a resize carries it by half as much.
    // A layout animating the window moves it faster than a bubble travels; the film follows.
    const auto &current = decision.currentBox;
    const auto &was = decision.previousBox;
    std::get<SBubbleStyle>(transformer.m_style)
        .bubble.drive({current.x + current.w * .5 - (was.x + was.w * .5),
                          current.y + current.h * .5 - (was.y + was.h * .5)},
            !decision.workspaceMoving && window->positionAnimation()->isBeingAnimated());
}

void SBubbleLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state = transformer ? std::get_if<SBubbleStyle>(&transformer->m_style) : nullptr;
    // A landed opening is already the window's film: movement acts on it while it settles.
    s.transitionBusy = state && state->bubble.active() && !state->bubble.landed();
}

void SBubbleLiveStyle::step(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled) {
    auto &state = std::get<SBubbleStyle>(transformer.m_style);
    if (!enabled && state.bubble.stirred()) cancelMovement(transformer);
    hyprimate::Box body{-size.x * .5, -size.y * .5, size.x, size.y};
    state.window = transformer.m_window;
    if (const auto window = transformer.m_window.lock()) {
        const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox capture = window->getFullWindowBoundingBox();
        body = {
            current.x - capture.middle().x, current.y - capture.middle().y, current.w, current.h};
    }
    const auto &options = runtime.m_config.movement.bubble;
    state.bubble.advance(elapsed,
        size,
        body,
        enabled ? hyprimate::BubbleMotionParams{options.strength,
                      options.microbubbles,
                      options.trail,
                      options.roundness,
                      bubbleShape(options.shape),
                      options.shapeSpeed,
                      options.transparency}
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
