#pragma once

#include <optional>

#include "WindowAnimationPort.hpp"

namespace hyprimate::animation {
    // Compare two observations without changing animation lifetime or frame policy.
    // A missing motion denotes an unchanged geometry or a discontinuity to rebaseline.
    std::optional<Motion> observe_motion(const Observation &previous,
        const Observation &current,
        bool previous_workspace_movement,
        bool workspace_movement,
        bool sweep_large_moves = false) noexcept;
}  // namespace hyprimate::animation
