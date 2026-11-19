#pragma once

#include "AnimationTransformer.hpp"  // IWYU pragma: keep

// Geometry shared by style renderers after the compositor has validated the input and output
// buffers.
struct SLiveRenderGeometry {
    CBox canvas;
    CBox source;
    CBox output;
    CBox local_output;
    double scale = 0;
    hyprimate::Vec2 texture_size;
};

#define HYPRIMATE_RENDER_DECL(kind, tag, module)                                            \
    CBox live_extents(const module::State &state, const CBox &box);                         \
    Render::SWindowTransformBuffer render_live(module::State &state,                        \
        Render::CRenderContext &ctx, const Render::SWindowTransformBuffer &input,            \
        const Render::SWindowTransformContext &context, const SLiveRenderGeometry &geometry, \
        module::Resources &resources);
HYPRIMATE_STYLE_LIST(HYPRIMATE_RENDER_DECL, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_RENDER_DECL
