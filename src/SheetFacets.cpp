#include "SheetFacets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace liquid_glass {
namespace {

constexpr int GRID = PAPER_CELLS + 1;

// Light direction (towards the light), normalised below.
constexpr double LIGHT_X = -0.35, LIGHT_Y = -0.55, LIGHT_Z = 0.76;

// Neighbouring triangles whose normals differ by more than about 35 degrees are across a crease: they do not share
// a smoothed normal, so the crease stays a sharp line of light and shadow.
constexpr double CREASE_COS = 0.82;

Vec3   operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3   operator*(Vec3 a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3   cross(Vec3 a, Vec3 b) noexcept { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Position of grid vertex `index` in the (PAPER_CELLS+1)^2 lattice, as (u, v) in [0, 1].
Vec2 gridUv(std::size_t index) noexcept {
    const int i = static_cast<int>(index % GRID);
    const int j = static_cast<int>(index / GRID);
    return {static_cast<double>(i) / PAPER_CELLS, static_cast<double>(j) / PAPER_CELLS};
}

std::size_t gridIndex(int i, int j) noexcept { return std::size_t(j) * GRID + std::size_t(i); }

// Grid corners of triangle t: two per cell, same winding as the idle sheet and the baked takes.
std::array<std::size_t, 3> triangleCorners(std::size_t t) noexcept {
    const std::size_t cell = t / 2;
    const int         i = int(cell % PAPER_CELLS);
    const int         j = int(cell / PAPER_CELLS);
    const bool        second = (t & 1U) != 0;
    return second ? std::array<std::size_t, 3>{gridIndex(i + 1, j), gridIndex(i, j + 1), gridIndex(i + 1, j + 1)}
                  : std::array<std::size_t, 3>{gridIndex(i, j), gridIndex(i, j + 1), gridIndex(i + 1, j)};
}

// Triangles touching grid vertex (i, j), at most six; returns how many were written.
int vertexTriangles(int i, int j, std::array<std::size_t, 6> &out) noexcept {
    int        count = 0;
    const auto add = [&](int ci, int cj, unsigned half) {
        if (ci >= 0 && cj >= 0 && ci < PAPER_CELLS && cj < PAPER_CELLS)
            out[count++] = (std::size_t(cj) * PAPER_CELLS + std::size_t(ci)) * 2 + half;
    };
    add(i, j, 0);     // as the first triangle's (i, j) corner
    add(i - 1, j, 0); // as (i + 1, j) of the cell to the left, in both of its triangles
    add(i - 1, j, 1);
    add(i, j - 1, 0); // as (i, j + 1) of the cell above, in both of its triangles
    add(i, j - 1, 1);
    add(i - 1, j - 1, 1); // as (i + 1, j + 1) of the cell up and to the left
    return count;
}

} // namespace

void flatSheetFacets(PaperFacets &out) noexcept {
    for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
        const auto  corners = triangleCorners(t);
        PaperFacet &facet = out[t];
        for (std::size_t c = 0; c < 3; ++c) {
            const Vec2 uv = gridUv(corners[c]);
            facet.uv[c] = uv;
            facet.position[c] = {uv.x - 0.5, uv.y - 0.5};
        }
        facet.shade = {0.0, 0.0, 0.0};
    }
}

void sheetFacets(const SheetGrid &world, Vec2 size, double shading, PaperFacets &out) noexcept {
    const double W = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
    const double H = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
    const double f = 3.0 * std::sqrt(W * H);

    std::array<Vec2, PAPER_GRID_VERTICES> screen{};
    for (std::size_t vi = 0; vi < PAPER_GRID_VERTICES; ++vi) {
        const Vec3  &X = world[vi];
        const double denom = std::max(0.25 * f, f - X.z);
        screen[vi] = {X.x * f / denom / W, X.y * f / denom / H};
    }

    // Back-to-front order by mean world z; the triangle index breaks ties. Scratch is fixed-size.
    std::array<std::pair<double, std::uint16_t>, PAPER_TRIANGLES> order{};
    for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
        const auto   corners = triangleCorners(t);
        const double meanZ = (world[corners[0]].z + world[corners[1]].z + world[corners[2]].z) / 3.0;
        order[t] = {meanZ, static_cast<std::uint16_t>(t)};
    }
    std::sort(order.begin(), order.end());

    // Lighting: two-sided, fixed light direction, shaded per corner. Each corner averages the normals of the triangles
    // around its vertex that bend less than CREASE_COS away from its own triangle (area-weighted), so light runs
    // smoothly over the sheet between folds and still breaks sharply along a real crease. Per-triangle shading
    // glittered: on the crumpled ball the triangles are a pixel or two wide and each flipped between light and dark
    // from frame to frame; fully smooth normals blurred the creases into pillows.
    const double lx = LIGHT_X, ly = LIGHT_Y, lz = LIGHT_Z;
    const double lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
    const Vec3   Ld{lx / lLen, ly / lLen, lz / lLen};
    const bool   lit = shading > 0.0;

    // Area-weighted (unnormalised) and unit normal of every triangle.
    std::array<Vec3, PAPER_TRIANGLES> faceNormal{};
    std::array<Vec3, PAPER_TRIANGLES> faceUnit{};
    if (lit) {
        for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
            const auto   c = triangleCorners(tri);
            const Vec3   n = cross(world[c[1]] - world[c[0]], world[c[2]] - world[c[0]]);
            const double len = std::sqrt(dot(n, n));
            faceNormal[tri] = n;
            faceUnit[tri] = len >= 1e-12 ? n * (1.0 / len) : Vec3{};
        }
    }
    // Normal at `vertex` as seen by triangle `tri`.
    const auto cornerNormal = [&](std::size_t tri, std::size_t vertex) {
        std::array<std::size_t, 6> around{};
        const int                  count = vertexTriangles(int(vertex % GRID), int(vertex / GRID), around);
        Vec3                       sum{};
        for (int k = 0; k < count; ++k) {
            if (dot(faceUnit[around[k]], faceUnit[tri]) < CREASE_COS)
                continue;
            const Vec3 &n = faceNormal[around[k]];
            sum = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
        }
        return sum;
    };
    // Shade for a normal seen from the side `facing` (+1 front, -1 back) of the sheet.
    const auto shadeOf = [&](Vec3 n, double facing) {
        const double len = std::sqrt(dot(n, n));
        if (len < 1e-12)
            return 0.0;
        n = n * (facing / len);
        const double diff = dot(n, Ld) - Ld.z;
        const double s = diff >= 0.0 ? diff / (1.0 - Ld.z) : diff / (1.0 + Ld.z);
        return std::clamp(s * 1.8 * shading, -1.0, 1.0);
    };

    for (std::size_t slot = 0; slot < PAPER_TRIANGLES; ++slot) {
        const auto  corners = triangleCorners(order[slot].second);
        PaperFacet &facet = out[slot];
        for (std::size_t n = 0; n < 3; ++n) {
            facet.position[n] = screen[corners[n]];
            facet.uv[n] = gridUv(corners[n]);
        }
        facet.shade = {0.0, 0.0, 0.0};
        if (!lit)
            continue;
        // The side of the sheet this triangle shows decides which way its vertex normals point.
        const double facing = faceNormal[order[slot].second].z < 0.0 ? -1.0 : 1.0;
        for (std::size_t n = 0; n < 3; ++n)
            facet.shade[n] = shadeOf(cornerNormal(order[slot].second, corners[n]), facing);
    }
}

} // namespace liquid_glass
