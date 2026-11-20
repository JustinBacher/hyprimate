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
}  // namespace hyprimate::workspace::planet_hop
