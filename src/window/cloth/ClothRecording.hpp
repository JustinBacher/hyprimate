#pragma once

#include <span>

#include "window/cloth/ClothFlutter.hpp"
#include "window/cloth/ClothSolver.hpp"

namespace hyprimate::cloth_detail {
    void allocate_recording(ClothFlutter::Run &run);
    bool record(ClothFlutter::Run &run,
        int frame,
        std::span<const cloth::Point> positions,
        std::span<const cloth::Point> velocities) noexcept;
}  // namespace hyprimate::cloth_detail
