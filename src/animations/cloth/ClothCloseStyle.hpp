#pragma once

#include <hyprland/src/desktop/view/window/Window.hpp>

#include "animations/shared/CloseSheetRender.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"

namespace close_style {
    inline bool close_enabled([[maybe_unused]] const SClothCloseData &data,
        [[maybe_unused]] const hyprimate::config::SConfig &config) {
        return true;
    }
    inline CBox footprint([[maybe_unused]] const SClothStyle &style, const Record &record) {
        return project(record.extents_device, record.render_box, record.snapshot_size);
    }
    inline bool stage(SClothStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        [[maybe_unused]] bool state_active,
        const hyprimate::config::SConfig &config,
        const std::function<hyprimate::EdgeInput()> &edge_input,
        const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed) {
        const auto INPUT = edge_input();
        const auto FRAME = hyprimate::resolve_style_close_edge(INPUT, config.close.cloth);
        const auto MONITOR = window->m_monitor.lock();
        const auto BOXES = obstacles();
        const auto SEED = next_seed();
        if (!FRAME || !MONITOR) {
            return false;
        }
        const auto &cloth_config = config.close.cloth;
        SClothCloseData data;
        data.params = {
            .duration = cloth_config.duration,
            .flutter = cloth_config.flutter,
            .shading = cloth_config.shading,
            .variation = cloth_config.variation,
            .shine = cloth_config.shine,
        };
        data.seed = SEED;
        data.capture = to_box(window->getFullWindowBoundingBox());
        data.obstacles.assign(BOXES.begin(), BOXES.end());
        data.geometry = hyprimate::ClothCloseGeometry{
            .edge = FRAME->edge,
            .screen = to_box(MONITOR->logicalBox()),
        };
        pending.close_data = std::move(data);
        pending.style = std::move(style);
        return true;
    }
    inline bool valid(
        [[maybe_unused]] const SClothStyle &style, const Pending &pending, bool blur) {
        return !blur && std::get<SClothCloseData>(pending.close_data).geometry.has_value();
    }
    inline void begin(SClothStyle &style, const Pending &pending, [[maybe_unused]] bool blur) {
        style.sheet.capture_size = pending.capture_logical;
        const auto &data = std::get<SClothCloseData>(pending.close_data);
        style.cloth.begin(hyprimate::Transition::Close,
            data.params,
            data.seed,
            data.capture,
            data.obstacles,
            data.geometry);
    }
    inline bool running(const SClothStyle &style) {
        return style.cloth.active();
    }
    inline bool finished(const SClothStyle &style) {
        return style.cloth.closed();
    }
    inline bool needs_owned([[maybe_unused]] const SClothStyle &style) {
        return true;
    }
    inline void advance(SClothStyle &style,
        [[maybe_unused]] const Record &record,
        double elapsed,
        [[maybe_unused]] bool native) {
        style.cloth.advance(elapsed);
    }
    inline bool settled([[maybe_unused]] const SClothStyle &style) {
        return true;
    }
    inline bool render_native([[maybe_unused]] const SClothStyle &style,
        [[maybe_unused]] const Record &record,
        [[maybe_unused]] const Render::CRenderContext &ctx,
        [[maybe_unused]] const SP<Desktop::IFadeout> &fadeout,
        [[maybe_unused]] const SP<Render::ITexture> &tex,
        [[maybe_unused]] const CRegion &damage) {
        return false;
    }
    inline void fill(SClothStyle &style, Record &record) {
        style.cloth.frame(style.sheet.capture_size, *style.sheet.frame);
        fill_sheet(style.sheet, record);
    }
    inline void render(const SClothStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        [[maybe_unused]] const SP<CSolidFill> &solid_fill,
        const SP<CSheetShade> &shade,
        [[maybe_unused]] const SP<CSwarmSprites> &sprites) {
        render_sheet(style.sheet,
            style.cloth.shaded(),
            style.cloth.opacity(),
            record,
            ctx,
            monitor,
            tex,
            shade);
    }
}  // namespace close_style
