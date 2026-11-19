#pragma once

#include <algorithm>
#include <cmath>

#include <hyprland/src/desktop/view/window/Window.hpp>

#include "../../CloseStyleAdapter.hpp"
#include "LiquidClosePass.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"

namespace close_style {
    inline bool close_enabled(
        [[maybe_unused]] const SLiquidCloseData &data, const hyprimate::config::SConfig &config) {
        return !(config.close.liquid.strength <= 0.0);
    }
    inline CBox footprint([[maybe_unused]] const SLiquidStyle &style, const Record &record) {
        CBox device = project(record.extents_device, record.render_box, record.snapshot_size);
        const auto &data = std::get<SLiquidCloseData>(record.close_data);
        if (data.has_liquid) {
            const CBox LIQUID =
                project(to_cbox(data.liquid_device), record.render_box, record.snapshot_size);
            const double MIN_X = std::min(device.x, LIQUID.x);
            const double MIN_Y = std::min(device.y, LIQUID.y);
            const double MAX_X = std::max(device.x + device.w, LIQUID.x + LIQUID.w);
            const double MAX_Y = std::max(device.y + device.h, LIQUID.y + LIQUID.h);
            device = CBox{MIN_X, MIN_Y, MAX_X - MIN_X, MAX_Y - MIN_Y};
        }
        return device;
    }
    inline bool valid([[maybe_unused]] const SLiquidStyle &style,
        [[maybe_unused]] const Pending &pending,
        [[maybe_unused]] bool owned) {
        return true;
    }
    inline void begin(SLiquidStyle &style, const Pending &pending, bool blur) {
        const auto &data = std::get<SLiquidCloseData>(pending.close_data);
        if (!blur) {
            style.transition.begin(hyprimate::Transition::Close,
                data.frame.value_or(style.transition.frame()),
                data.params,
                data.strength,
                data.splash);
        }
    }
    inline bool running(const SLiquidStyle &style) {
        return style.transition.active();
    }
    inline bool finished(const SLiquidStyle &style) {
        return style.transition.closed();
    }
    inline bool needs_owned([[maybe_unused]] const SLiquidStyle &style) {
        return false;
    }
    inline void advance(SLiquidStyle &style, Record &record, double elapsed, bool owned) {
        auto &data = std::get<SLiquidCloseData>(record.close_data);
        if (owned) {
            hyprimate::advance_liquid_pose(style.mesh,
                style.transition,
                style.grid,
                elapsed,
                record.capture_logical,
                hyprimate::LiquidStrength{data.strength},
                hyprimate::LiquidDrive{0.0});
        } else if (!style.mesh.settled()) {
            style.mesh.advance(elapsed, record.capture_logical);
            data.vertices_stale = true;
        }
    }
    inline bool settled(const SLiquidStyle &style) {
        return style.mesh.settled();
    }
    inline void fill(SLiquidStyle &style, Record &record) {
        using hyprimate::Vec2;
        auto &data = std::get<SLiquidCloseData>(record.close_data);
        hyprimate::LiquidPose pose{
            .mesh = style.mesh,
            .transition = style.transition,
            .compensation = data.compensation,
        };
        const CBox LOGICAL = CBox{record.source_device}.scale(1.0 / record.scale);
        const auto LOGICAL_BOX = to_box(LOGICAL);
        pose.displacement_gain =
            hyprimate::fit_liquid_pose_gain(pose, {.x = LOGICAL.w, .y = LOGICAL.h}, &style.grid);
        auto extents = hyprimate::liquid_render_extents(pose, LOGICAL_BOX, style.grid);
        hyprimate::expand_box(extents, 16.0);
        record.extents_device = to_cbox(extents).scale(record.scale);
        const auto MENISCUS = hyprimate::liquid_meniscus_extents(pose, LOGICAL_BOX);
        data.has_liquid = MENISCUS.has_value();
        if (MENISCUS) {
            data.liquid_device = to_box(to_cbox(*MENISCUS).scale(record.scale));
        }
        hyprimate::fill_liquid_render_mesh(pose,
            to_box(record.source_device),
            record.scale,
            to_box(record.extents_device),
            Vec2{.x = 0.0, .y = 0.0},
            Vec2{.x = record.snapshot_size.x, .y = record.snapshot_size.y},
            style.grid,
            style.vertices);
        if (MENISCUS) {
            hyprimate::fill_liquid_meniscus_mesh(pose,
                to_box(record.source_device),
                record.scale,
                data.liquid_device,
                style.meniscus_vertices);
        }
        data.vertices_stale = false;
    }
    inline void render(SLiquidStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CSolidFill> &solid,
        [[maybe_unused]] const SP<CSheetShade> &shade,
        [[maybe_unused]] const SP<CSwarmSprites> &sprites) {
        const auto &close = std::get<SLiquidCloseData>(record.close_data);
        if (close.vertices_stale) {
            fill(style, record);
        }
        CLiquidClosePassElement::SData data;
        data.tex = tex;
        data.box = project(record.extents_device, record.render_box, record.snapshot_size);
        data.vertices = &style.vertices;
        data.alpha = record.source_alpha;
        data.scale = record.scale;
        CBox clip{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        const CBox REST_LOGICAL = CBox{record.render_box.x + record.source_device.x,
            record.render_box.y + record.source_device.y,
            record.source_device.w,
            record.source_device.h}
                                      .scale(1.0 / record.scale);
        const hyprimate::LiquidPose POSE{
            .mesh = style.mesh,
            .transition = style.transition,
            .compensation = close.compensation,
        };
        if (const auto VISIBLE = hyprimate::liquid_visible_box(POSE, to_box(REST_LOGICAL))) {
            clip = clip.intersection(to_cbox(*VISIBLE).scale(record.scale));
        }
        data.damage = CRegion{clip};
        data.solid = solid ? solid->texture() : nullptr;
        data.liquid = close.has_liquid && data.solid ? &style.meniscus_vertices : nullptr;
        data.liquid_box =
            project(to_cbox(close.liquid_device), record.render_box, record.snapshot_size);
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
    }
    inline hyprimate::LiquidPose close_pose(const SLiquidStyle &state, const PHLWINDOW &window) {
        const hyprimate::LiquidPose UNIT{
            .mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .open_offset = {.x = 0.0, .y = 0.0},
            .displacement_gain = state.displacement_gain,
            .box_pose = false,
        };
        if (!window || !state.box_driven) {
            return UNIT;
        }
        const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox GOAL = state.goal_body;
        if (!(CURRENT.w > 0.0) || !(CURRENT.h > 0.0) || !(GOAL.w > 0.0) || !(GOAL.h > 0.0)) {
            return UNIT;
        }
        constexpr double MAX_SCALE = 64.0;
        return {
            .mesh = state.mesh,
            .transition = state.transition,
            .compensation =
                {
                    .x = std::clamp(GOAL.w / CURRENT.w, 1.0 / MAX_SCALE, MAX_SCALE),
                    .y = std::clamp(GOAL.h / CURRENT.h, 1.0 / MAX_SCALE, MAX_SCALE),
                },
            .open_offset =
                {
                    .x = (GOAL.x + (0.5 * GOAL.w)) - (CURRENT.x + (0.5 * CURRENT.w)),
                    .y = (GOAL.y + (0.5 * GOAL.h)) - (CURRENT.y + (0.5 * CURRENT.h)),
                },
            .displacement_gain = state.displacement_gain,
            .box_pose = false,
        };
    }
    inline bool stage(const SLiquidStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        bool active,
        const hyprimate::config::SConfig &config,
        const std::function<hyprimate::EdgeInput()> &edge_input,
        [[maybe_unused]] const std::function<std::span<const hyprimate::Box>()> &surfaces,
        [[maybe_unused]] const std::function<std::uint64_t()> &seed) {
        const auto &close = config.close.liquid;
        if (!std::isfinite(close.strength) || close.strength <= 0.0) {
            return false;
        }
        const bool CONTINUING = style.transition.active();
        const auto FRAME = CONTINUING ? std::optional<hyprimate::EdgeFrame>{}
                                      : hyprimate::resolve_style_close_edge(edge_input(), close);
        SLiquidCloseData data;
        if (active) {
            const auto POSE = close_pose(style, window);
            pending.style =
                SLiquidStyle{.mesh = POSE.mesh.get(), .transition = POSE.transition.get()};
            data.compensation = POSE.compensation;
        } else {
            pending.style = SLiquidStyle{.mesh = hyprimate::LiquidMesh{liquid_inputs(config)}};
        }
        data.strength = close.strength;
        data.frame = FRAME;
        data.splash = close.splash;
        data.params = {.elasticity = close.elasticity, .momentum = close.momentum};
        pending.close_data = data;
        return true;
    }
    inline bool render_native(SLiquidStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const SP<Desktop::IFadeout> &fadeout,
        const SP<Render::ITexture> &tex,
        CRegion damage) {
        if (std::get<SLiquidCloseData>(record.close_data).vertices_stale) {
            fill(style, record);
        }
        CLiquidClosePassElement::SData data;
        data.tex = tex;
        data.box = project(record.extents_device, fadeout->renderBox(), record.snapshot_size);
        data.vertices = &style.vertices;
        data.alpha = fadeout->alpha();
        data.scale = record.scale;
        data.damage = std::move(damage);
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
        return true;
    }
}  // namespace close_style
