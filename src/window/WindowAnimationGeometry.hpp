#pragma once

#include <cstdint>

#include "WindowAnimationPort.hpp"

namespace hyprimate::animation::detail {
    Place place(const Observation &observation, Neighbourhood near, std::uint64_t seed);
    Box presented_body(const Observation &observation);
    Box source_device(const Observation &observation);
    Box rest_capture(const Observation &observation, const Box &goal);
    Step dying_step(const Place &place, Box source_device, double captured_scale,
        bool public_owned, MonitorId monitor, double elapsed);
    Box dying_footprint(const Box &bounds, const Box &screen, double captured_scale);
}  // namespace hyprimate::animation::detail
