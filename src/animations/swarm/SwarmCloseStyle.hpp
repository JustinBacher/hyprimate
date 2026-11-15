#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "SwarmConfig.hpp"
#include "SwarmRenderer.hpp"
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>

namespace close_style {
inline bool closeEnabled(const SSwarmCloseData &, const liquid_glass::config::SConfig &) { return true; }
inline CBox footprint(const SSwarmStyle &, const Record &record) {
    return project(record.extentsDevice, record.renderBox, record.snapshotSize);
}
inline bool stage(SSwarmStyle &style, Pending &pending, const PHLWINDOW &window, bool,
                  const liquid_glass::config::SConfig &config, liquid_glass::MaterialParams,
                  const std::function<std::optional<liquid_glass::EdgeFrame>()> &,
                  const std::function<std::span<const liquid_glass::Box>()> &,
                  const std::function<std::uint64_t()>         &nextSeed,
                  const std::function<liquid_glass::EdgeSet()> &swarmSides) {
    const auto monitor = window->m_monitor.lock();
    const auto sides = swarmSides();
    const auto seed = nextSeed();
    if (!monitor || sides.empty())
        return false;
    pending.closeData =
        SSwarmCloseData{.params = swarmParams(config),
                        .seed = seed,
                        .placement = {toBox(window->getFullWindowBoundingBox()),
                                      toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
                                      toBox(monitor->logicalBox())},
                        .sides = sides,
                        .creature = swarmCreature(config)};
    pending.style = std::move(style);
    return true;
}
inline bool valid(const SSwarmStyle &, const Pending &, bool blur) { return !blur; }
inline void begin(SSwarmStyle &style, const Pending &pending, bool) {
    const auto &data = std::get<SSwarmCloseData>(pending.closeData);
    // A swarm already in flight keeps the creature it is drawn as.
    if (!style.swarm.active())
        style.creature = data.creature;
    style.swarm.begin(liquid_glass::Transition::Close, data.params, data.seed, data.placement, data.sides);
}
inline bool running(const SSwarmStyle &style) { return style.swarm.active(); }
inline bool finished(const SSwarmStyle &style) { return style.swarm.closed(); }
inline bool needsOwned(const SSwarmStyle &) { return true; }
inline void advance(SSwarmStyle &style, Record &, double elapsed, bool) { style.swarm.advance(elapsed); }
inline bool settled(const SSwarmStyle &) { return true; }
inline bool renderNative(SSwarmStyle &, Record &, Render::CRenderContext &, const SP<Desktop::IFadeout> &,
                         const SP<Render::ITexture> &, CRegion) {
    return false;
}
// Extents only: the meshes need the creature's sheet, which the renderer supplies when it draws.
inline void fill(SSwarmStyle &style, Record &record) {
    const liquid_glass::Box  bounds = style.swarm.bounds();
    const liquid_glass::Vec2 size = style.swarm.size();
    if (!(bounds.w > 0.0 && bounds.h > 0.0 && size.x > 0.0 && size.y > 0.0)) {
        record.extentsDevice = CBox{record.sourceDevice.x, record.sourceDevice.y, 1.0, 1.0};
        return;
    }
    const double sx = record.sourceDevice.w / size.x, sy = record.sourceDevice.h / size.y;
    record.extentsDevice =
        CBox{record.sourceDevice.x + bounds.x * sx, record.sourceDevice.y + bounds.y * sy, bounds.w * sx, bounds.h * sy}
            .expand(2.0);
}
inline void render(SSwarmStyle &style, Record &record, Render::CRenderContext &ctx, const PHLMONITOR &monitor,
                   const SP<Render::ITexture> &tex, const SP<CSolidFill> &, const SP<CPaperShade> &,
                   const SP<CSwarmSprites>    &sprites) {
    const auto *sheet = sprites ? sprites->sheet(style.creature) : nullptr;
    const auto  atlas = sheet ? sprites->texture(style.creature) : SP<Render::ITexture>{};
    if (!sheet || !atlas || record.sourceAlpha <= 0.F)
        return;
    fillSwarmMeshes(style.swarm, *sheet, record.sourceDevice, record.extentsDevice, liquid_glass::Vec2{0.0, 0.0},
                    liquid_glass::Vec2{record.snapshotSize.x, record.snapshotSize.y}, style.meshes);
    CSwarmClosePassElement::SData data;
    data.tex = tex;
    data.atlas = atlas;
    data.layerSize = swarmLayerSize(record.sourceDevice);
    data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
    data.meshes = &style.meshes;
    data.alpha = record.sourceAlpha;
    data.scale = record.scale;
    data.damage =
        CRegion{CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(data.box)};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CSwarmClosePassElement>(std::move(data)));
}
} // namespace close_style
