#pragma once

#include "AnimationTransformer.hpp"  // IWYU pragma: keep

// Geometry shared by style renderers after the compositor has validated the input and output
// buffers.
struct SLiveRenderGeometry {
    CBox canvas;
    CBox source;
    CBox output;
    CBox localOutput;
    double scale;
    hyprimate::Vec2 textureSize;
};

hyprimate::LiquidPose livePose(const SLiquidStyle &state);
CBox liveExtents(const SLiquidStyle &state, const CBox &box);
CBox liveExtents(const SPaperStyle &state, const CBox &box);
CBox liveExtents(const SClothStyle &state, const CBox &box);
CBox liveExtents(const SSwarmStyle &state, const CBox &box);
CBox liveExtents(const SBubbleStyle &state, const CBox &box);

Render::SWindowTransformBuffer renderLive(SLiquidStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer renderLive(SPaperStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer renderLive(SClothStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer renderLive(SSwarmStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);

Render::SWindowTransformBuffer renderLive(SBubbleStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    const SP<CSolidFill> &solid,
    const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
