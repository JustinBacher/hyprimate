#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

#include <hyprutils/animation/Spring.hpp>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {
    // Native springs have no speed field. Stop a bounded journey at the first
    // turning point, or when the physical response has settled, whichever is earlier.
    inline double spring_travel_seconds(const Hyprutils::Animation::SSpringCurve &spring) {
        const double MASS = std::max(spring.mass, .0001F);
        const double DECAY = std::max(spring.damping, 0.F) / (2.0 * MASS);
        const double OMEGA2 = (std::max(spring.stiffness, .0001F) / MASS) - (DECAY * DECAY);
        const double LIMIT = OMEGA2 > 0 ? std::numbers::pi / std::sqrt(OMEGA2) : 86400.0;
        const auto SETTLED = [&](double seconds) {
            float value = 0;
            float velocity = 0;
            Hyprutils::Animation::advanceSpring(
                value, velocity, spring, std::chrono::duration<float>{static_cast<float>(seconds)});
            return std::abs(1.F - value) <= spring.valueEpsilon &&
                   std::abs(velocity) <= spring.velocityEpsilon;
        };
        double lower = 0;
        double upper = std::min(.125, LIMIT);
        while (!SETTLED(upper)) {
            if (upper >= LIMIT) {
                return LIMIT;
            }
            lower = upper;
            upper = std::min(2 * upper, LIMIT);
        }
        for (int step = 0; step < 24; ++step) {
            const double MID = (lower + upper) * .5;
            if (SETTLED(MID)) {
                upper = MID;
            } else {
                lower = MID;
            }
        }
        return upper;
    }

    // A journey has two physical endpoints, not an unbounded scalar target. Fit
    // the rising response into that interval instead of clipping overshoot,
    // sitting on the desktop, and then taking off again when the spring crosses one.
    inline Transition::Curve spring_curve(
        Hyprutils::Animation::SSpringCurve spring, double seconds) {
        // Match advanceSpring's lower physical bounds when computing the first turning point.
        const double MASS = std::max(spring.mass, .0001F);
        const double OMEGA_SQUARED = std::max(spring.stiffness, .0001F) / MASS;
        const double DECAY = spring.damping / (2.0 * MASS);
        const double FREQUENCY_SQUARED = OMEGA_SQUARED - (DECAY * DECAY);
        double span = FREQUENCY_SQUARED > 0
                          ? std::min(seconds, std::numbers::pi / std::sqrt(FREQUENCY_SQUARED))
                          : seconds;
        const auto RESPONSE = [spring](double elapsed) {
            float value = 0;
            float velocity = 0;
            Hyprutils::Animation::advanceSpring(
                value, velocity, spring, std::chrono::duration<float>{static_cast<float>(elapsed)});
            return static_cast<double>(value);
        };
        // An overdamped/critical spring can settle in a small fraction of a long
        // configured duration. Stretch its approach, not the stationary tail.
        if (FREQUENCY_SQUARED <= 0 && RESPONSE(span) >= 1.0 - spring.valueEpsilon) {
            double lower = 0;
            for (int step = 0; step < 32; ++step) {
                const double MID = (lower + span) * .5;
                if (RESPONSE(MID) < 1.0 - spring.valueEpsilon) {
                    lower = MID;
                } else {
                    span = MID;
                }
            }
        }
        const double END = RESPONSE(span);
        if (!std::isfinite(END) || END <= 0) {
            return {};
        }
        return [RESPONSE, span, END](
                   double time) { return std::clamp(RESPONSE(span * time) / END, 0.0, 1.0); };
    }
}  // namespace hyprimate::workspace
