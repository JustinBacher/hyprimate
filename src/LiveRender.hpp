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

hyprimate::LiquidPose live_pose(const SLiquidStyle &state);
CBox live_extents(const SLiquidStyle &state, const CBox &box);
CBox live_extents(const SPaperStyle &state, const CBox &box);
CBox live_extents(const SClothStyle &state, const CBox &box);
CBox live_extents(const SSwarmStyle &state, const CBox &box);
CBox live_extents(const SBubbleStyle &state, const CBox &box);

Render::SWindowTransformBuffer render_live(SLiquidStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer render_live(SPaperStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer render_live(SClothStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer render_live(SSwarmStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer render_live(SBubbleStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
