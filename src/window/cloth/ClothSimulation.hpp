#pragma once

#include <cstdint>
#include <vector>

#include "window/cloth/ClothFlutter.hpp"

namespace hyprimate::cloth_detail {
    inline constexpr int MAX_OBSTACLES = 32;

    struct SimInput {
        std::vector<Box> obstacles;
        double flutter = 0;
        double phase_a = 0, phase_b = 0, freq_a = 0;
    };

    enum class TerminalReason : std::uint8_t;
    [[nodiscard]] TerminalReason simulate(ClothFlutter::Run &run, const SimInput &input);
    [[nodiscard]] TerminalReason simulate_movement(ClothFlutter::Run &run, double phase);
}  // namespace hyprimate::cloth_detail
