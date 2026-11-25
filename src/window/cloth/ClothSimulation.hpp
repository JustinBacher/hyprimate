#pragma once

#include <vector>

#include "window/cloth/ClothFlutter.hpp"

namespace hyprimate::cloth_detail {
    inline constexpr int MAX_OBSTACLES = 32;

    struct SimInput {
        std::vector<Box> obstacles;
        double flutter = 0;
        double phase_a = 0, phase_b = 0, freq_a = 0;
    };

    void simulate(ClothFlutter::Run &run, const SimInput &input);
    void simulate_movement(ClothFlutter::Run &run, double phase);
}  // namespace hyprimate::cloth_detail
