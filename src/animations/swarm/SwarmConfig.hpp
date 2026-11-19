#pragma once

#include "AnimationTransformer.hpp"

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

template <typename SwarmConfig>
inline const std::string &swarm_creature(const SwarmConfig &swarm) {
    return swarm.sprite;
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

inline hyprimate::EdgeSet swarm_sides(const hyprimate::EdgeInput &input,
    hyprimate::config::Flags<hyprimate::config::LoneWindowSides> sides) {
    return input.last_window ? swarm_sides(sides) :
        hyprimate::resolve_edge_sides(input.body, input.screen, input.neighbours);
}
