#pragma once

#include "animations/shared/CloseSheetRender.hpp"

namespace close_style {
    inline bool close_enabled([[maybe_unused]] const SPaperCloseData &data,
        [[maybe_unused]] const hyprimate::config::SConfig &config) {
        return true;
    }

    inline bool stage(SPaperStyle &style,
        Pending &pending,
        [[maybe_unused]] const PHLWINDOW &window,
        [[maybe_unused]] bool state_active,
        const hyprimate::config::SConfig &config,
        [[maybe_unused]] hyprimate::LiquidParams liquid,
        [[maybe_unused]] const std::function<hyprimate::EdgeInput()> &edge_input,
        [[maybe_unused]] const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed,
        [[maybe_unused]] const std::function<hyprimate::EdgeSet()> &swarm_sides) {
        const auto &paper_config = config.close.paper;
        pending.close_data = SPaperCloseData{
            .params =
                {
                    .duration = paper_config.duration,
                    .crumpliness = paper_config.crumpliness,
                    .shading = paper_config.shading,
                    .variation = paper_config.variation,
                },
            .seed = next_seed(),
        };
        pending.style = std::move(style);
        return true;
    }
    inline bool valid([[maybe_unused]] const SPaperStyle &style,
        [[maybe_unused]] const Pending &pending,
        bool blur) {
        return !blur;
    }
    inline void begin(SPaperStyle &style, const Pending &pending, [[maybe_unused]] bool blur) {
        style.sheet.capture_size = pending.capture_logical;
        const auto &data = std::get<SPaperCloseData>(pending.close_data);
        style.crumple.begin(hyprimate::Transition::Close, data.params, data.seed);
    }
    inline bool running(const SPaperStyle &style) {
        return style.crumple.active();
    }
    inline bool finished(const SPaperStyle &style) {
        return style.crumple.closed();
    }
    inline bool needs_owned([[maybe_unused]] const SPaperStyle &style) {
        return true;
    }
    inline void advance(SPaperStyle &style,
        [[maybe_unused]] const Record &record,
        double elapsed,
        [[maybe_unused]] bool native) {
        style.crumple.advance(elapsed);
    }
    inline bool settled([[maybe_unused]] const SPaperStyle &style) {
        return true;
    }
    inline bool render_native([[maybe_unused]] const SPaperStyle &style,
        [[maybe_unused]] const Record &record,
        [[maybe_unused]] const Render::CRenderContext &ctx,
        [[maybe_unused]] const SP<Desktop::IFadeout> &fadeout,
        [[maybe_unused]] const SP<Render::ITexture> &tex,
        [[maybe_unused]] const CRegion &damage) {
        return false;
    }

    inline CBox footprint([[maybe_unused]] const SPaperStyle &style, const Record &record) {
        return project(record.extents_device, record.render_box, record.snapshot_size);
    }
    inline void fill(SPaperStyle &style, Record &record) {
        style.crumple.frame(style.sheet.capture_size, *style.sheet.frame);
        fill_sheet(style.sheet, record);
    }
    inline void render(const SPaperStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        [[maybe_unused]] const SP<CSolidFill> &solid_fill,
        const SP<CSheetShade> &shade,
        [[maybe_unused]] const SP<CSwarmSprites> &sprites) {
        render_sheet(style.sheet,
            style.crumple.shaded(),
            style.crumple.opacity(),
            record,
            ctx,
            monitor,
            tex,
            shade);
    }
}  // namespace close_style
