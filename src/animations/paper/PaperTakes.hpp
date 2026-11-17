#pragma once

#include <cstddef>
#include <span>

namespace hyprimate {

    // Paper crumples baked offline with Blender's cloth solver (src/animations/paper/tools) and
    // embedded in the plugin. Each take is a flat sheet of PAPER_CELLS + 1 vertices per side
    // crumpling into a ball; frame 0 is the flat sheet. Positions are in units of sqrt(W * H) of
    // the take's own sheet, whose width / height is paper_take_aspect(take) >= 1.
    int paper_take_count() noexcept;
    int paper_take_frames() noexcept;
    double paper_take_aspect(int take) noexcept;
    // Position of `vertex` (index j * (PAPER_CELLS + 1) + i) at `frame` of `take`; arguments must
    // be in range.
    void paper_take_vertex(
        int take, int frame, std::size_t vertex, std::span<double, 3> out) noexcept;

}  // namespace hyprimate
