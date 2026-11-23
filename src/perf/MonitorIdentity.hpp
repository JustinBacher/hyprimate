#pragma once

#include <cstdint>
#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace hyprimate::perf {
    // Compositor thread only. Uses the window port's lifetime-stable identity mapping.
    [[nodiscard]] std::uint64_t monitor_identity(const PHLMONITOR &monitor) noexcept;
}
