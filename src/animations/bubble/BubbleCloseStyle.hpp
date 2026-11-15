#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "BubbleRenderer.hpp"

namespace close_style {
inline bool closeEnabled(const SBubbleCloseData &, const liquid_glass::config::SConfig &) { return true; }
inline CBox footprint(const SBubbleStyle &, const Record &record) {
    return project(record.extentsDevice, record.renderBox, record.snapshotSize);
}
inline bool stage(SBubbleStyle &style, Pending &pending, const PHLWINDOW &, bool,
                  const liquid_glass::config::SConfig &config, liquid_glass::MaterialParams,
                  const std::function<std::optional<liquid_glass::EdgeFrame>()> &,
                  const std::function<std::span<const liquid_glass::Box>()> &,
                  const std::function<std::uint64_t()> &nextSeed, const std::function<liquid_glass::EdgeSet()> &) {
    if (!style.renderer)
        return false;
    pending.closeData = SBubbleCloseData{.duration = config.close.bubble.duration, .seed = nextSeed()};
    pending.style = std::move(style);
    return true;
}
inline bool valid(const SBubbleStyle &, const Pending &, bool blur) { return !blur; }
inline void begin(SBubbleStyle &style, const Pending &pending, bool) {
    const auto &data = std::get<SBubbleCloseData>(pending.closeData);
    style.bubble.begin(liquid_glass::Transition::Close, data.duration, data.seed, pending.captureLogical);
}
inline bool running(const SBubbleStyle &style) { return style.bubble.active(); }
inline bool finished(const SBubbleStyle &style) { return style.bubble.closed(); }
inline bool needsOwned(const SBubbleStyle &) { return true; }
inline void advance(SBubbleStyle &style, Record &record, double elapsed, bool) {
    style.bubble.advance(elapsed);
    if (style.bubble.closed())
        record.snapshot.reset(); // only the suppression record must outlive the effect
}
inline bool settled(const SBubbleStyle &) { return true; }
inline bool renderNative(SBubbleStyle &, Record &, Render::CRenderContext &, const SP<Desktop::IFadeout> &,
                         const SP<Render::ITexture> &, CRegion) {
    return false;
}
inline void fill(SBubbleStyle &style, Record &record) {
    const auto bounds = style.bubble.bounds(record.captureLogical);
    if (bounds.w <= 0.0 || bounds.h <= 0.0) {
        record.extentsDevice = CBox{};
        return;
    }
    const double sx = record.sourceDevice.w / record.captureLogical.x;
    const double sy = record.sourceDevice.h / record.captureLogical.y;
    record.extentsDevice =
        CBox{record.sourceDevice.x + bounds.x * sx, record.sourceDevice.y + bounds.y * sy, bounds.w * sx, bounds.h * sy}
            .expand(2.0);
    const double x = std::floor(record.extentsDevice.x), y = std::floor(record.extentsDevice.y);
    record.extentsDevice = CBox{x, y, std::ceil(record.extentsDevice.x + record.extentsDevice.w) - x,
                                std::ceil(record.extentsDevice.y + record.extentsDevice.h) - y};
}
inline void render(SBubbleStyle &style, Record &record, Render::CRenderContext &ctx, const PHLMONITOR &monitor,
                   const SP<Render::ITexture> &tex, const SP<CSolidFill> &, const SP<CPaperShade> &,
                   const SP<CSwarmSprites> &) {
    if (!style.renderer || record.extentsDevice.empty() || record.sourceAlpha <= 0.F)
        return;
    CBubbleClosePassElement::SData data;
    data.renderer = style.renderer;
    data.draw = SBubbleDraw{
        .window = tex,
        .frame = style.bubble.frame(record.captureLogical),
        .canvas = record.extentsDevice.size(),
        .source = record.sourceDevice,
        .textureOrigin = {0.0, 0.0},
        .textureSize = {record.snapshotSize.x, record.snapshotSize.y},
        .outputOrigin = {record.extentsDevice.x, record.extentsDevice.y},
    };
    data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
    data.alpha = record.sourceAlpha;
    data.scale = record.scale;
    data.damage =
        CRegion{CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(data.box)};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubbleClosePassElement>(std::move(data)));
}
} // namespace close_style
