#pragma once

#include <cstdint>
#include <optional>

namespace hyprimate::workspace {
// Captured at navigation time, not looked up again as workspaces appear or disappear.
struct JourneyEndpoint {
    std::int64_t identity = 0;
    std::optional<std::uint32_t> numbered;
    bool special = false;
    std::uint32_t logicalOrder = 0;
    // Durable spatial slot, independent of compositor object lifetime and queue identity.
    std::uint64_t placement = 0;
};
}
