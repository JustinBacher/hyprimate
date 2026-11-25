#pragma once

// Shared by the benchmark and the test-only compositor adapter. Inputs and
// outputs are distinct, correctly sized, x-fastest arrays; no GL state is used.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace hyprimate::compute::fixture {
    struct Vec2 { float x, y; };
    static_assert(sizeof(Vec2) == 2 * sizeof(float));

    inline std::uint32_t hash(std::uint32_t v) noexcept {
        v ^= v >> 16u; v *= 0x7feb352du;
        v ^= v >> 15u; v *= 0x846ca68bu;
        return v ^ (v >> 16u);
    }
    inline float random(std::uint32_t v) noexcept {
        return static_cast<float>(hash(v) >> 8u) * (1.0f / 16777216.0f);
    }
    inline void seed_stencil(std::span<float> field, unsigned width, unsigned height) {
        std::fill(field.begin(), field.end(), 0.0f);
        field[width / 3u + width * (height / 4u)] += 1.0f;
        field[2u * width / 3u + width * (3u * height / 4u)] += 0.6f;
    }
    inline void step_stencil(std::span<const float> previous, std::span<float> output,
                             unsigned width, unsigned height, float diffusion) {
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto i = static_cast<std::size_t>(y) * width + x;
                const float c = previous[i];
                const float left = x ? previous[i - 1] : 0.0f;
                const float right = x + 1 < width ? previous[i + 1] : 0.0f;
                const float down = y ? previous[i - width] : 0.0f;
                const float up = y + 1 < height ? previous[i + width] : 0.0f;
                output[i] = c + diffusion * (left + right + down + up - 4.0f * c);
            }
        }
    }
    inline void seed_particles(std::span<Vec2> positions, std::span<Vec2> velocities, float seed_x) {
        const auto seed = static_cast<std::uint32_t>(seed_x * 16777216.0f);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto k = static_cast<std::uint32_t>(i) ^ seed;
            positions[i] = {random(k), random(k ^ 0x9e3779b9u)};
            velocities[i] = {(random(k ^ 0x85ebca6bu) - 0.5f) * 0.2f,
                             (random(k ^ 0xc2b2ae35u) - 0.5f) * 0.2f};
        }
    }
    inline void step_particles(std::span<const Vec2> positions, std::span<const Vec2> velocities,
                               std::span<Vec2> next_positions, std::span<Vec2> next_velocities,
                               float acceleration, float delta) {
        const float dt = std::clamp(delta, 0.0f, 0.05f);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto p = positions[i], v = velocities[i];
            const Vec2 next{std::clamp(v.x + (0.38f - p.x) * acceleration * dt, -0.45f, 0.45f),
                            std::clamp(v.y + (0.61f - p.y) * acceleration * dt, -0.45f, 0.45f)};
            const float x = p.x + next.x * dt, y = p.y + next.y * dt;
            next_positions[i] = {x - std::floor(x), y - std::floor(y)};
            next_velocities[i] = next;
        }
    }
}
