#pragma once

#include <algorithm>
#include <cmath>

#include <hyprland/src/desktop/view/window/Window.hpp>

#include "../../CloseStyleAdapter.hpp"
#include "LiquidClosePass.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"

namespace close_style {
    inline bool closeEnabled(const SLiquidCloseData &, const hyprimate::config::SConfig &config) {
        return !(config.close.liquid.strength <= 0.0);
    }
    inline CBox footprint(const SLiquidStyle &, const Record &record) {
        CBox device = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        const auto &data = std::get<SLiquidCloseData>(record.closeData);
        if (data.hasLiquid) {
            const CBox LIQUID =
                project(toCBox(data.liquidDevice), record.renderBox, record.snapshotSize);
            const double MIN_X = std::min(device.x, LIQUID.x);
            const double MIN_Y = std::min(device.y, LIQUID.y);
            const double MAX_X = std::max(device.x + device.w, LIQUID.x + LIQUID.w);
            const double MAX_Y = std::max(device.y + device.h, LIQUID.y + LIQUID.h);
            device = CBox{MIN_X, MIN_Y, MAX_X - MIN_X, MAX_Y - MIN_Y};
        }
        return device;
    }
    inline bool valid(const SLiquidStyle &, const Pending &, bool) {
        return true;
    }
    inline void begin(SLiquidStyle &style, const Pending &pending, bool blur) {
        const auto &data = std::get<SLiquidCloseData>(pending.closeData);
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
    inline bool needsOwned(const SLiquidStyle &) {
        return false;
    }
    inline void advance(SLiquidStyle &style, Record &record, double elapsed, bool owned) {
        auto &data = std::get<SLiquidCloseData>(record.closeData);
        if (owned) {
            hyprimate::advanceLiquidPose(style.mesh,
                style.transition,
                style.grid,
                elapsed,
                record.captureLogical,
                data.strength,
                0.0);
        } else if (!style.mesh.settled()) {
            style.mesh.advance(elapsed, record.captureLogical);
            data.verticesStale = true;
        }
    }
    inline bool settled(const SLiquidStyle &style) {
        return style.mesh.settled();
    }
    inline void fill(SLiquidStyle &style, Record &record) {
        using hyprimate::Vec2;
        auto &data = std::get<SLiquidCloseData>(record.closeData);
        hyprimate::LiquidPose pose{
            .mesh = style.mesh, .transition = style.transition, .compensation = data.compensation};
        const CBox LOGICAL = CBox{record.sourceDevice}.scale(1.0 / record.scale);
        const auto LOGICAL_BOX = toBox(LOGICAL);
        pose.displacementGain =
            hyprimate::fitLiquidPoseGain(pose, {.x = LOGICAL.w, .y = LOGICAL.h}, &style.grid);
        auto extents = hyprimate::liquidRenderExtents(pose, LOGICAL_BOX, style.grid);
        hyprimate::expandBox(extents, 16.0);
        record.extentsDevice = toCBox(extents).scale(record.scale);
        const auto MENISCUS = hyprimate::liquidMeniscusExtents(pose, LOGICAL_BOX);
        data.hasLiquid = MENISCUS.has_value();
        if (MENISCUS) {
            data.liquidDevice = toBox(toCBox(*MENISCUS).scale(record.scale));
        }
        hyprimate::fillLiquidRenderMesh(pose,
            toBox(record.sourceDevice),
            record.scale,
            toBox(record.extentsDevice),
            Vec2{.x = 0.0, .y = 0.0},
            Vec2{.x = record.snapshotSize.x, .y = record.snapshotSize.y},
            style.grid,
            style.vertices);
        if (MENISCUS) {
            hyprimate::fillLiquidMeniscusMesh(pose,
                toBox(record.sourceDevice),
                record.scale,
                data.liquidDevice,
                style.meniscusVertices);
        }
        data.verticesStale = false;
    }
    inline void render(SLiquidStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CSolidFill> &solid,
        const SP<CPaperShade> &,
        const SP<CSwarmSprites> &) {
        const auto &close = std::get<SLiquidCloseData>(record.closeData);
        if (close.verticesStale) {
            fill(style, record);
        }
        CLiquidClosePassElement::SData data;
        data.tex = tex;
        data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        data.vertices = &style.vertices;
        data.alpha = record.sourceAlpha;
        data.scale = record.scale;
        CBox clip{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        const CBox REST_LOGICAL = CBox{record.renderBox.x + record.sourceDevice.x,
            record.renderBox.y + record.sourceDevice.y,
            record.sourceDevice.w,
            record.sourceDevice.h}
                                      .scale(1.0 / record.scale);
        const hyprimate::LiquidPose POSE{
            .mesh = style.mesh, .transition = style.transition, .compensation = close.compensation};
        if (const auto VISIBLE = hyprimate::liquidVisibleBox(POSE, toBox(REST_LOGICAL))) {
            clip = clip.intersection(toCBox(*VISIBLE).scale(record.scale));
        }
        data.damage = CRegion{clip};
        data.solid = solid ? solid->texture() : nullptr;
        data.liquid = close.hasLiquid && data.solid ? &style.meniscusVertices : nullptr;
        data.liquidBox = project(toCBox(close.liquidDevice), record.renderBox, record.snapshotSize);
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
    }
    inline hyprimate::LiquidPose closePose(const SLiquidStyle &state, const PHLWINDOW &window) {
        const hyprimate::LiquidPose UNIT{.mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .openOffset = {.x = 0.0, .y = 0.0},
            .displacementGain = state.displacementGain,
            .boxPose = false};
        if (!window || !state.boxDriven) {
            return UNIT;
        }
        const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox GOAL = state.goalBody;
        if (!(CURRENT.w > 0.0 && CURRENT.h > 0.0 && GOAL.w > 0.0 && GOAL.h > 0.0)) {
            return UNIT;
        }
        constexpr double MAX_SCALE = 64.0;
        return {.mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = std::clamp(GOAL.w / CURRENT.w, 1.0 / MAX_SCALE, MAX_SCALE),
                .y = std::clamp(GOAL.h / CURRENT.h, 1.0 / MAX_SCALE, MAX_SCALE)},
            .openOffset = {.x = (GOAL.x + (0.5 * GOAL.w)) - (CURRENT.x + (0.5 * CURRENT.w)),
                .y = (GOAL.y + (0.5 * GOAL.h)) - (CURRENT.y + (0.5 * CURRENT.h))},
            .displacementGain = state.displacementGain,
            .boxPose = false};
    }
    inline bool stage(const SLiquidStyle &style,
        Pending &pending,
        const PHLWINDOW &window,
        bool active,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams material,
        const std::function<hyprimate::EdgeInput()> &edgeInput,
        const std::function<std::span<const hyprimate::Box>()> &,
        const std::function<std::uint64_t()> &,
        const std::function<hyprimate::EdgeSet()> &) {
        const auto &close = config.close.liquid;
        if (!std::isfinite(close.strength) || close.strength <= 0.0) {
            return false;
        }
        const bool CONTINUING = style.transition.active();
        const auto FRAME = CONTINUING ? std::optional<hyprimate::EdgeFrame>{}
                                      : hyprimate::resolveStyleCloseEdge(edgeInput(), close);
        SLiquidCloseData data;
        if (active) {
            const auto POSE = closePose(style, window);
            pending.style = SLiquidStyle{.mesh = POSE.mesh, .transition = POSE.transition};
            data.compensation = POSE.compensation;
        } else {
            pending.style = SLiquidStyle{.mesh = hyprimate::LiquidMesh{material}};
        }
        data.strength = close.strength;
        data.frame = FRAME;
        data.splash = close.splash;
        data.params = {.elasticity = close.elasticity, .momentum = close.momentum};
        pending.closeData = data;
        return true;
    }
    inline bool renderNative(SLiquidStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const SP<Desktop::IFadeout> &fadeout,
        const SP<Render::ITexture> &tex,
        CRegion damage) {
        if (std::get<SLiquidCloseData>(record.closeData).verticesStale) {
            fill(style, record);
        }
        CLiquidClosePassElement::SData data;
        data.tex = tex;
        data.box = project(record.extentsDevice, fadeout->renderBox(), record.snapshotSize);
        data.vertices = &style.vertices;
        data.alpha = fadeout->alpha();
        data.scale = record.scale;
        data.damage = std::move(damage);
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
        return true;
    }
}  // namespace close_style
