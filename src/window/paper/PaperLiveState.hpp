#pragma once

#include <memory>
#include <optional>

#include "window/paper/PaperInteraction.hpp"
#include "window/paper/PaperWorkers.hpp"

struct SPaperStyle {
    std::shared_ptr<hyprimate::PaperRun> run;
    hyprimate::PaperSurface display;
    hyprimate::PaperInteraction interaction;
    // A close worker reads the last live mesh while the compositor still presents it.
    std::shared_ptr<const hyprimate::PaperSurface> closing_from;
    std::optional<hyprimate::Box> running_bounds;
    const hyprimate::PaperPoint *running_bounds_points = nullptr;
    hyprimate::Vec2 bounds_capture;
    [[nodiscard]] const hyprimate::PaperSurface &surface() const {
        return closing_from ? *closing_from : display;
    }
};
