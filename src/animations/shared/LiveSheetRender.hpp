#pragma once

#include <hyprland/src/render/Renderer.hpp>

#include "LiveRender.hpp"

inline CBox liveSheetExtents(const SSheetBuffers &sheet, const CBox &box) {
    CBox extents = sheetExtents(*sheet.frame, box);
    extents.expand(4.0);
    return extents;
}

inline Render::SWindowTransformBuffer renderLiveSheet(SSheetBuffers &sheet,
    double opacity,
    bool shaded,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const SLiveRenderGeometry &g,
    const SP<CPaperShade> &shade) {
    fillSheetMeshes(*sheet.frame,
        g.source,
        g.output,
        {.x = in.box.x, .y = in.box.y},
        g.textureSize,
        sheet.window,
        sheet.shade);
    const auto LAYER = renderPaper(ctx,
        SPaperDraw{
            .window = in.framebuffer->getTexture(),
            .shade = shaded && shade ? shade->texture() : SP<Render::ITexture>{},
            .canvas = g.canvas.size(),
            .meshBox = g.localOutput,
            .windowVertices = &sheet.window,
            .shadeVertices = &sheet.shade,
            .alpha = static_cast<float>(opacity),
        });
    if (!LAYER) {
        return {.framebuffer = in.framebuffer, .box = in.box, .success = false};
    }
    return {.framebuffer = LAYER, .box = g.canvas};
}
