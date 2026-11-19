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
    CSheetShade &shade) {
    const auto MESH = hyprimate::fill_sheet_mesh(*sheet.frame,
        hyprimate::animation::MeshTarget<SMeshRenderVertex>{
            .source = to_box(geometry.source), .output = to_box(geometry.output),
            .image_origin = {.x = input.box.x, .y = input.box.y},
            .image_size = geometry.texture_size, .scale = geometry.scale, .buffers = sheet.buffers},
        shaded, static_cast<float>(opacity));
    const auto LAYER = render_sheet(ctx,
        SSheetDraw{
            .window = input.framebuffer->getTexture(),
            .shade = MESH.shade ? shade.texture() : SP<Render::ITexture>{},
            .canvas = geometry.canvas.size(),
            .mesh_box = geometry.local_output,
            .window_vertices = MESH.window,
            .shade_vertices = MESH.shade,
            .alpha = MESH.opacity,
        });
    if (!LAYER) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    return {.framebuffer = LAYER, .box = geometry.canvas};
}
