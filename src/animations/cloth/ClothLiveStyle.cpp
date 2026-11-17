#include <cmath>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "LiveStyle.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"
#include "animations/shared/LiveSheetRender.hpp"

CBox liveExtents(const SClothStyle &state, const CBox &box) {
    return liveSheetExtents(state.sheet, box);
}

Render::SWindowTransformBuffer renderLive(SClothStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const Render::SWindowTransformContext &,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &) {
    return renderLiveSheet(
        state.sheet, state.cloth.opacity(), state.cloth.shaded(), ctx, in, geometry, shade);
}

bool SClothLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    if (const auto frame = hyprimate::resolveStyleOpenEdge(
            runtime.edgeInputFor(window, true), runtime.m_config.open.cloth))
        entry.openEdge = frame->edge;
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const CBox capture = window->getFullWindowBoundingBox();
    const CBox rest{goal.x + (capture.x - current.x),
        goal.y + (capture.y - current.y),
        goal.w + (capture.w - current.w),
        goal.h + (capture.h - current.h)};
    const auto usable = [](const CBox &box) {
        return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
               std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
    };
    if (!usable(goal) || !usable(rest)) return false;
    runtime.m_neighbours.clear();
    runtime.gatherClothObstacles(window);
    auto *transformer = runtime.acquireTransformer(window);
    const auto &cloth = runtime.m_config.open.cloth;
    auto &state = std::get<SClothStyle>(transformer->m_style);
    state.cloth.reset();
    state.cloth.begin(hyprimate::Transition::Open,
        {.duration = cloth.duration,
            .flutter = cloth.flutter,
            .shading = cloth.shading,
            .variation = cloth.variation,
            .shine = cloth.shine},
        runtime.m_nextSeed++,
        toBox(rest),
        runtime.m_neighbours,
        std::nullopt);
    state.sheet.captureSize = {rest.w, rest.h};
    state.cloth.frame(state.sheet.captureSize, *state.sheet.frame);
    if (state.cloth.active()) {
        g_pHyprRenderer->damageBox(capture);
        window->positionAnimation()->setValueAndWarp(goal.pos());
        window->sizeAnimation()->setValueAndWarp(goal.size());
        runtime.rebaseline(entry, window);
    }
    return true;
}

void SClothLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &,
    const hyprimate::TrackDecision &decision,
    double elapsed,
    bool enabled,
    bool moving) {
    auto &state = std::get<SClothStyle>(transformer.m_style);
    if (moving && !state.cloth.moving()) {
        state.cloth.beginMovement(state.movement, runtime.m_nextSeed++, decision.previousCapture);
        state.sheet.captureSize = {decision.previousCapture.w, decision.previousCapture.h};
        state.cloth.frame(state.sheet.captureSize, *state.sheet.frame);
    }
    if (enabled && state.cloth.moving()) {
        const auto &capture = decision.currentCapture;
        state.cloth.updateMovement(capture, elapsed);
        state.sheet.captureSize = {capture.w, capture.h};
        state.cloth.frame(state.sheet.captureSize, *state.sheet.frame);
    }
}

void SClothLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *cloth = transformer ? std::get_if<SClothStyle>(&transformer->m_style) : nullptr;
    s.handoff = cloth && cloth->cloth.active();
    s.transitionBusy = cloth && cloth->cloth.active() && !s.handoff;
    s.handoffMoving = cloth && cloth->cloth.moving();
}

void SClothLiveStyle::step(CPluginRuntime &,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style);
    if (!enabled && cloth.cloth.moving()) cancelMovement(transformer);
    if (!cloth.cloth.active()) return;
    cloth.sheet.captureSize = size;
    cloth.cloth.advance(elapsed);
    cloth.cloth.frame(size, *cloth.sheet.frame);
}

void SClothLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style).cloth;
    if (cloth.active() && !cloth.moving()) cloth.reset();
}

void SClothLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style);
    if (cloth.cloth.moving()) {
        cloth.cloth.reset();
        cloth.cloth.frame(cloth.sheet.captureSize, *cloth.sheet.frame);
    }
}

bool SClothLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    return std::get<SClothStyle>(transformer.m_style).cloth.active();
}

bool SClothLiveStyle::builtFor(const CAnimationTransformer &transformer,
    hyprimate::config::Style style,
    hyprimate::MaterialParams,
    hyprimate::ClothMovementParams cloth) noexcept {
    return style == hyprimate::config::Style::Cloth &&
           std::get<SClothStyle>(transformer.m_style).movement == cloth;
}

bool SClothLiveStyle::available(const CAnimationTransformer &transformer) noexcept {
    return bool(std::get<SClothStyle>(transformer.m_style).sheet.frame);
}

SStyleState SClothLiveStyle::initial(
    hyprimate::MaterialParams, hyprimate::ClothMovementParams cloth, std::uint64_t) {
    return SClothStyle{.movement = cloth};
}

double SClothLiveStyle::strength(const CPluginRuntime &runtime) {
    return runtime.m_config.movement.cloth.strength;
}
bool SClothLiveStyle::usable(const CPluginRuntime &runtime) {
    const auto params = runtime.clothMovementParams();
    return std::isfinite(params.flutter) && std::isfinite(params.shading) &&
           std::isfinite(params.variation);
}
