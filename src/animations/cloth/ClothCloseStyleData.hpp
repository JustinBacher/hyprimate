#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include "ClothFlutter.hpp"

struct SClothCloseData {
    hyprimate::ClothParams params{};
    std::uint64_t seed = 0;
    hyprimate::Box capture{};
    std::vector<hyprimate::Box> obstacles;
    std::optional<hyprimate::ClothCloseGeometry> geometry;
};
