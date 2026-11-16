#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // A sheet is the window drawn as a (PAPER_CELLS + 1)^2 vertex grid of lit triangles. Paper and
    // cloth both animate the grid in 3D and project it through projectSheet; the paper renderer
    // draws the result.
    inline constexpr int PAPER_CELLS = 56;
    inline constexpr std::size_t PAPER_TRIANGLES =
        std::size_t(PAPER_CELLS) * PAPER_CELLS * 2;  // 6272
    inline constexpr std::size_t PAPER_GRID_VERTICES =
        std::size_t(PAPER_CELLS + 1) * (PAPER_CELLS + 1);

    // Sheet vertex positions, index j * (PAPER_CELLS + 1) + i for column i and row j (row 0 is the
    // window's top edge). Logical pixels relative to the rest capture centre: x right, y down, z
    // towards the viewer.
    using SheetGrid = std::array<Vec3, PAPER_GRID_VERTICES>;

    enum class SheetLighting { PaperCreases, ClothSmooth };

    // The projected sheet, reused across frames. Triangle t has grid corners sheetTriangle(t), uv
    // sheetUv(corner).
    struct SheetFrame {
        SheetFrame() noexcept;  // identity draw order, so the first projection starts from a valid
                                // permutation

        // Displayed grid vertex positions, relative to the rest capture centre, in units of the
        // rest size (x/W, y/H).
        std::array<Vec2, PAPER_GRID_VERTICES> position{};
        // Per triangle, per corner, in [-1,1]: <0 darker, >0 lighter. All zero when unlit.
        std::array<std::array<float, 3>, PAPER_TRIANGLES> shade{};
        // Triangle ids sorted back to front.
        std::array<std::uint16_t, PAPER_TRIANGLES> order{};
        bool lit = false;
    };

    // Grid corners of triangle t, in the order matching shade[t] and the uv of each corner.
    const std::array<std::size_t, 3> &sheetTriangle(std::size_t t) noexcept;

    // Rest uv of a grid vertex in [0,1] over the rest capture.
    Vec2 sheetUv(std::size_t vertex) noexcept;

    // The window at rest: every vertex in place, identity order, unlit.
    void flatSheet(SheetFrame &out) noexcept;

    // Projects `world` onto the screen with a fixed perspective (eye 3 * sqrt(W * H) in front of
    // the sheet), orders the triangles back to front and lights them. `size` is the rest capture
    // size in logical pixels; `shading` in [0, 1] scales the lighting and 0 draws unlit.
    void projectSheet(const SheetGrid &world,
        Vec2 size,
        double shading,
        SheetLighting lighting,
        SheetFrame &out,
        double shine = 1.0) noexcept;

}  // namespace hyprimate
