#pragma once
#include "PaperCrumple.hpp"
#include <cstdint>

struct SPaperCloseData {
    liquid_glass::PaperParams params{};
    std::uint64_t seed = 0;
};
