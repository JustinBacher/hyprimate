#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include <GLES3/gl32.h>

#include "shared/Core.hpp"
#include "window/bubble/BubbleFormation.hpp"
#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleWake.hpp"

namespace bubble_material_detail {
    struct Instance {
        std::array<float, 8> values;
    };

    // One small soap bubble: shape, neck, film (thickness, seed, flow, visibility), outline.
    struct MicroInstance {
        std::array<float, 16> values;
    };

    inline constexpr std::size_t K_MICRO_INSTANCES = std::max({hyprimate::K_BUBBLE_MICRO_CAPACITY,
        hyprimate::K_BUBBLE_WAKE_BUBBLES,
        hyprimate::kBubbleFormationCapacity});

    inline void vector(GLint location, hyprimate::Vec2 value) {
        glUniform2f(location, static_cast<float>(value.x), static_cast<float>(value.y));
    }
}  // namespace bubble_material_detail
