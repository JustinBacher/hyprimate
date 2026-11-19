#pragma once

#include <cstdint>
#include <optional>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {
inline Direction direction_between(std::optional<std::uint32_t> source,
    std::optional<std::uint32_t> destination, bool source_special, bool destination_special) {
    if (source_special != destination_special) {
        return destination_special ? Direction::Forward : Direction::Backward;
    }
    return source && destination && *destination < *source ? Direction::Backward : Direction::Forward;
}
}
