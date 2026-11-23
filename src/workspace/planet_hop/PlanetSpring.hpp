#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

#include <hyprutils/animation/Spring.hpp>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace::planet_hop {
    // A globe journey has two physical endpoints, not an unbounded scalar target. Fit the
    // native spring's rising response into that interval instead of clipping overshoot,
    // sitting on the desktop, and then taking off again when the spring crosses one.
    inline Transition::Curve spring_curve(
        Hyprutils::Animation::SSpringCurve spring, double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            return {};
        }
        // Match advanceSpring's lower physical bounds when computing the first turning point.
        const double MASS = std::max(spring.mass, .0001F);
        const double OMEGA_SQUARED = std::max(spring.stiffness, .0001F) / MASS;
        const double DECAY = spring.damping / (2.0 * MASS);
        const double FREQUENCY_SQUARED = OMEGA_SQUARED - (DECAY * DECAY);
        double span = FREQUENCY_SQUARED > 0
                          ? std::min(seconds, std::numbers::pi / std::sqrt(FREQUENCY_SQUARED))
                          : seconds;
        const double OMEGA = std::sqrt(OMEGA_SQUARED);
        if (std::max(OMEGA, DECAY) * span < 1e-3) {
            // The native float response subtracts nearly equal values at takeoff.
            // Normalize its short-time expansion before that cancellation can erase it.
            const double CUBIC = (2.0 / 3.0) * DECAY * span;
            return
                [CUBIC](double time) { return time * time * (1.0 - CUBIC * time) / (1.0 - CUBIC); };
        }
        const auto RESPONSE = [spring](double elapsed) {
            float value = 0;
            float velocity = 0;
            Hyprutils::Animation::advanceSpring(
                value, velocity, spring, std::chrono::duration<float>{static_cast<float>(elapsed)});
            return static_cast<double>(value);
        };
        // An overdamped/critical spring can settle in a small fraction of a long
        // configured duration. Stretch its approach, not the stationary tail.
        double end = 0.0;
        if (FREQUENCY_SQUARED <= 0) {
            const double SETTLED = 1.0 - spring.valueEpsilon;
            const double DEADLINE = span;
            double lower = 0.0;
            span = std::min(DEADLINE, 1.0 / OMEGA);
            end = RESPONSE(span);
            // Bracket settling near the spring's own timescale, not at the
            // configured deadline, which can be orders of magnitude longer.
            while (span < DEADLINE && end < SETTLED) {
                lower = span;
                span = std::min(DEADLINE, 2.0 * span);
                end = RESPONSE(span);
            }
            if (end >= SETTLED) {
                for (int step = 0; step < 32; ++step) {
                    const double MID = (lower + span) * .5;
                    const double VALUE = RESPONSE(MID);
                    if (VALUE < SETTLED) {
                        lower = MID;
                    } else {
                        span = MID;
                        end = VALUE;
                    }
                }
            }
        } else {
            end = RESPONSE(span);
        }
        if (!std::isfinite(end) || end <= 0) {
            return {};
        }
        return [RESPONSE, span, END = end](
                   double time) { return std::clamp(RESPONSE(span * time) / END, 0.0, 1.0); };
    }
}  // namespace hyprimate::workspace::planet_hop
