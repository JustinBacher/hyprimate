#pragma once

#include <cstdint>

#include "window/bubble/Bubble.hpp"

namespace hyprimate::bubble_frame_detail {
    struct RuptureDuration {
        double seconds;
    };
    void start_rupture(BubbleSurface &surface,
        Vec2 spot,
        std::uint64_t seed,
        RuptureDuration duration,
        bool sphere);
    void update_rupture(BubbleSurface &surface);
    Box bubble_frame_bounds(const BubbleFrame &frame);
}  // namespace hyprimate::bubble_frame_detail
