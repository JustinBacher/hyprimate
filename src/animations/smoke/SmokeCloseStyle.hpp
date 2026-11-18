#pragma once

#include <cmath>

#include "CloseStyleAdapter.hpp"
#include "SmokeLiveState.hpp"
#include "SmokeRoute.hpp"

namespace close_style {

    inline bool close_enabled([[maybe_unused]] const SSmokeCloseData &data,
        [[maybe_unused]] const hyprimate::config::SConfig &config) {
        return true;
    }

    inline CBox footprint([[maybe_unused]] const SSmokeStyle &style, const Record &record) {
        return project(record.extents_device, record.render_box, record.snapshot_size);
    }

    inline bool stage(SSmokeStyle &style,
        Pending &pending,
        [[maybe_unused]] const PHLWINDOW &window,
        bool state_active,
        const hyprimate::config::SConfig &config,
        [[maybe_unused]] hyprimate::MaterialParams material,
        const std::function<hyprimate::EdgeInput()> &edge_input,
        [[maybe_unused]] const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed,
        [[maybe_unused]] const std::function<hyprimate::EdgeSet()> &swarm_sides) {
        if (!style.renderer) {
            return false;
        }
        if (!state_active) {
            style.smoke.seed(next_seed());
        }
        pending.close_data = SSmokeCloseData{
            .duration = config.close.smoke.duration,
            .params = {.strength = config.movement.smoke.strength,
                .turbulence = config.close.smoke.turbulence,
                .trail = config.movement.smoke.trail,
                .reform = config.movement.smoke.reform,
                .mass = config.movement.smoke.mass,
                .roundness = config.movement.smoke.roundness,
                .density = config.smoke.density,
                .variation = config.smoke.variation},
            .route = hyprimate::smoke_route(config.close.smoke.direction, edge_input()),
        };
        pending.style = std::move(style);
        return true;
    }

    inline bool valid([[maybe_unused]] const SSmokeStyle &style,
        [[maybe_unused]] const Pending &pending,
        bool blur) {
        return !blur;
    }

    inline void begin(SSmokeStyle &style, const Pending &pending, [[maybe_unused]] bool blur) {
        const auto &data = std::get<SSmokeCloseData>(pending.close_data);
        style.smoke.begin(
            hyprimate::Transition::Close, data.duration, pending.capture_logical, data.route);
    }

    inline bool running(const SSmokeStyle &style) {
        return style.smoke.active();
    }

    inline bool finished(const SSmokeStyle &style) {
        return style.smoke.closed();
    }

    inline bool needs_owned([[maybe_unused]] const SSmokeStyle &style) {
        return true;
    }

    inline void advance(
        SSmokeStyle &style, Record &record, double elapsed, [[maybe_unused]] bool native) {
        const auto &data = std::get<SSmokeCloseData>(record.close_data);
        style.smoke.advance(elapsed, record.capture_logical, data.params);
        if (style.smoke.closed()) {
            record.snapshot.reset();
            style.field.reset();
        }
    }

    inline bool settled([[maybe_unused]] const SSmokeStyle &style) {
        return true;
    }

    inline bool render_native([[maybe_unused]] const SSmokeStyle &style,
        [[maybe_unused]] const Record &record,
        [[maybe_unused]] const Render::CRenderContext &ctx,
        [[maybe_unused]] const SP<Desktop::IFadeout> &fadeout,
        [[maybe_unused]] const SP<Render::ITexture> &tex,
        [[maybe_unused]] const CRegion &damage) {
        return false;
    }

    inline void fill(const SSmokeStyle &style, Record &record) {
        if (style.smoke.closed()) {
            record.extents_device = {};
            return;
        }
        const auto bounds = style.smoke.bounds(record.capture_logical);
        const double sx = record.source_device.w / record.capture_logical.x;
        const double sy = record.source_device.h / record.capture_logical.y;
        const auto raw = CBox{record.source_device.x + bounds.x * sx,
            record.source_device.y + bounds.y * sy,
            bounds.w * sx,
            bounds.h * sy}
                             .expand(2.0);
        const double left = std::floor(raw.x);
        const double top = std::floor(raw.y);
        record.extents_device =
            CBox{left, top, std::ceil(raw.x + raw.w) - left, std::ceil(raw.y + raw.h) - top};
    }

    inline void render(SSmokeStyle &style,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        [[maybe_unused]] const SP<CSolidFill> &solid,
        [[maybe_unused]] const SP<CPaperShade> &shade,
        [[maybe_unused]] const SP<CSwarmSprites> &sprites) {
        if (!style.renderer || record.extents_device.empty() || record.source_alpha <= 0.F) {
            return;
        }
        if (!style.field) {
            style.field = makeShared<CSmokeFieldHandle>();
        }
        CSmokePassElement::SData data;
        data.renderer = style.renderer;
        data.draw = SSmokeDraw{
            .window = tex,
            .field = style.field,
            .frame = style.smoke.frame(record.capture_logical),
            .canvas = record.extents_device.size(),
            .source = record.source_device,
            .texture_origin = {0, 0},
            .texture_size = {record.snapshot_size.x, record.snapshot_size.y},
            .output_origin = {record.extents_device.x, record.extents_device.y},
        };
        data.box = project(record.extents_device, record.render_box, record.snapshot_size);
        data.alpha = record.source_alpha;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CSmokePassElement>(std::move(data)));
    }

}  // namespace close_style
