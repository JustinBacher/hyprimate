#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "shared/Core.hpp"

namespace hyprimate::bubble_motion_detail {
    inline constexpr double PI_VALUE = std::numbers::pi, TAU = 2 * PI_VALUE;
    inline constexpr std::array<int, 4> MODES{2, 3, 4, 5};
    // The trailing fluid: one damped mode, underdamped enough to overshoot once when the window
    // stops (about 16%) and quiet again within roughly half a second.
    inline constexpr double LAG_FREQUENCY = TAU * 1.9, LAG_DAMPING = .5;
    inline constexpr double LEAD_FLATTEN = .3;
    // Capillary waves on the outline: n = 2 rings at this frequency; higher modes follow the
    // dispersion of a 2D drop, w_n^2 ~ n^3 - n, and lose energy faster.
    inline constexpr double WAVE_FREQUENCY = TAU * 2.3;
    // Interior pressure, the mode the opening's fluid core settles through.
    inline constexpr double CORE_PRESSURE_FREQUENCY = TAU * 2.6, PRESSURE_DAMPING = .32;
    // Pointer-driven positions arrive unevenly; two smoothing stages keep that jitter out of
    // the acceleration.
    inline constexpr double VELOCITY_SMOOTHING = .03;
    inline constexpr double MAX_SPEED = 6000;
    // Below this speed the film draws out no strands.
    inline constexpr double TRAIL_SPEED = 140;
    // Below this speed a moving window counts as paused: its shape holds briefly, then spreads.
    inline constexpr double PAUSE_SPEED = 30;
    // The film behind a layout-animated window closes in on it critically damped: it eases out of
    // its old place and arrives within about half a second, whatever the layout's own curve.
    inline constexpr double CARRY_FREQUENCY = 9;
    // Beyond this the film is not left behind: a farther jump moves it mostly with the window.
    inline constexpr double MAX_CARRY = 2400;
    inline constexpr double PILLOW_ROUND = .5, PILLOW_EXTENT = .92;
    inline constexpr double BEHIND_TURN = .12;
    inline constexpr double FILM_INHERIT = .3;

    inline Vec2 add(Vec2 lhs, Vec2 rhs) {
        return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
    }
    inline Vec2 sub(Vec2 lhs, Vec2 rhs) {
        return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
    }
    inline Vec2 mul(Vec2 vector, double scale) {
        return {.x = vector.x * scale, .y = vector.y * scale};
    }
    inline double dot(Vec2 lhs, Vec2 rhs) {
        return (lhs.x * rhs.x) + (lhs.y * rhs.y);
    }
    inline double length(Vec2 lhs) {
        return std::hypot(lhs.x, lhs.y);
    }
    inline bool finite(Vec2 lhs) {
        return std::isfinite(lhs.x) && std::isfinite(lhs.y);
    }
    // The outline quadrupole (cos 2phi, sin 2phi) that elongates a bubble along unit n.
    inline Vec2 mode(Vec2 direction) {
        return {.x = (direction.x * direction.x) - (direction.y * direction.y),
            .y = 2 * direction.x * direction.y};
    }
    // Magnitude-preserving soft limit: linear for small values, never beyond limit.
    inline Vec2 soften(Vec2 value, double limit) {
        const double SPEED = length(value);
        if (!(limit > 0) || !(SPEED > 1e-12)) {
            return {};
        }
        return mul(value, limit * std::tanh(SPEED / limit) / SPEED);
    }
    inline double mode_frequency(double mode) {
        return WAVE_FREQUENCY * std::sqrt(((mode * mode * mode) - mode) / 6.0);
    }
    inline double mode_decay(std::size_t index) {
        return 3.5 + (1.6 * static_cast<double>(index));
    }
    // 0..1 how sharply the window is turning: acceleration across its direction of travel.
    inline double turning(Vec2 velocity, double speed, Vec2 acceleration) {
        if (!(speed > 1e-6)) {
            return 0;
        }
        const Vec2 DIR = mul(velocity, 1 / speed);
        return std::clamp(
            std::abs((DIR.x * acceleration.y) - (DIR.y * acceleration.x)) / 12000, 0.0, 1.0);
    }
    // Outward direction of the rounded membrane (half size h, roundness `round`) at q: a
    // rounded rectangle's normal turning into an ellipse's. Not normalised; zero at the centre.
    inline Vec2 outward(Vec2 half_size, double roundness, Vec2 point) {
        const Vec2 UNIT{.x = point.x / half_size.x, .y = point.y / half_size.y};
        const Vec2 SQUARE{.x = UNIT.x * UNIT.x * UNIT.x / half_size.x,
            .y = UNIT.y * UNIT.y * UNIT.y / half_size.y};
        const Vec2 CIRCLE{.x = UNIT.x / half_size.x, .y = UNIT.y / half_size.y};
        const double SQUARE_LENGTH = length(SQUARE);
        const double CIRCLE_LENGTH = length(CIRCLE);
        if (!(SQUARE_LENGTH > 1e-12) || !(CIRCLE_LENGTH > 1e-12)) {
            return {};
        }
        return add(mul(SQUARE, (1 - roundness) / SQUARE_LENGTH),
            mul(CIRCLE, roundness / CIRCLE_LENGTH));
    }
    // Advances an outline quadrupole oscillator toward target; stable for any step.
    inline void ring(Vec2 &squash,
        Vec2 &rate,
        Vec2 target,
        double frequency,
        double damping,
        double elapsed) {
        const int STEPS = std::max(1, static_cast<int>(std::ceil(frequency * elapsed / .2)));
        const double STEP = elapsed / STEPS;
        for (int i = 0; i < STEPS; ++i) {
            rate = add(rate,
                mul(add(mul(sub(squash, target), -frequency * frequency),
                        mul(rate, -2 * damping * frequency)),
                    STEP));
            squash = add(squash, mul(rate, STEP));
        }
    }
}  // namespace hyprimate::bubble_motion_detail
