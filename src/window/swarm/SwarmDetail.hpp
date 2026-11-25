#pragma once

#include <cmath>

#include "window/swarm/Swarm.hpp"

namespace hyprimate::swarm_detail {

    inline constexpr double STEP = 1.0 / 120.0;
    inline constexpr double SPEED_CAP = 1.45;  // top speed over cruise
    inline constexpr double REST_RADIUS =
        0.7;  // spacings from home inside which an agent swings to its rest direction
    inline constexpr double MERGE_MIN = 0.18;  // s to merge into the window, by settling
    inline constexpr double SPLIT_TIME = 0.14;  // s to shrink from tile to creature
    inline constexpr double CORNER_TIME = 0.18;  // s for the global surface rounding to change

    inline Vec2 operator+(Vec2 lhs, Vec2 rhs) noexcept {
        return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
    }
    inline Vec2 operator-(Vec2 lhs, Vec2 rhs) noexcept {
        return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
    }
    inline Vec2 operator*(Vec2 vector, double scale) noexcept {
        return {.x = vector.x * scale, .y = vector.y * scale};
    }
    inline double length(Vec2 vector) noexcept {
        return std::hypot(vector.x, vector.y);
    }
    inline double lerp(double start, double end, double fraction) noexcept {
        return start + ((end - start) * fraction);
    }

    inline void release_agent(SwarmAgent &agent) noexcept {
        agent.visible = true;
        if (agent.leaving && agent.docked) {
            agent.docked = false;
            agent.heading =
                std::atan2(agent.exit.y - agent.position.y, agent.exit.x - agent.position.x);
        }
    }

}  // namespace hyprimate::swarm_detail
