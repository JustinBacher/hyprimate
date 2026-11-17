#pragma once

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "../../CloseStyleAdapter.hpp"
#include "SwarmConfig.hpp"
#include "SwarmRenderer.hpp"

namespace close_style {
    inline bool closeEnabled(const SSwarmCloseData &, const hyprimate::config::SConfig &) {
        return true;
    }
    inline CBox footprint(const SSwarmStyle &, const Record &record) {
        return project(record.extentsDevice, record.renderBox, record.snapshotSize);
    }
    inline bool stage(SSwarmStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        bool,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams,
        const std::function<hyprimate::EdgeInput()> &,
        const std::function<std::span<const hyprimate::Box>()> &,
        const std::function<std::uint64_t()> &nextSeed,
        const std::function<hyprimate::EdgeSet()> &swarmSides) {
        const auto MONITOR = window->m_monitor.lock();
        const auto SIDES = swarmSides();
        const auto SEED = nextSeed();
        if (!MONITOR || SIDES.empty()) {
            return false;
        }
        pending.closeData = SSwarmCloseData{.params = swarmParams(config.close.swarm),
            .seed = SEED,
            .placement = {.capture = toBox(window->getFullWindowBoundingBox()),
                .body = toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
                .screen = toBox(MONITOR->logicalBox())},
            .sides = SIDES,
            .creature = swarmCreature(config.close.swarm)};
        pending.style = std::move(style);
        return true;
    }
    inline bool valid(const SSwarmStyle &, const Pending &, bool blur) {
        return !blur;
    }
    inline void begin(SSwarmStyle &style, const Pending &pending, bool) {
        const auto &data = std::get<SSwarmCloseData>(pending.closeData);
        // A swarm already in flight keeps the creature it is drawn as.
        if (!style.swarm.active()) {
            style.creature = data.creature;
        }
        style.swarm.begin(
            hyprimate::Transition::Close, data.params, data.seed, data.placement, data.sides);
    }
    inline bool running(const SSwarmStyle &style) {
        return style.swarm.active();
    }
    inline bool finished(const SSwarmStyle &style) {
        return style.swarm.closed();
    }
    inline bool needsOwned(const SSwarmStyle &) {
        return true;
    }
    inline void advance(SSwarmStyle &style, Record &, double elapsed, bool) {
        style.swarm.advance(elapsed);
    }
    inline bool settled(const SSwarmStyle &) {
        return true;
    }
    inline bool renderNative(SSwarmStyle &,
        Record &,
        Render::CRenderContext &,
        const SP<Desktop::IFadeout> &,
        const SP<Render::ITexture> &,
        CRegion) {
        return false;
    }
    // Extents only: the meshes need the creature's sheet, which the renderer supplies when it
    // draws.
    inline void fill(const SSwarmStyle &style, Record &record) {
        const hyprimate::Box BOUNDS = style.swarm.bounds();
        const hyprimate::Vec2 SIZE = style.swarm.size();
        if (!(BOUNDS.w > 0.0 && BOUNDS.h > 0.0 && SIZE.x > 0.0 && SIZE.y > 0.0)) {
            record.extentsDevice = CBox{record.sourceDevice.x, record.sourceDevice.y, 1.0, 1.0};
            return;
        }
        const double SCALE_X = record.sourceDevice.w / SIZE.x;
        const double SCALE_Y = record.sourceDevice.h / SIZE.y;
        record.extentsDevice = CBox{record.sourceDevice.x + (BOUNDS.x * SCALE_X),
            record.sourceDevice.y + (BOUNDS.y * SCALE_Y),
            BOUNDS.w * SCALE_X,
            BOUNDS.h * SCALE_Y}
                                   .expand(2.0);
    }
    inline void render(SSwarmStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CSolidFill> &,
        const SP<CPaperShade> &,
        const SP<CSwarmSprites> &sprites) {
        const auto *sheet = sprites ? sprites->sheet(style.creature) : nullptr;
        const auto ATLAS =
            (sheet != nullptr) ? sprites->texture(style.creature) : SP<Render::ITexture>{};
        if ((sheet == nullptr) || !ATLAS || record.sourceAlpha <= 0.F) {
            return;
        }
        fillSwarmMeshes(style.swarm,
            *sheet,
            record.sourceDevice,
            record.extentsDevice,
            hyprimate::Vec2{.x = 0.0, .y = 0.0},
            hyprimate::Vec2{.x = record.snapshotSize.x, .y = record.snapshotSize.y},
            style.meshes);
        CSwarmClosePassElement::SData data;
        data.tex = tex;
        data.atlas = ATLAS;
        data.sprites = sprites;
        data.layerSize = swarmLayerSize(record.sourceDevice);
        data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        data.meshes = &style.meshes;
        data.alpha = record.sourceAlpha;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CSwarmClosePassElement>(std::move(data)));
    }
}  // namespace close_style
