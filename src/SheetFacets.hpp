#pragma once

#include "LiquidMesh.hpp"

#include <array>
#include <cstddef>

namespace liquid_glass {

// A sheet is the window drawn as a (PAPER_CELLS + 1)^2 vertex grid of lit triangles. Paper and cloth both animate the
// grid in 3D and project it through sheetFacets; the paper renderer draws the result.
inline constexpr int         PAPER_CELLS = 56;
inline constexpr std::size_t PAPER_TRIANGLES = std::size_t(PAPER_CELLS) * PAPER_CELLS * 2; // 6272
inline constexpr std::size_t PAPER_GRID_VERTICES = std::size_t(PAPER_CELLS + 1) * (PAPER_CELLS + 1);

struct PaperFacet {
    std::array<Vec2, 3>
        position{};             // displayed, relative to the rest capture centre, in units of the rest size (x/W, y/H)
    std::array<Vec2, 3>   uv{}; // [0,1] over the rest capture
    std::array<double, 3> shade{}; // per corner, [-1,1]; <0 darker, >0 lighter, 0 unlit
};
using PaperFacets = std::array<PaperFacet, PAPER_TRIANGLES>;

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// Sheet vertex positions, index j * (PAPER_CELLS + 1) + i for column i and row j (row 0 is the window's top edge).
// Logical pixels relative to the rest capture centre: x right, y down, z towards the viewer.
using SheetGrid = std::array<Vec3, PAPER_GRID_VERTICES>;

// The window at rest: every facet in place and unlit.
void flatSheetFacets(PaperFacets &out) noexcept;

// Projects `world` onto the screen with a fixed perspective (eye 3 * sqrt(W * H) in front of the sheet), orders the
// triangles back to front and lights them. `size` is the rest capture size in logical pixels; `shading` in [0, 1]
// scales the lighting and 0 draws unlit.
void sheetFacets(const SheetGrid &world, Vec2 size, double shading, PaperFacets &out) noexcept;

} // namespace liquid_glass
