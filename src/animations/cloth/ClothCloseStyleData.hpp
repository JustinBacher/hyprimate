#pragma once
#include "ClothFlutter.hpp"
#include <cstdint>
#include <optional>
#include <vector>

struct SClothCloseData {
    liquid_glass::ClothParams params{};
    std::uint64_t seed = 0;
    liquid_glass::Box capture{};
    std::vector<liquid_glass::Box> obstacles;
    std::optional<liquid_glass::ClothCloseGeometry> geometry;
};
