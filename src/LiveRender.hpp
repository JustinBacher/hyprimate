#pragma once

#include "AnimationTransformer.hpp"

// Geometry shared by style renderers after the compositor has validated the input and output buffers.
struct SLiveRenderGeometry {
    CBox canvas;
    CBox source;
    CBox output;
    CBox localOutput;
    double scale;
    liquid_glass::Vec2 textureSize;
};

liquid_glass::LiquidPose livePose(const SLiquidStyle &state);
CBox liveExtents(const SLiquidStyle &state, const CBox &box);
CBox liveExtents(const SPaperStyle &state, const CBox &box);
CBox liveExtents(const SClothStyle &state, const CBox &box);
CBox liveExtents(const SSwarmStyle &state, const CBox &box);

Render::SWindowTransformBuffer renderLive(SLiquidStyle &state, Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in, const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry, const SP<CSolidFill> &solid, const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
Render::SWindowTransformBuffer renderLive(SPaperStyle &state, Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in, const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry, const SP<CSolidFill> &solid, const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
Render::SWindowTransformBuffer renderLive(SClothStyle &state, Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in, const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry, const SP<CSolidFill> &solid, const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
Render::SWindowTransformBuffer renderLive(SSwarmStyle &state, Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in, const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry, const SP<CSolidFill> &solid, const SP<CPaperShade> &shade,
    const SP<CSwarmSprites> &sprites);
