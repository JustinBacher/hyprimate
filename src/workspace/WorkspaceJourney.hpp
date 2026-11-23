#pragma once

#include <cstdint>
#include <optional>

namespace hyprimate::workspace {
    // Captured at navigation time, not looked up again as workspaces appear or disappear.
    struct JourneyEndpoint {
        std::int64_t identity = 0;
        std::optional<std::uint32_t> numbered = std::nullopt;
        bool special = false;
        std::uint32_t logical_order = 0;
        // Durable spatial slot, independent of compositor object lifetime and queue identity.
        std::uint64_t placement = 0;
        // Owner geometry is snapshotted with the composition, in compositor logical pixels.
        // It is independent of capture readiness and of the number of connected outputs.
        std::uint64_t owner = 0;
        double monitor_x = 0;
        double monitor_y = 0;
        double monitor_width = 1;
        double monitor_height = 1;
        bool operator==(const JourneyEndpoint &) const = default;
    };
}  // namespace hyprimate::workspace
