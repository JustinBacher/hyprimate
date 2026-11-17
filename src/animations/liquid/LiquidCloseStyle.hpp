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
            const CBox liquid =
                project(toCBox(data.liquidDevice), record.renderBox, record.snapshotSize);
            const double x0 = std::min(device.x, liquid.x);
            const double y0 = std::min(device.y, liquid.y);
            const double x1 = std::max(device.x + device.w, liquid.x + liquid.w);
            const double y1 = std::max(device.y + device.h, liquid.y + liquid.h);
            device = CBox{x0, y0, x1 - x0, y1 - y0};
        }
        return device;
    }
    inline bool valid(const SLiquidStyle &, const Pending &, bool) {
        return true;
    }
    inline void begin(SLiquidStyle &style, const Pending &pending, bool blur) {
        const auto &data = std::get<SLiquidCloseData>(pending.closeData);
        if (!blur)
            style.transition.begin(hyprimate::Transition::Close,
                data.frame.value_or(style.transition.frame()),
                data.params,
                data.strength,
                data.splash);
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
        if (owned)
            hyprimate::advanceLiquidPose(style.mesh,
                style.transition,
                style.grid,
                elapsed,
                record.captureLogical,
                data.strength,
                0.0);
        else if (!style.mesh.settled()) {
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
        hyprimate::LiquidPose pose{style.mesh, style.transition, data.compensation};
        const CBox logical = CBox{record.sourceDevice}.scale(1.0 / record.scale);
        const auto logicalBox = toBox(logical);
        pose.displacementGain =
            hyprimate::fitLiquidPoseGain(pose, {logical.w, logical.h}, &style.grid);
        auto extents = hyprimate::liquidRenderExtents(pose, logicalBox, style.grid);
        hyprimate::expandBox(extents, 16.0);
        record.extentsDevice = toCBox(extents).scale(record.scale);
        const auto meniscus = hyprimate::liquidMeniscusExtents(pose, logicalBox);
        data.hasLiquid = meniscus.has_value();
        if (meniscus) data.liquidDevice = toBox(toCBox(*meniscus).scale(record.scale));
        hyprimate::fillLiquidRenderMesh(pose,
            toBox(record.sourceDevice),
            record.scale,
            toBox(record.extentsDevice),
            Vec2{0.0, 0.0},
            Vec2{record.snapshotSize.x, record.snapshotSize.y},
            style.grid,
            style.vertices);
        if (meniscus)
            hyprimate::fillLiquidMeniscusMesh(pose,
                toBox(record.sourceDevice),
                record.scale,
                data.liquidDevice,
                style.meniscusVertices);
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
        auto &close = std::get<SLiquidCloseData>(record.closeData);
        if (close.verticesStale) fill(style, record);
        CLiquidClosePassElement::SData data;
        data.tex = tex;
        data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        data.vertices = &style.vertices;
        data.alpha = record.sourceAlpha;
        data.scale = record.scale;
        CBox clip{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        const CBox restLogical = CBox{record.renderBox.x + record.sourceDevice.x,
            record.renderBox.y + record.sourceDevice.y,
            record.sourceDevice.w,
            record.sourceDevice.h}
                                     .scale(1.0 / record.scale);
        const hyprimate::LiquidPose pose{style.mesh, style.transition, close.compensation};
        if (const auto visible = hyprimate::liquidVisibleBox(pose, toBox(restLogical)))
            clip = clip.intersection(toCBox(*visible).scale(record.scale));
        data.damage = CRegion{clip};
        data.solid = solid ? solid->texture() : nullptr;
        data.liquid = close.hasLiquid && data.solid ? &style.meniscusVertices : nullptr;
        data.liquidBox = project(toCBox(close.liquidDevice), record.renderBox, record.snapshotSize);
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
    }
    inline hyprimate::LiquidPose closePose(const SLiquidStyle &state, const PHLWINDOW &window) {
        const hyprimate::LiquidPose unit{
            state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, state.displacementGain, false};
        if (!window || !state.boxDriven) return unit;
        const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox goal = state.goalBody;
        if (!(current.w > 0.0 && current.h > 0.0 && goal.w > 0.0 && goal.h > 0.0)) return unit;
        constexpr double maxScale = 64.0;
        return {state.mesh,
            state.transition,
            {std::clamp(goal.w / current.w, 1.0 / maxScale, maxScale),
                std::clamp(goal.h / current.h, 1.0 / maxScale, maxScale)},
            {(goal.x + 0.5 * goal.w) - (current.x + 0.5 * current.w),
                (goal.y + 0.5 * goal.h) - (current.y + 0.5 * current.h)},
            state.displacementGain,
            false};
    }
    inline bool stage(SLiquidStyle &style,
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
        if (!std::isfinite(close.strength) || close.strength <= 0.0) return false;
        const bool continuing = style.transition.active();
        const auto frame = continuing ? std::optional<hyprimate::EdgeFrame>{}
                                      : hyprimate::resolveStyleCloseEdge(edgeInput(), close);
        SLiquidCloseData data;
        if (active) {
            const auto pose = closePose(style, window);
            pending.style = SLiquidStyle{.mesh = pose.mesh, .transition = pose.transition};
            data.compensation = pose.compensation;
        } else {
            pending.style = SLiquidStyle{.mesh = hyprimate::LiquidMesh{material}};
        }
        data.strength = close.strength;
        data.frame = frame;
        data.splash = close.splash;
        data.params = {.elasticity = close.elasticity, .momentum = close.momentum};
        pending.closeData = std::move(data);
        return true;
    }
    inline bool renderNative(SLiquidStyle &style,
        Record &record,
        Render::CRenderContext &ctx,
        const SP<Desktop::IFadeout> &fadeout,
        const SP<Render::ITexture> &tex,
        CRegion damage) {
        if (std::get<SLiquidCloseData>(record.closeData).verticesStale) fill(style, record);
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
