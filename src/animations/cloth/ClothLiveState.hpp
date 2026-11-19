#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "animations/cloth/ClothFlutter.hpp"

struct SClothStyle {
    hyprimate::ClothFlutter cloth;
    hyprimate::ClothMovementParams movement{};
    std::unique_ptr<hyprimate::SheetFrame> frame = std::make_unique<hyprimate::SheetFrame>();
    hyprimate::Vec2 capture_size{};
    std::optional<hyprimate::Edge> origin;
    std::uint64_t seed = 0;
};
