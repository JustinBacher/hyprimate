#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "LiveRender.hpp"
#include "LiveStyle.hpp"

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
    if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot)
        depositBubbleImpacts(state, frame, context.monitor.lock(), context.currentBox.middle());
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
    state.bubble.begin(hyprimate::Transition::Open,
        runtime.m_config.open.bubble.duration,
        runtime.m_nextSeed++,
        {rest.w, rest.h});
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
    CAnimationTransformer &,
    const PHLWINDOW &,
    const hyprimate::TrackDecision &,
    double,
    bool,
    bool) {
}

void SBubbleLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state = transformer ? std::get_if<SBubbleStyle>(&transformer->m_style) : nullptr;
    s.transitionBusy = state && state->bubble.active();
}

void SBubbleLiveStyle::step(
    CPluginRuntime &, CAnimationTransformer &transformer, double elapsed, hyprimate::Vec2, bool) {
    auto &state = std::get<SBubbleStyle>(transformer.m_style);
    state.bubble.advance(elapsed);
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

void SBubbleLiveStyle::cancelMovement(CAnimationTransformer &) {
}

bool SBubbleLiveStyle::available(const CAnimationTransformer &transformer) noexcept {
    return bool(std::get<SBubbleStyle>(transformer.m_style).renderer);
}

bool SBubbleLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    return std::get<SBubbleStyle>(transformer.m_style).bubble.active();
}

bool SBubbleLiveStyle::builtFor(const CAnimationTransformer &,
    hyprimate::config::Style style,
    hyprimate::MaterialParams,
    hyprimate::ClothMovementParams) noexcept {
    return style == hyprimate::config::Style::Bubble;
}

SStyleState SBubbleLiveStyle::initial(
    hyprimate::MaterialParams, hyprimate::ClothMovementParams, std::uint64_t) {
    return SBubbleStyle{};
}

double SBubbleLiveStyle::strength(const CPluginRuntime &) {
    return 0.0;
}
bool SBubbleLiveStyle::usable(const CPluginRuntime &) {
    return true;
}
