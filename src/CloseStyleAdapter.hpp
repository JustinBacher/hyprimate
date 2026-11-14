#pragma once

#include "AnimationCloseRenderer.hpp"

namespace close_style {
using Record = CAnimationCloseRenderer::SClosing;
using Pending = CAnimationCloseRenderer::SPending;

inline CBox project(const CBox &extents, const CBox &box, const Vector2D &size) {
    return toCBox(liquid_glass::projectToOutput(toBox(extents), toBox(box), {size.x, size.y}));
}

}
