#pragma once

#include <memory>

#include "animations/paper/PaperInteraction.hpp"
#include "animations/paper/PaperWorkers.hpp"

struct SPaperStyle {
    std::shared_ptr<hyprimate::PaperRun> run;
    hyprimate::PaperSurface display;
    hyprimate::PaperInteraction interaction;
    // A close worker reads the last live mesh while the compositor still presents it.
    std::shared_ptr<const hyprimate::PaperSurface> closing_from;
    [[nodiscard]] const hyprimate::PaperSurface &surface() const {
        return closing_from && display.faces.empty() ? *closing_from : display;
    }
};
