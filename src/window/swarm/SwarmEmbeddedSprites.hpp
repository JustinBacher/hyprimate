#pragma once

#include <span>
#include <string_view>

namespace hyprimate {
    // Explicit paths and disabled built-ins always fall back to filesystem loading.
    std::span<const unsigned char> embedded_swarm_sprite(std::string_view name, bool enabled) noexcept;
}
