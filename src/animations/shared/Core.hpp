#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace liquid_glass {

struct Vec2 {
    double x = 0;
    double y = 0;
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

enum class Transition { Open, Close };
enum class Edge { Left, Right, Top, Bottom };

// Any combination of the four screen edges.
struct EdgeSet {
    std::uint8_t   bits = 0;
    constexpr void add(Edge e) noexcept { bits |= static_cast<std::uint8_t>(1U << static_cast<unsigned>(e)); }
    constexpr bool has(Edge e) const noexcept { return ((bits >> static_cast<unsigned>(e)) & 1U) != 0; }
    constexpr bool empty() const noexcept { return bits == 0; }
    constexpr bool operator==(const EdgeSet &) const = default;
};

// Logical pixels, y down.
struct Box {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// True when every coordinate is finite and the box has positive extent.
inline bool isUsable(const Box &b) noexcept {
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.w) && std::isfinite(b.h) && b.w > 0.0 &&
           b.h > 0.0;
}

// Fixed simulation step shared by the liquid physics; frame time is consumed in whole substeps.
inline constexpr double kSubstepSeconds = 1.0 / 240.0;

// A stall is a gap between frames longer than an animation can play through. The threshold is defined here; each
// animation chooses its own response (abort, clamp or reset).
inline constexpr double kStallSeconds = 0.25;

// Render mesh resolution: LIQUID_RENDER_CELLS quads per side, sampled from the lattice by sampleDisplacement.
inline constexpr int LIQUID_RENDER_CELLS = 32;

// splitmix64 step: equal seeds give equal streams; neighbouring seeds give unrelated ones.
inline std::uint64_t splitmix64(std::uint64_t &state) noexcept {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform in [0, 1) from a splitmix64 stream: the top 53 bits.
inline double unitDraw(std::uint64_t &state) noexcept {
    return static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53;
}

// C1 smoothstep: 0 at a, 1 at b, clamped outside [a, b].
inline double smoothstep(double a, double b, double x) noexcept {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace liquid_glass
