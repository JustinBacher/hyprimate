#include "animations/shared/LiveSheetRender.hpp"
#include "LiveStyle.hpp"
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <cmath>
using liquid_glass::Vec2;

CBox liveExtents(const SPaperStyle &state, const CBox &box) { return liveSheetExtents(state.sheet, box); }

Render::SWindowTransformBuffer renderLive(SPaperStyle &state, Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in, const Render::SWindowTransformContext &,
    const SLiveRenderGeometry &geometry, const SP<CSolidFill> &, const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &) {
    return renderLiveSheet(state.sheet, state.crumple.opacity(), state.crumple.shaded(), ctx, in, geometry, shade);
}

bool SPaperLiveStyle::open(CPluginRuntime &runtime, CPluginRuntime::STracked &, const PHLWINDOW &window) {
    const CBox capture = window->getFullWindowBoundingBox();
    const auto &paper = runtime.m_config.open.paper;
    auto *transformer = runtime.acquireTransformer(window);
    auto &state = std::get<SPaperStyle>(transformer->m_style);
    state.crumple.reset();
    state.crumple.begin(liquid_glass::Transition::Open,
                        {.duration = paper.duration, .crumpliness = paper.crumpliness, .shading = paper.shading,
                         .variation = paper.variation}, runtime.m_nextSeed++);
    state.sheet.captureSize = {capture.w, capture.h};
    state.crumple.frame(state.sheet.captureSize, *state.sheet.frame);
    return true;
}

void SPaperLiveStyle::movement(CPluginRuntime &, CAnimationTransformer &, const PHLWINDOW &,
                               const liquid_glass::TrackDecision &, double, bool, bool) {}

void SPaperLiveStyle::snapshot(const CAnimationTransformer *transformer, liquid_glass::WindowSnapshot &s) {
    if (transformer) {
        const auto *paper = std::get_if<SPaperStyle>(&transformer->m_style);
        s.transitionBusy = paper && paper->crumple.active();
    }
}

void SPaperLiveStyle::step(CPluginRuntime &, CAnimationTransformer &transformer, double elapsed,
                           liquid_glass::Vec2 size, bool) {
    auto &paper = std::get<SPaperStyle>(transformer.m_style);
    if (!paper.crumple.active())
        return;
    paper.sheet.captureSize = size;
    paper.crumple.advance(elapsed);
    paper.crumple.frame(size, *paper.sheet.frame);
}

void SPaperLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &paper = std::get<SPaperStyle>(transformer.m_style);
    if (paper.crumple.active())
        paper.crumple.reset();
}

void SPaperLiveStyle::cancelMovement(CAnimationTransformer &) {}

bool SPaperLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    return std::get<SPaperStyle>(transformer.m_style).crumple.active();
}

bool SPaperLiveStyle::builtFor(const CAnimationTransformer &, liquid_glass::config::Style style,
                               liquid_glass::MaterialParams, liquid_glass::ClothMovementParams) noexcept {
    return style == liquid_glass::config::Style::Paper;
}

bool SPaperLiveStyle::available(const CAnimationTransformer &transformer) noexcept {
    return bool(std::get<SPaperStyle>(transformer.m_style).sheet.frame);
}

SStyleState SPaperLiveStyle::initial(liquid_glass::MaterialParams, liquid_glass::ClothMovementParams, std::uint64_t) {
    return SPaperStyle{};
}

double SPaperLiveStyle::strength(const CPluginRuntime &) { return 0.0; }
bool SPaperLiveStyle::usable(const CPluginRuntime &runtime) {
    const auto params = runtime.effectiveMaterialParams();
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) && std::isfinite(params.momentum) &&
           std::isfinite(params.variation);
}
