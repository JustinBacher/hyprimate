#pragma once

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "../../CloseStyleAdapter.hpp"
#include "SwarmConfig.hpp"
#include "SwarmRenderer.hpp"

namespace close_style {
    inline bool close_enabled([[maybe_unused]] const SSwarmCloseData &data,
        [[maybe_unused]] const hyprimate::config::SConfig &config) {
        return true;
    }
    inline CBox footprint([[maybe_unused]] const SSwarmStyle &style, const Record &record) {
        return project(record.extents_device, record.render_box, record.snapshot_size);
    }
    inline bool stage(SSwarmStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        [[maybe_unused]] bool state_active,
        const hyprimate::config::SConfig &config,
        const std::function<hyprimate::EdgeInput()> &edge_input,
        [[maybe_unused]] const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed) {
        const auto MONITOR = window->m_monitor.lock();
        const auto SIDES = swarm_sides(edge_input(), config.close.swarm.loneWindowSides);
        const auto SEED = next_seed();
        if (!MONITOR || SIDES.empty()) {
            return false;
        }
        pending.close_data = SSwarmCloseData{
            .params = swarm_params(config.close.swarm),
            .seed = SEED,
            .placement =
                {
                    .capture = to_box(window->getFullWindowBoundingBox()),
                    .body =
                        to_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
                    .screen = to_box(MONITOR->logicalBox()),
                },
            .sides = SIDES,
            .creature = swarm_creature(config.close.swarm),
        };
        pending.style = std::move(style);
        return true;
    }
    inline bool valid([[maybe_unused]] const SSwarmStyle &style,
        [[maybe_unused]] const Pending &pending,
        bool blur) {
        return !blur;
    }
    inline void begin(SSwarmStyle &style, const Pending &pending, [[maybe_unused]] bool blur) {
        const auto &data = std::get<SSwarmCloseData>(pending.close_data);
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
    inline bool needs_owned([[maybe_unused]] const SSwarmStyle &style) {
        return true;
    }
    inline void advance(SSwarmStyle &style,
        [[maybe_unused]] const Record &record,
        double elapsed,
        [[maybe_unused]] bool native) {
        style.swarm.advance(elapsed);
    }
    inline bool settled([[maybe_unused]] const SSwarmStyle &style) {
        return true;
    }
    inline bool render_native([[maybe_unused]] const SSwarmStyle &style,
        [[maybe_unused]] const Record &record,
        [[maybe_unused]] const Render::CRenderContext &ctx,
        [[maybe_unused]] const SP<Desktop::IFadeout> &fadeout,
        [[maybe_unused]] const SP<Render::ITexture> &tex,
        [[maybe_unused]] const CRegion &damage) {
        return false;
    }
    // Extents only: the meshes need the creature's sheet, which the renderer supplies when it
    // draws.
    inline void fill(const SSwarmStyle &style, Record &record) {
        const hyprimate::Box BOUNDS = style.swarm.bounds();
        const hyprimate::Vec2 SIZE = style.swarm.size();
        if (!(BOUNDS.w > 0.0) || !(BOUNDS.h > 0.0) || !(SIZE.x > 0.0) || !(SIZE.y > 0.0)) {
            record.extents_device = CBox{record.source_device.x, record.source_device.y, 1.0, 1.0};
            return;
        }
        const double SCALE_X = record.source_device.w / SIZE.x;
        const double SCALE_Y = record.source_device.h / SIZE.y;
        record.extents_device = CBox{record.source_device.x + (BOUNDS.x * SCALE_X),
            record.source_device.y + (BOUNDS.y * SCALE_Y),
            BOUNDS.w * SCALE_X,
            BOUNDS.h * SCALE_Y}
                                    .expand(2.0);
    }
    inline void render(SSwarmStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        [[maybe_unused]] const SP<CSolidFill> &solid_fill,
        [[maybe_unused]] const SP<CSheetShade> &shade,
        const SP<CSwarmSprites> &sprites) {
        const auto *sheet = sprites ? sprites->sheet(style.creature) : nullptr;
        const auto ATLAS =
            (sheet != nullptr) ? sprites->texture(style.creature) : SP<Render::ITexture>{};
        if ((sheet == nullptr) || !ATLAS || record.source_alpha <= 0.F) {
            return;
        }
        fill_swarm_meshes(style.swarm,
            *sheet,
            record.source_device,
            record.extents_device,
            hyprimate::Vec2{.x = 0.0, .y = 0.0},
            hyprimate::Vec2{.x = record.snapshot_size.x, .y = record.snapshot_size.y},
            style.meshes);
        CSwarmClosePassElement::SData data;
        data.tex = tex;
        data.atlas = ATLAS;
        data.sprites = sprites;
        data.layer_size = swarm_layer_size(record.source_device);
        data.box = project(record.extents_device, record.render_box, record.snapshot_size);
        data.meshes = &style.meshes;
        data.alpha = record.source_alpha;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CSwarmClosePassElement>(std::move(data)));
    }
}  // namespace close_style
