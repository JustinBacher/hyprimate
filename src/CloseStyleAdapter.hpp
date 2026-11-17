#pragma once

#include "AnimationCloseRenderer.hpp"

namespace close_style {
    using Record = CAnimationCloseRenderer::SClosing;
    using Pending = CAnimationCloseRenderer::SPending;

    inline CBox project(const CBox &extents, const CBox &box, const Vector2D &size) {
        return toCBox(
            hyprimate::projectToOutput(toBox(extents), toBox(box), {.x = size.x, .y = size.y}));
    }

}  // namespace close_style
