#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

        Vec3 operator-(Vec3 a, Vec3 b) noexcept {
            return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
        }
        Vec3 operator*(Vec3 a, double s) noexcept {
            return {.x = a.x * s, .y = a.y * s, .z = a.z * s};
        }
        double dot(Vec3 a, Vec3 b) noexcept {
            return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
        }
        Vec3 cross(Vec3 a, Vec3 b) noexcept {
            return {.x = (a.y * b.z) - (a.z * b.y),
                .y = (a.z * b.x) - (a.x * b.z),
                .z = (a.x * b.y) - (a.y * b.x)};
        }

        std::size_t gridIndex(int i, int j) noexcept {
            return (static_cast<std::size_t>(j) * GRID) + static_cast<std::size_t>(i);
        }

        // Grid corners of triangle t: two per cell, same winding as the idle sheet and the baked
        // takes.
        Triangle triangleCorners(std::size_t t) noexcept {
            const std::size_t CELL = t / 2;
            const int COL = static_cast<int>(CELL % PAPER_CELLS);
            const int ROW = static_cast<int>(CELL / PAPER_CELLS);
            const bool SECOND = (t & 1U) != 0;
            return SECOND
                       ? Triangle{gridIndex(COL + 1, ROW),
                             gridIndex(COL, ROW + 1),
                             gridIndex(COL + 1, ROW + 1)}
                       : Triangle{
                             gridIndex(COL, ROW), gridIndex(COL, ROW + 1), gridIndex(COL + 1, ROW)};
        }

        const Topology &topology() noexcept {
            static const Topology cached = [] {
                Topology result{};
                for (std::size_t vertex = 0; vertex < PAPER_GRID_VERTICES; ++vertex) {
                    const int COL = static_cast<int>(vertex % GRID);
                    const int ROW = static_cast<int>(vertex / GRID);
                    result.uv[vertex] = {.x = static_cast<double>(COL) / PAPER_CELLS,
                        .y = static_cast<double>(ROW) / PAPER_CELLS};

                    auto &adjacent = result.adjacent[vertex];
                    const auto ADD = [&](int ci, int cj, unsigned half) {
                        if (ci >= 0 && cj >= 0 && ci < PAPER_CELLS && cj < PAPER_CELLS) {
                            adjacent.triangles[adjacent.count++] = static_cast<std::uint16_t>(
                                (((static_cast<std::size_t>(cj) * PAPER_CELLS) +
                                     static_cast<std::size_t>(ci)) *
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
                }
                for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
                    result.triangles[tri] = triangleCorners(tri);
                }
                return result;
            }();
            return cached;
        }

        // Back-to-front by mean world z; the triangle index breaks ties. Sorting (meanZ, t) pairs
        // gives one total order, so the result is the same whatever the starting permutation.
        void sortBackToFront(
            const SheetGrid &world, std::array<std::uint16_t, PAPER_TRIANGLES> &order) noexcept {
            const Topology &mesh = topology();
            const auto MEAN_Z = [&](std::uint16_t t) {
                const auto &corners = mesh.triangles[t];
                return (world[corners[0]].z + world[corners[1]].z + world[corners[2]].z) / 3.0;
            };
            std::sort(order.begin(), order.end(), [&](std::uint16_t a, std::uint16_t b) {
                const double DEPTH_A = MEAN_Z(a);
                const double DEPTH_B = MEAN_Z(b);
                return DEPTH_A < DEPTH_B || (!(DEPTH_B < DEPTH_A) && a < b);
            });
        }

    }  // namespace

    SheetFrame::SheetFrame() noexcept {
        for (std::size_t triangle = 0; triangle < PAPER_TRIANGLES; ++triangle) {
            order[triangle] = static_cast<std::uint16_t>(triangle);
        }
    }

    const std::array<std::size_t, 3> &sheetTriangle(std::size_t t) noexcept {
        return topology().triangles[t];
    }

    Vec2 sheetUv(std::size_t vertex) noexcept {
        return topology().uv[vertex];
    }

    void flatSheet(SheetFrame &out) noexcept {
        const Topology &mesh = topology();
        for (std::size_t vertex = 0; vertex < PAPER_GRID_VERTICES; ++vertex) {
            const Vec2 TEX_COORD = mesh.uv[vertex];
            out.position[vertex] = {.x = TEX_COORD.x - 0.5, .y = TEX_COORD.y - 0.5};
        }
        for (std::size_t triangle = 0; triangle < PAPER_TRIANGLES; ++triangle) {
            out.order[triangle] = static_cast<std::uint16_t>(triangle);
            out.shade[triangle] = {0.0F, 0.0F, 0.0F};
        }
        out.lit = false;
    }

    void projectSheet(const SheetGrid &world,
        Vec2 size,
        double shading,
        SheetLighting lighting,
        SheetFrame &out,
        double shine) noexcept {
        const Topology &mesh = topology();
        const double WIDTH = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
        const double HEIGHT = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
        const double FOCAL_LENGTH = 3.0 * std::sqrt(WIDTH * HEIGHT);

        for (std::size_t vi = 0; vi < PAPER_GRID_VERTICES; ++vi) {
            const Vec3 &world_point = world[vi];
            const double DENOM = std::max(0.25 * FOCAL_LENGTH, FOCAL_LENGTH - world_point.z);
            out.position[vi] = {.x = world_point.x * FOCAL_LENGTH / DENOM / WIDTH,
                .y = world_point.y * FOCAL_LENGTH / DENOM / HEIGHT};
        }

        sortBackToFront(world, out.order);

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
            .x = LIGHT_DIR_X / L_LEN, .y = LIGHT_DIR_Y / L_LEN, .z = LIGHT_DIR_Z / L_LEN};

        // Area-weighted (unnormalised) normals are used by both modes; unit face normals are only
        // needed for paper creases.
        std::array<Vec3, PAPER_TRIANGLES> faceNormal{};
        std::array<Vec3, PAPER_TRIANGLES> faceUnit{};
        std::array<Vec3, PAPER_GRID_VERTICES> vertexNormal{};
        for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
            const auto &tri_corners = mesh.triangles[tri];
            const Vec3 NORMAL = cross(world[tri_corners[1]] - world[tri_corners[0]],
                world[tri_corners[2]] - world[tri_corners[0]]);
            const double LEN = std::sqrt(dot(NORMAL, NORMAL));
            faceNormal[tri] = NORMAL;
            if (lighting == SheetLighting::PaperCreases) {
                faceUnit[tri] = LEN >= 1e-12 ? NORMAL * (1.0 / LEN) : Vec3{};
            } else {
                for (const std::size_t VERTEX : tri_corners) {
                    Vec3 &sum = vertexNormal[VERTEX];
                    sum = {.x = sum.x + NORMAL.x, .y = sum.y + NORMAL.y, .z = sum.z + NORMAL.z};
                }
            }
        }
        // Normal at `vertex` as seen by triangle `tri`.
        const auto CORNER_NORMAL = [&](std::size_t tri, std::size_t vertex) {
            if (lighting == SheetLighting::ClothSmooth) {
                return vertexNormal[vertex];
            }

            Vec3 sum{};
            const VertexAdjacency &around = mesh.adjacent[vertex];
            for (std::uint8_t k = 0; k < around.count; ++k) {
                const std::size_t NEIGHBOR = around.triangles[k];
                if (dot(faceUnit[NEIGHBOR], faceUnit[tri]) < CREASE_COS) {
                    continue;
                }
                const Vec3 &neighbor_normal = faceNormal[NEIGHBOR];
                sum = {.x = sum.x + neighbor_normal.x,
                    .y = sum.y + neighbor_normal.y,
                    .z = sum.z + neighbor_normal.z};
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

        std::array<double, PAPER_GRID_VERTICES> vertexShadeFront{};
        std::array<double, PAPER_GRID_VERTICES> vertexShadeBack{};
        if (lighting == SheetLighting::ClothSmooth) {
            for (std::size_t vertex = 0; vertex < PAPER_GRID_VERTICES; ++vertex) {
                vertexShadeFront[vertex] = SHADE_OF(vertexNormal[vertex], 1.0);
                vertexShadeBack[vertex] = SHADE_OF(vertexNormal[vertex], -1.0);
            }
        }

        for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
            const auto &corners = mesh.triangles[tri];
            // The side of the sheet this triangle shows decides which way its vertex normals point.
            const double FACING = faceNormal[tri].z < 0.0 ? -1.0 : 1.0;
            for (std::size_t corner_index = 0; corner_index < 3; ++corner_index) {
                double corner_shade = 0.0;
                if (lighting == SheetLighting::ClothSmooth) {
                    corner_shade = FACING > 0.0 ? vertexShadeFront[corners[corner_index]]
                                                : vertexShadeBack[corners[corner_index]];
                } else {
                    corner_shade = SHADE_OF(CORNER_NORMAL(tri, corners[corner_index]), FACING);
                }
                out.shade[tri][corner_index] = static_cast<float>(corner_shade);
            }
        }
    }

}  // namespace hyprimate
