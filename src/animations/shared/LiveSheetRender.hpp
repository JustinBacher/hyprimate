#pragma once

#include <hyprland/src/render/Renderer.hpp>

#include "LiveRender.hpp"

inline CBox live_sheet_extents(const SSheetBuffers &sheet, const CBox &box) {
    CBox extents = sheet_extents(*sheet.frame, box);
    extents.expand(4.0);
    return extents;
}

inline Render::SWindowTransformBuffer render_live_sheet(SSheetBuffers &sheet,
    double opacity,
    bool shaded,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const SLiveRenderGeometry &geometry,
    const SP<CPaperShade> &shade) {
    fill_sheet_meshes(*sheet.frame,
        geometry.source,
        geometry.output,
        {.x = input.box.x, .y = input.box.y},
        geometry.texture_size,
        sheet.window,
        sheet.shade);
    const auto LAYER = render_paper(ctx,
        SPaperDraw{
            .window = input.framebuffer->getTexture(),
            .shade = shaded && shade ? shade->texture() : SP<Render::ITexture>{},
            .canvas = geometry.canvas.size(),
            .mesh_box = geometry.local_output,
            .window_vertices = &sheet.window,
            .shade_vertices = &sheet.shade,
            .alpha = static_cast<float>(opacity),
        });
    if (!LAYER) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    return {.framebuffer = LAYER, .box = geometry.canvas};
}
