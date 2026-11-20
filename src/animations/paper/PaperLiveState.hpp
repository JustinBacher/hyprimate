#pragma once

#include <memory>

#include "animations/paper/PaperWorkers.hpp"

struct SPaperStyle {
    std::shared_ptr<hyprimate::PaperRun> run;
    hyprimate::PaperSurface display;
};
