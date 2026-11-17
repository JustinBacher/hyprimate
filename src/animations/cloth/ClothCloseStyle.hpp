#pragma once

#include <hyprland/src/desktop/view/window/Window.hpp>

#include "animations/shared/CloseSheetRender.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"

namespace close_style {
    inline bool closeEnabled(const SClothCloseData &, const hyprimate::config::SConfig &) {
        return true;
    }
    inline CBox footprint(const SClothStyle &, const Record &record) {
        return project(record.extentsDevice, record.renderBox, record.snapshotSize);
    }
    inline bool stage(SClothStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        bool,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams,
        const std::function<hyprimate::EdgeInput()> &edgeInput,
        const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &nextSeed,
        const std::function<hyprimate::EdgeSet()> &) {
        const auto INPUT = edgeInput();
        const auto FRAME = hyprimate::resolveStyleCloseEdge(INPUT, config.close.cloth);
        const auto MONITOR = window->m_monitor.lock();
        const auto BOXES = obstacles();
        const auto SEED = nextSeed();
        if (!FRAME || !MONITOR) {
            return false;
        }
        const auto &p = config.close.cloth;
        SClothCloseData data;
        data.params = {.duration = p.duration,
            .flutter = p.flutter,
            .shading = p.shading,
            .variation = p.variation,
            .shine = p.shine};
        data.seed = SEED;
        data.capture = toBox(window->getFullWindowBoundingBox());
        data.obstacles.assign(BOXES.begin(), BOXES.end());
        data.geometry = hyprimate::ClothCloseGeometry{
            .edge = FRAME->edge, .screen = toBox(MONITOR->logicalBox())};
        pending.closeData = std::move(data);
        pending.style = std::move(style);
        return true;
    }
    inline bool valid(const SClothStyle &, const Pending &pending, bool blur) {
        return !blur && std::get<SClothCloseData>(pending.closeData).geometry.has_value();
    }
    inline void begin(SClothStyle &style, const Pending &pending, bool) {
        style.sheet.captureSize = pending.captureLogical;
        const auto &data = std::get<SClothCloseData>(pending.closeData);
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
    inline bool needsOwned(const SClothStyle &) {
        return true;
    }
    inline void advance(SClothStyle &style, Record &, double elapsed, bool) {
        style.cloth.advance(elapsed);
    }
    inline bool settled(const SClothStyle &) {
        return true;
    }
    inline bool renderNative(SClothStyle &,
        Record &,
        Render::CRenderContext &,
        const SP<Desktop::IFadeout> &,
        const SP<Render::ITexture> &,
        CRegion) {
        return false;
    }
    inline void fill(SClothStyle &style, Record &record) {
        style.cloth.frame(style.sheet.captureSize, *style.sheet.frame);
        fillSheet(style.sheet, record);
    }
    inline void render(const SClothStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CSolidFill> &,
        const SP<CPaperShade> &shade,
        const SP<CSwarmSprites> &) {
        renderSheet(style.sheet,
            style.cloth.shaded(),
            style.cloth.opacity(),
            record,
            ctx,
            monitor,
            tex,
            shade);
    }
}  // namespace close_style
