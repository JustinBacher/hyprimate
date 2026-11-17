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
            const int i = static_cast<int>(CELL % PAPER_CELLS);
            const int j = static_cast<int>(CELL / PAPER_CELLS);
            const bool SECOND = (t & 1U) != 0;
            return SECOND
                       ? Triangle{gridIndex(i + 1, j), gridIndex(i, j + 1), gridIndex(i + 1, j + 1)}
                       : Triangle{gridIndex(i, j), gridIndex(i, j + 1), gridIndex(i + 1, j)};
        }

        const Topology &topology() noexcept {
            static const Topology cached = [] {
                Topology result{};
                for (std::size_t vertex = 0; vertex < PAPER_GRID_VERTICES; ++vertex) {
                    const int i = static_cast<int>(vertex % GRID);
                    const int j = static_cast<int>(vertex / GRID);
                    result.uv[vertex] = {.x = static_cast<double>(i) / PAPER_CELLS,
                        .y = static_cast<double>(j) / PAPER_CELLS};

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
                    ADD(i, j, 0);
                    ADD(i - 1, j, 0);
                    ADD(i - 1, j, 1);
                    ADD(i, j - 1, 0);
                    ADD(i, j - 1, 1);
                    ADD(i - 1, j - 1, 1);
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
                const auto &c = mesh.triangles[t];
                return (world[c[0]].z + world[c[1]].z + world[c[2]].z) / 3.0;
            };
            std::sort(order.begin(), order.end(), [&](std::uint16_t a, std::uint16_t b) {
                const double za = MEAN_Z(a);
                const double zb = MEAN_Z(b);
                return za < zb || (!(zb < za) && a < b);
            });
        }

    }  // namespace

    SheetFrame::SheetFrame() noexcept {
        for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
            order[t] = static_cast<std::uint16_t>(t);
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
            const Vec2 uv = mesh.uv[vertex];
            out.position[vertex] = {.x = uv.x - 0.5, .y = uv.y - 0.5};
        }
        for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
            out.order[t] = static_cast<std::uint16_t>(t);
            out.shade[t] = {0.0F, 0.0F, 0.0F};
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
        const double W = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
        const double H = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
        const double f = 3.0 * std::sqrt(W * H);

        for (std::size_t vi = 0; vi < PAPER_GRID_VERTICES; ++vi) {
            const Vec3 &X = world[vi];
            const double DENOM = std::max(0.25 * f, f - X.z);
            out.position[vi] = {.x = X.x * f / DENOM / W, .y = X.y * f / DENOM / H};
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
        const double lx = LIGHT_X;
        const double ly = LIGHT_Y;
        const double lz = LIGHT_Z;
        const double L_LEN = std::sqrt((lx * lx) + (ly * ly) + (lz * lz));
        const Vec3 Ld{.x = lx / L_LEN, .y = ly / L_LEN, .z = lz / L_LEN};

        // Area-weighted (unnormalised) normals are used by both modes; unit face normals are only
        // needed for paper creases.
        std::array<Vec3, PAPER_TRIANGLES> faceNormal{};
        std::array<Vec3, PAPER_TRIANGLES> faceUnit{};
        std::array<Vec3, PAPER_GRID_VERTICES> vertexNormal{};
        for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
            const auto &c = mesh.triangles[tri];
            const Vec3 n = cross(world[c[1]] - world[c[0]], world[c[2]] - world[c[0]]);
            const double LEN = std::sqrt(dot(n, n));
            faceNormal[tri] = n;
            if (lighting == SheetLighting::PaperCreases) {
                faceUnit[tri] = LEN >= 1e-12 ? n * (1.0 / LEN) : Vec3{};
            } else {
                for (const std::size_t VERTEX : c) {
                    Vec3 &sum = vertexNormal[VERTEX];
                    sum = {.x = sum.x + n.x, .y = sum.y + n.y, .z = sum.z + n.z};
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
                const Vec3 &n = faceNormal[NEIGHBOR];
                sum = {.x = sum.x + n.x, .y = sum.y + n.y, .z = sum.z + n.z};
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
            const double DIFF = dot(n, Ld) - Ld.z;
            const double s = DIFF >= 0.0 ? DIFF / (1.0 - Ld.z) : DIFF / (1.0 + Ld.z);
            const double SHADE = std::clamp(s * 1.8 * shading, -1.0, 1.0);
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
            for (std::size_t n = 0; n < 3; ++n) {
                double s = 0.0;
                if (lighting == SheetLighting::ClothSmooth) {
                    s = FACING > 0.0 ? vertexShadeFront[corners[n]] : vertexShadeBack[corners[n]];
                } else {
                    s = SHADE_OF(CORNER_NORMAL(tri, corners[n]), FACING);
                }
                out.shade[tri][n] = static_cast<float>(s);
            }
        }
    }

}  // namespace hyprimate
