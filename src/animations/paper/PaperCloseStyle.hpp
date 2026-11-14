#pragma once

#include "animations/shared/CloseSheetRender.hpp"

namespace close_style {
inline bool closeEnabled(const SPaperCloseData &, const liquid_glass::config::SConfig &) { return true; }


inline bool stage(SPaperStyle &style, Pending &pending, const PHLWINDOW &, bool,
                  const liquid_glass::config::SConfig &config, liquid_glass::MaterialParams,
                  const std::function<std::optional<liquid_glass::EdgeFrame>()> &,
                  const std::function<std::span<const liquid_glass::Box>()> &,
                  const std::function<std::uint64_t()> &nextSeed,
                  const std::function<liquid_glass::EdgeSet()> &) {
    const auto &p = config.close.paper;
    pending.closeData = SPaperCloseData{
        .params = {.duration = p.duration, .crumpliness = p.crumpliness, .shading = p.shading, .variation = p.variation},
        .seed = nextSeed()};
    pending.style = std::move(style);
    return true;
}
inline bool valid(const SPaperStyle &, const Pending &, bool blur) { return !blur; }
inline void begin(SPaperStyle &style, const Pending &pending, bool) {
    style.sheet.captureSize = pending.captureLogical;
    const auto &data = std::get<SPaperCloseData>(pending.closeData);
    style.crumple.begin(liquid_glass::Transition::Close, data.params, data.seed);
}
inline bool running(const SPaperStyle &style) { return style.crumple.active(); }
inline bool finished(const SPaperStyle &style) { return style.crumple.closed(); }
inline bool needsOwned(const SPaperStyle &) { return true; }
inline void advance(SPaperStyle &style, Record &, double elapsed, bool) { style.crumple.advance(elapsed); }
inline bool settled(const SPaperStyle &) { return true; }
inline bool renderNative(SPaperStyle &, Record &, Render::CRenderContext &, const SP<Desktop::IFadeout> &,
                         const SP<Render::ITexture> &, CRegion) { return false; }

inline CBox footprint(const SPaperStyle &, const Record &record) {
    return project(record.extentsDevice, record.renderBox, record.snapshotSize);
}
inline void fill(SPaperStyle &style, Record &record) {
    style.crumple.frame(style.sheet.captureSize, *style.sheet.frame);
    fillSheet(style.sheet, record);
}
inline void render(SPaperStyle &style, Record &record, Render::CRenderContext &ctx, const PHLMONITOR &monitor,
                   const SP<Render::ITexture> &tex, const SP<CSolidFill> &, const SP<CPaperShade> &shade,
                   const SP<CSwarmSprites> &) {
    renderSheet(style.sheet, style.crumple.shaded(), style.crumple.opacity(), record, ctx, monitor, tex, shade);
}
}
