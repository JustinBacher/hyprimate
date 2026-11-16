#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "BubbleRenderer.hpp"

namespace close_style {
    inline bool closeEnabled(const SBubbleCloseData &, const hyprimate::config::SConfig &) {
        return true;
    }
    inline CBox footprint(const SBubbleStyle &, const Record &record) {
        return project(record.extentsDevice, record.renderBox, record.snapshotSize);
    }
    inline bool stage(SBubbleStyle &style,
        Pending &pending,
        const PHLWINDOW &,
        bool,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams,
        const std::function<std::optional<hyprimate::EdgeFrame>()> &,
        const std::function<std::span<const hyprimate::Box>()> &,
        const std::function<std::uint64_t()> &nextSeed,
        const std::function<hyprimate::EdgeSet()> &) {
        if (!style.renderer) return false;
        pending.closeData =
            SBubbleCloseData{.duration = config.close.bubble.duration, .seed = nextSeed()};
        pending.style = std::move(style);
        return true;
    }
    inline bool valid(const SBubbleStyle &, const Pending &, bool blur) {
        return !blur;
    }
    inline void begin(SBubbleStyle &style, const Pending &pending, bool) {
        const auto &data = std::get<SBubbleCloseData>(pending.closeData);
        style.bubble.begin(
            hyprimate::Transition::Close, data.duration, data.seed, pending.captureLogical);
        // An interrupted opening hands over its clock, so its pending beads keep landing in step.
        if (!style.impacts.running()) style.impacts.start();
    }
    inline bool running(const SBubbleStyle &style) {
        return style.bubble.active();
    }
    inline bool finished(const SBubbleStyle &style) {
        return style.bubble.closed();
    }
    inline bool needsOwned(const SBubbleStyle &) {
        return true;
    }
    inline void advance(SBubbleStyle &style, Record &record, double elapsed, bool) {
        style.bubble.advance(elapsed);
        style.impacts.advance(elapsed);
        if (style.bubble.closed()) {
            record.snapshot.reset();  // only the suppression record must outlive the effect
            style.wallpaper.reset();
            style.impacts.release();  // beads still in flight keep their own time from here
        }
    }
    inline bool settled(const SBubbleStyle &) {
        return true;
    }
    inline bool renderNative(SBubbleStyle &,
        Record &,
        Render::CRenderContext &,
        const SP<Desktop::IFadeout> &,
        const SP<Render::ITexture> &,
        CRegion) {
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
        record.extentsDevice = CBox{record.sourceDevice.x + bounds.x * sx,
            record.sourceDevice.y + bounds.y * sy,
            bounds.w * sx,
            bounds.h * sy}
                                   .expand(2.0);
        const double x = std::floor(record.extentsDevice.x), y = std::floor(record.extentsDevice.y);
        record.extentsDevice = CBox{x,
            y,
            std::ceil(record.extentsDevice.x + record.extentsDevice.w) - x,
            std::ceil(record.extentsDevice.y + record.extentsDevice.h) - y};
    }
    inline void render(SBubbleStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CSolidFill> &,
        const SP<CPaperShade> &,
        const SP<CSwarmSprites> &) {
        if (!style.renderer || record.extentsDevice.empty() || record.sourceAlpha <= 0.F) return;
        CBubblePassElement::SData data;
        data.renderer = style.renderer;
        data.draw = SBubbleDraw{
            .window = tex,
            .wallpaper = style.wallpaper,
            .frame = style.bubble.frame(record.captureLogical),
            .canvas = record.extentsDevice.size(),
            .source = record.sourceDevice,
            .textureOrigin = {0.0, 0.0},
            .textureSize = {record.snapshotSize.x, record.snapshotSize.y},
            .outputOrigin = {record.extentsDevice.x, record.extentsDevice.y},
        };
        if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot) {
            const auto source = project(record.sourceDevice, record.renderBox, record.snapshotSize)
                                    .scale(1.0 / record.scale);
            depositBubbleImpacts(style, data.draw.frame, monitor, source.middle());
        }
        data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        data.alpha = record.sourceAlpha;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
}  // namespace close_style
