#pragma once

#include <memory>

#include "animations/paper/PaperCrumple.hpp"

struct SPaperStyle {
    hyprimate::PaperCrumple crumple{};
    std::unique_ptr<hyprimate::SheetFrame> frame = std::make_unique<hyprimate::SheetFrame>();
    hyprimate::Vec2 capture_size{};
};
