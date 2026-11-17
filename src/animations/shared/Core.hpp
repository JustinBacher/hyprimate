#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hyprimate {

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
        std::uint8_t bits = 0;
        constexpr void add(Edge edge) noexcept {
            bits |= static_cast<std::uint8_t>(1U << static_cast<unsigned>(edge));
        }
        [[nodiscard]] constexpr bool has(Edge edge) const noexcept {
            return ((static_cast<unsigned>(bits) >> static_cast<unsigned>(edge)) & 1U) != 0;
        }
        [[nodiscard]] constexpr bool empty() const noexcept {
            return bits == 0;
        }
        constexpr bool operator==(const EdgeSet &) const = default;
    };

    // Logical pixels, y down.
    struct Box {
        double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    };

    // True when every coordinate is finite and the box has positive extent.
    inline bool is_usable(const Box &box) noexcept {
        return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
               std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
    }

    // Fixed simulation step shared by the liquid physics; frame time is consumed in whole substeps.
    inline constexpr double SUBSTEP_SECONDS = 1.0 / 240.0;

    // A stall is a gap between frames longer than an animation can play through. The threshold is
    // defined here; each animation chooses its own response (abort, clamp or reset).
    inline constexpr double STALL_SECONDS = 0.25;

    // Render mesh resolution: LIQUID_RENDER_CELLS quads per side, sampled from the lattice by
    // sampleDisplacement.
    inline constexpr int LIQUID_RENDER_CELLS = 32;

    // splitmix64 step: equal seeds give equal streams; neighbouring seeds give unrelated ones.
    inline std::uint64_t splitmix64(std::uint64_t &state) noexcept {
        std::uint64_t mixed = (state += 0x9E3779B97F4A7C15ULL);
        mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
        return mixed ^ (mixed >> 31U);
    }

    // Uniform in [0, 1) from a splitmix64 stream: the top 53 bits.
    inline double unit_draw(std::uint64_t &state) noexcept {
        return static_cast<double>(splitmix64(state) >> 11U) * 0x1.0p-53;
    }

    // C1 smoothstep: 0 at lower_bound, 1 at upper_bound, clamped outside that interval.
    inline double smoothstep(double lower_bound, double upper_bound, double value) noexcept {
        const double FRACTION =
            std::clamp((value - lower_bound) / (upper_bound - lower_bound), 0.0, 1.0);
        return FRACTION * FRACTION * (3.0 - (2.0 * FRACTION));
    }

}  // namespace hyprimate
