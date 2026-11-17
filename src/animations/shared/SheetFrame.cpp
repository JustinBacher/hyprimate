#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {

        constexpr int GRID = PAPER_CELLS + 1;
        using Triangle = std::array<std::size_t, 3>;

        struct VertexAdjacency {
            std::array<std::uint16_t, 6> triangles{};
            std::uint8_t count = 0;
        };

        struct Topology {
            std::array<Triangle, PAPER_TRIANGLES> triangles{};
            std::array<VertexAdjacency, PAPER_GRID_VERTICES> adjacent{};
            std::array<Vec2, PAPER_GRID_VERTICES> uv{};
        };

        // Light direction (towards the light), normalised below.
        constexpr double LIGHT_X = -0.35, LIGHT_Y = -0.55, LIGHT_Z = 0.76;

        // Neighbouring triangles whose normals differ by more than about 35 degrees are across a
        // crease: they do not share a smoothed normal, so the crease stays a sharp line of light
        // and shadow.
        constexpr double CREASE_COS = 0.82;

        Vec3 operator-(Vec3 left, Vec3 right) noexcept {
            return {.x = left.x - right.x, .y = left.y - right.y, .z = left.z - right.z};
        }
        Vec3 operator*(Vec3 vector, double scale) noexcept {
            return {.x = vector.x * scale, .y = vector.y * scale, .z = vector.z * scale};
        }
        double dot(Vec3 left, Vec3 right) noexcept {
            return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
        }
        Vec3 cross(Vec3 left, Vec3 right) noexcept {
            return {
                .x = (left.y * right.z) - (left.z * right.y),
                .y = (left.z * right.x) - (left.x * right.z),
                .z = (left.x * right.y) - (left.y * right.x),
            };
        }

        std::size_t grid_index(int col, int row) noexcept {
            return (static_cast<std::size_t>(row) * GRID) + static_cast<std::size_t>(col);
        }

        // Grid corners of triangle t: two per cell, same winding as the idle sheet and the baked
        // takes.
        Triangle triangle_corners(std::size_t triangle) noexcept {
            const std::size_t CELL = triangle / 2;
            const int COL = static_cast<int>(CELL % PAPER_CELLS);
            const int ROW = static_cast<int>(CELL / PAPER_CELLS);
            const bool SECOND = (triangle & 1U) != 0;
            return SECOND ? Triangle{grid_index(COL + 1, ROW),
                                grid_index(COL, ROW + 1),
                                grid_index(COL + 1, ROW + 1),}
                          : Triangle{grid_index(COL, ROW),
                                grid_index(COL, ROW + 1),
                                grid_index(COL + 1, ROW),};
        }

        const Topology &topology() noexcept {
            static const Topology CACHED = [] {
                Topology result{};
                std::size_t vertex = 0;
                for (auto &&[tex_coord, adjacent] : std::views::zip(result.uv, result.adjacent)) {
                    const int COL = static_cast<int>(vertex % GRID);
                    const int ROW = static_cast<int>(vertex / GRID);
                    tex_coord = {
                        .x = static_cast<double>(COL) / PAPER_CELLS,
                        .y = static_cast<double>(ROW) / PAPER_CELLS,
                    };

                    auto next = std::span(adjacent.triangles).begin();
                    const auto ADD = [&](int cell_col, int cell_row, unsigned half) {
                        if (cell_col >= 0 && cell_row >= 0 && cell_col < PAPER_CELLS &&
                            cell_row < PAPER_CELLS) {
                            *next++ = static_cast<std::uint16_t>(
                                (((static_cast<std::size_t>(cell_row) * PAPER_CELLS) +
                                     static_cast<std::size_t>(cell_col)) *
                                    2) +
                                half);
                        }
                    };
                    // Keep the original paper neighborhood order so crease shading remains
                    // identical.
                    ADD(COL, ROW, 0);
                    ADD(COL - 1, ROW, 0);
                    ADD(COL - 1, ROW, 1);
                    ADD(COL, ROW - 1, 0);
                    ADD(COL, ROW - 1, 1);
                    ADD(COL - 1, ROW - 1, 1);
                    adjacent.count =
                        static_cast<std::uint8_t>(next - std::span(adjacent.triangles).begin());
                    ++vertex;
                }
                std::size_t tri = 0;
                std::ranges::generate(result.triangles, [&] { return triangle_corners(tri++); });
                return result;
            }();
            return CACHED;
        }

        // Back-to-front by mean world z; the triangle index breaks ties. Sorting (meanZ, t) pairs
        // gives one total order, so the result is the same whatever the starting permutation.
        void sort_back_to_front(
            const SheetGrid &world, std::array<std::uint16_t, PAPER_TRIANGLES> &order) noexcept {
            const Topology &mesh = topology();
            const auto MEAN_Z = [&](std::uint16_t triangle) {
                const auto &corners = mesh.triangles[triangle];
                return (world[std::get<0>(corners)].z + world[std::get<1>(corners)].z +
                           world[std::get<2>(corners)].z) /
                       3.0;
            };
            std::ranges::sort(order, [&](std::uint16_t first, std::uint16_t second) {
                const double DEPTH_A = MEAN_Z(first);
                const double DEPTH_B = MEAN_Z(second);
                return DEPTH_A < DEPTH_B || (!(DEPTH_B < DEPTH_A) && first < second);
            });
        }

    }  // namespace

    SheetFrame::SheetFrame() noexcept {
        std::uint16_t triangle = 0;
        std::ranges::generate(order, [&] { return triangle++; });
    }

    const std::array<std::size_t, 3> &sheet_triangle(std::size_t triangle) noexcept {
        return topology().triangles[triangle];
    }

    Vec2 sheet_uv(std::size_t vertex) noexcept {
        return topology().uv[vertex];
    }

    void flat_sheet(SheetFrame &out) noexcept {
        const Topology &mesh = topology();
        std::ranges::transform(mesh.uv, out.position.begin(), [](Vec2 tex_coord) {
            return Vec2{.x = tex_coord.x - 0.5, .y = tex_coord.y - 0.5};
        });
        std::uint16_t triangle = 0;
        for (auto &&[order, shade] : std::views::zip(out.order, out.shade)) {
            order = triangle++;
            shade = {0.0F, 0.0F, 0.0F};
        }
        out.lit = false;
    }

    void project_sheet(const SheetGrid &world,
        Vec2 size,
        double shading,
        SheetLighting lighting,
        SheetFrame &out,
        double shine) noexcept {
        const Topology &mesh = topology();
        const double WIDTH = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
        const double HEIGHT = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
        const double FOCAL_LENGTH = 3.0 * std::sqrt(WIDTH * HEIGHT);

        std::ranges::transform(world, out.position.begin(), [&](const Vec3 &world_point) {
            const double DENOM = std::max(0.25 * FOCAL_LENGTH, FOCAL_LENGTH - world_point.z);
            return Vec2{
                .x = world_point.x * FOCAL_LENGTH / DENOM / WIDTH,
                .y = world_point.y * FOCAL_LENGTH / DENOM / HEIGHT,
            };
        });

        sort_back_to_front(world, out.order);

        out.lit = shading > 0.0;
        if (!out.lit) {
            for (auto &shade : out.shade) {
                shade = {0.0F, 0.0F, 0.0F};
            }
            return;
        }

        // Lighting is two-sided and fixed. Paper keeps the crease-aware per-corner normals that
        // preserve its sharp folds; cloth uses a single area-weighted normal per grid vertex for
        // smoother fabric lighting.
        const double LIGHT_DIR_X = LIGHT_X;
        const double LIGHT_DIR_Y = LIGHT_Y;
        const double LIGHT_DIR_Z = LIGHT_Z;
        const double L_LEN = std::sqrt((LIGHT_DIR_X * LIGHT_DIR_X) + (LIGHT_DIR_Y * LIGHT_DIR_Y) +
                                       (LIGHT_DIR_Z * LIGHT_DIR_Z));
        const Vec3 LIGHT_DIR{
            .x = LIGHT_DIR_X / L_LEN,
            .y = LIGHT_DIR_Y / L_LEN,
            .z = LIGHT_DIR_Z / L_LEN,
        };

        // Area-weighted (unnormalised) normals are used by both modes; unit face normals are only
        // needed for paper creases.
        std::array<Vec3, PAPER_TRIANGLES> face_normal{};
        std::array<Vec3, PAPER_TRIANGLES> face_unit{};
        std::array<Vec3, PAPER_GRID_VERTICES> vertex_normal{};
        for (auto &&[tri_corners, normal, unit] :
            std::views::zip(mesh.triangles, face_normal, face_unit)) {
            const Vec3 NORMAL =
                cross(world[std::get<1>(tri_corners)] - world[std::get<0>(tri_corners)],
                    world[std::get<2>(tri_corners)] - world[std::get<0>(tri_corners)]);
            const double LEN = std::sqrt(dot(NORMAL, NORMAL));
            normal = NORMAL;
            if (lighting == SheetLighting::PaperCreases) {
                unit = LEN >= 1e-12 ? NORMAL * (1.0 / LEN) : Vec3{};
            } else {
                for (const std::size_t VERTEX : tri_corners) {
                    Vec3 &sum = vertex_normal[VERTEX];
                    sum = {.x = sum.x + NORMAL.x, .y = sum.y + NORMAL.y, .z = sum.z + NORMAL.z};
                }
            }
        }
        // Normal at `vertex` as seen by triangle `tri`.
        const auto CORNER_NORMAL = [&](std::size_t tri, std::size_t vertex) {
            if (lighting == SheetLighting::ClothSmooth) {
                return vertex_normal[vertex];
            }

            Vec3 sum{};
            const VertexAdjacency &around = mesh.adjacent[vertex];
            for (const std::size_t NEIGHBOR : std::span(around.triangles).first(around.count)) {
                if (dot(face_unit[NEIGHBOR], face_unit[tri]) < CREASE_COS) {
                    continue;
                }
                const Vec3 &neighbor_normal = face_normal[NEIGHBOR];
                sum = {
                    .x = sum.x + neighbor_normal.x,
                    .y = sum.y + neighbor_normal.y,
                    .z = sum.z + neighbor_normal.z,
                };
            }
            return sum;
        };
        // Shade for a normal seen from the side `facing` (+1 front, -1 back) of the sheet.
        const auto SHADE_OF = [&](Vec3 n, double facing) {
            const double LEN = std::sqrt(dot(n, n));
            if (LEN < 1e-12) {
                return 0.0;
            }
            n = n * (facing / LEN);
            const double DIFF = dot(n, LIGHT_DIR) - LIGHT_DIR.z;
            const double LIGHT_RESPONSE =
                DIFF >= 0.0 ? DIFF / (1.0 - LIGHT_DIR.z) : DIFF / (1.0 + LIGHT_DIR.z);
            const double SHADE = std::clamp(LIGHT_RESPONSE * 1.8 * shading, -1.0, 1.0);
            return lighting == SheetLighting::ClothSmooth && SHADE > 0
                       ? SHADE * std::clamp(shine, 0.0, 1.0)
                       : SHADE;
        };

        std::array<double, PAPER_GRID_VERTICES> vertex_shade_front{};
        std::array<double, PAPER_GRID_VERTICES> vertex_shade_back{};
        if (lighting == SheetLighting::ClothSmooth) {
            for (auto &&[normal, front, back] :
                std::views::zip(vertex_normal, vertex_shade_front, vertex_shade_back)) {
                front = SHADE_OF(normal, 1.0);
                back = SHADE_OF(normal, -1.0);
            }
        }

        std::size_t tri = 0;
        for (auto &&[corners, normal, shade] :
            std::views::zip(mesh.triangles, face_normal, out.shade)) {
            // The side of the sheet this triangle shows decides which way its vertex normals point.
            const double FACING = normal.z < 0.0 ? -1.0 : 1.0;
            std::ranges::transform(corners, shade.begin(), [&](std::size_t vertex) {
                double corner_shade = 0.0;
                if (lighting == SheetLighting::ClothSmooth) {
                    corner_shade =
                        FACING > 0.0 ? vertex_shade_front[vertex] : vertex_shade_back[vertex];
                } else {
                    corner_shade = SHADE_OF(CORNER_NORMAL(tri, vertex), FACING);
                }
                return static_cast<float>(corner_shade);
            });
            ++tri;
        }
    }

}  // namespace hyprimate
