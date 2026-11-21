#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>

#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {

        constexpr int GRID = SHEET_CELLS + 1;
        using Triangle = std::array<std::size_t, 3>;

        struct Topology {
            std::array<Triangle, SHEET_TRIANGLES> triangles{};
            std::array<Vec2, SHEET_GRID_VERTICES> uv{};
        };

        // Light direction (towards the light), normalised below.
        constexpr double LIGHT_X = -0.35, LIGHT_Y = -0.55, LIGHT_Z = 0.76;

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

        // Grid corners of triangle t: two per cell, same winding as the idle sheet.
        Triangle triangle_corners(std::size_t triangle) noexcept {
            const std::size_t CELL = triangle / 2;
            const int COL = static_cast<int>(CELL % SHEET_CELLS);
            const int ROW = static_cast<int>(CELL / SHEET_CELLS);
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
                for (auto &tex_coord : result.uv) {
                    const int COL = static_cast<int>(vertex % GRID);
                    const int ROW = static_cast<int>(vertex / GRID);
                    tex_coord = {
                        .x = static_cast<double>(COL) / SHEET_CELLS,
                        .y = static_cast<double>(ROW) / SHEET_CELLS,
                    };
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
            const SheetGrid &world, std::array<std::uint16_t, SHEET_TRIANGLES> &order) noexcept {
            const Topology &mesh = topology();
            std::array<double, SHEET_TRIANGLES> depths;
            for (std::size_t triangle = 0; triangle < SHEET_TRIANGLES; ++triangle) {
                const auto &corners = mesh.triangles[triangle];
                depths[triangle] = (world[std::get<0>(corners)].z + world[std::get<1>(corners)].z +
                                       world[std::get<2>(corners)].z) /
                                   3.0;
            }
            std::ranges::sort(order, [&](std::uint16_t first, std::uint16_t second) {
                const double DEPTH_A = depths[first];
                const double DEPTH_B = depths[second];
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

        // Two-sided, area-weighted vertex normals for fabric lighting.
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

        std::array<Vec3, SHEET_TRIANGLES> face_normal{};
        std::array<Vec3, SHEET_GRID_VERTICES> vertex_normal{};
        for (auto &&[tri_corners, normal] : std::views::zip(mesh.triangles, face_normal)) {
            const Vec3 NORMAL =
                cross(world[std::get<1>(tri_corners)] - world[std::get<0>(tri_corners)],
                    world[std::get<2>(tri_corners)] - world[std::get<0>(tri_corners)]);
            normal = NORMAL;
            for (const std::size_t VERTEX : tri_corners) {
                Vec3 &sum = vertex_normal[VERTEX];
                sum = {.x = sum.x + NORMAL.x, .y = sum.y + NORMAL.y, .z = sum.z + NORMAL.z};
            }
        }
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
            return SHADE > 0 ? SHADE * std::clamp(shine, 0.0, 1.0) : SHADE;
        };

        std::array<double, SHEET_GRID_VERTICES> vertex_shade_front{};
        std::array<double, SHEET_GRID_VERTICES> vertex_shade_back{};
        for (auto &&[normal, front, back] :
            std::views::zip(vertex_normal, vertex_shade_front, vertex_shade_back)) {
            front = SHADE_OF(normal, 1.0);
            back = SHADE_OF(normal, -1.0);
        }

        for (auto &&[corners, normal, shade] :
            std::views::zip(mesh.triangles, face_normal, out.shade)) {
            // The side of the sheet this triangle shows decides which way its vertex normals point.
            const double FACING = normal.z < 0.0 ? -1.0 : 1.0;
            std::ranges::transform(corners, shade.begin(), [&](std::size_t vertex) {
                return static_cast<float>(FACING > 0.0 ? vertex_shade_front[vertex] : vertex_shade_back[vertex]);
            });
        }
    }

}  // namespace hyprimate
