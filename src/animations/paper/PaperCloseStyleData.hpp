#pragma once
#include <cstdint>

#include "PaperCrumple.hpp"

struct SPaperCloseData {
    hyprimate::PaperParams params{};
    std::uint64_t seed = 0;
};
