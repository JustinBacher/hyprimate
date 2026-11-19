#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "BubbleRenderer.hpp"

namespace close_style {
    inline bool close_enabled([[maybe_unused]] const SBubbleCloseData &data,
        [[maybe_unused]] const hyprimate::config::SConfig &config) {
        return true;
    }
    inline CBox footprint([[maybe_unused]] const SBubbleStyle &style, const Record &record) {
        return project(record.extents_device, record.render_box, record.snapshot_size);
    }
    inline bool stage(SBubbleStyle &style,
        Pending &pending,
        [[maybe_unused]] const PHLWINDOW &window,
        [[maybe_unused]] bool state_active,
        const hyprimate::config::SConfig &config,
        [[maybe_unused]] const std::function<hyprimate::EdgeInput()> &edge_input,
        [[maybe_unused]] const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed) {
        if (!style.renderer) {
            return false;
        }
        pending.close_data =
            SBubbleCloseData{.duration = config.close.bubble.duration, .seed = next_seed()};
        pending.style = std::move(style);
        return true;
    }
    inline bool valid([[maybe_unused]] const SBubbleStyle &style,
        [[maybe_unused]] const Pending &pending,
        bool blur) {
        return !blur;
    }
    inline void begin(SBubbleStyle &style, const Pending &pending, [[maybe_unused]] bool blur) {
        const auto &data = std::get<SBubbleCloseData>(pending.close_data);
        style.bubble.begin(hyprimate::Transition::Close,
            hyprimate::BubbleDuration{data.duration},
            data.seed,
            pending.capture_logical);
        // An interrupted opening hands over its clock, so its pending beads keep landing in step.
        if (!style.impacts.running()) {
            style.impacts.start();
        }
    }
    inline bool running(const SBubbleStyle &style) {
        return style.bubble.active();
    }
    inline bool finished(const SBubbleStyle &style) {
        return style.bubble.closed();
    }
    inline bool needs_owned([[maybe_unused]] const SBubbleStyle &style) {
        return true;
    }
    inline void advance(
        SBubbleStyle &style, Record &record, double elapsed, [[maybe_unused]] bool native) {
        style.bubble.advance(elapsed);
        style.impacts.advance(elapsed);
        if (style.bubble.closed()) {
            record.snapshot.reset();  // only the suppression record must outlive the effect
            style.wallpaper.reset();
            style.impacts.release();  // beads still in flight keep their own time from here
        }
    }
    inline bool settled([[maybe_unused]] const SBubbleStyle &style) {
        return true;
    }
    inline bool render_native([[maybe_unused]] const SBubbleStyle &style,
        [[maybe_unused]] const Record &record,
        [[maybe_unused]] const Render::CRenderContext &ctx,
        [[maybe_unused]] const SP<Desktop::IFadeout> &fadeout,
        [[maybe_unused]] const SP<Render::ITexture> &tex,
        [[maybe_unused]] const CRegion &damage) {
        return false;
    }
    inline void fill(const SBubbleStyle &style, Record &record) {
        const auto BOUNDS = style.bubble.bounds(record.capture_logical);
        if (BOUNDS.w <= 0.0 || BOUNDS.h <= 0.0) {
            record.extents_device = CBox{};
            return;
        }
        const double SCALE_X = record.source_device.w / record.capture_logical.x;
        const double SCALE_Y = record.source_device.h / record.capture_logical.y;
        record.extents_device = CBox{record.source_device.x + (BOUNDS.x * SCALE_X),
            record.source_device.y + (BOUNDS.y * SCALE_Y),
            BOUNDS.w * SCALE_X,
            BOUNDS.h * SCALE_Y}
                                    .expand(2.0);
        const double LEFT = std::floor(record.extents_device.x);
        const double TOP = std::floor(record.extents_device.y);
        record.extents_device = CBox{LEFT,
            TOP,
            std::ceil(record.extents_device.x + record.extents_device.w) - LEFT,
            std::ceil(record.extents_device.y + record.extents_device.h) - TOP};
    }
    inline void render(SBubbleStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        [[maybe_unused]] const SP<CSolidFill> &solid_fill,
        [[maybe_unused]] const SP<CSheetShade> &shade,
        [[maybe_unused]] const SP<CSwarmSprites> &sprites) {
        if (!style.renderer || record.extents_device.empty() || record.source_alpha <= 0.F) {
            return;
        }
        CBubblePassElement::SData data;
        data.renderer = style.renderer;
        data.draw = SBubbleDraw{
            .window = tex,
            .wallpaper = style.wallpaper,
            .frame = style.bubble.frame(record.capture_logical),
            .canvas = record.extents_device.size(),
            .source = record.source_device,
            .texture_origin = {.x = 0.0, .y = 0.0},
            .texture_size = {.x = record.snapshot_size.x, .y = record.snapshot_size.y},
            .output_origin = {.x = record.extents_device.x, .y = record.extents_device.y},
        };
        if (!ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot) {
            const auto SOURCE =
                project(record.source_device, record.render_box, record.snapshot_size)
                    .scale(1.0 / record.scale);
            deposit_bubble_impacts(style, data.draw.frame, monitor, SOURCE.middle());
        }
        data.box = project(record.extents_device, record.render_box, record.snapshot_size);
        data.alpha = record.source_alpha;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
}  // namespace close_style
