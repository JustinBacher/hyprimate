#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "window/bubble/Bubble.hpp"

namespace hyprimate::detail {
    struct BurstEvent {
        Vec3 position{};
        double time = 0;
        std::uint32_t id = 0;
    };
    struct CollapseCache {
        BubbleSurface key{};
        Vec3 position{};
        double time = 0;
        std::array<BurstEvent, K_BUBBLE_MAX_MERGES> merges{};
        std::size_t merge_count = 0;
        bool valid = false;
    };

    const CollapseCache &collapse(const BubbleSurface &surface);
}  // namespace hyprimate::detail
