#pragma once

#include <cstddef>

#include <array>
#include <optional>

#include "WindowAnimationPort.hpp"

namespace hyprimate::animation::detail {
    std::optional<double> step_clock(bool &stepped, Clock::time_point &last_step,
        FrameId &last_frame, std::size_t &stall_count,
        std::array<Clock::time_point, STALL_LIMIT> &stalls, const FrameStamp &stamp);
}  // namespace hyprimate::animation::detail
