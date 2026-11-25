#pragma once

#include "config_settings_generated.hpp"
#include "window/shared/EdgeFrame.hpp"
#include "window/swarm/Swarm.hpp"

// The swarm settings as the simulation and the sprite layer take them.
template <typename SwarmConfig>
inline hyprimate::SwarmParams swarm_params(const SwarmConfig &swarm) {
    return {
        .count = swarm.count,
        .speed = swarm.speed,
        .stagger = swarm.stagger,
        .cohesion = swarm.cohesion,
        .separation = swarm.separation,
        .responsiveness = swarm.agility,
        .settling = swarm.settling,
        .wander = swarm.wander,
        .variation = swarm.variation,
    };
}

inline hyprimate::EdgeSet swarm_sides(
    hyprimate::config::Flags<hyprimate::config::LoneWindowSides> sides) {
    using hyprimate::Edge;
    using hyprimate::config::LoneWindowSides;
    hyprimate::EdgeSet out;
    if (sides.has(LoneWindowSides::Top)) {
        out.add(Edge::Top);
    }
    if (sides.has(LoneWindowSides::Bottom)) {
        out.add(Edge::Bottom);
    }
    if (sides.has(LoneWindowSides::Left)) {
        out.add(Edge::Left);
    }
    if (sides.has(LoneWindowSides::Right)) {
        out.add(Edge::Right);
    }
    return out;
}

