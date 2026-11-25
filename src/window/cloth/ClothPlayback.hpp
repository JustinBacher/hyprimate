#pragma once

#include "window/cloth/ClothFlutter.hpp"

namespace hyprimate::cloth_detail {
    // Copy the pair before releasing queue slots back to the producer.
    bool consume_movement(ClothFlutter::Run &run,
        ClothFlutter::MovementPlayback &playback,
        double wanted) noexcept;
}  // namespace hyprimate::cloth_detail
