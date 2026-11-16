#pragma once

#include "AnimationTransformer.hpp"

// The swarm settings as the simulation and the sprite layer take them.
inline hyprimate::SwarmParams swarmParams(const hyprimate::config::SConfig &config) {
    const auto &swarm = config.swarm;
    return {.count = swarm.count,
        .speed = swarm.speed,
        .stagger = swarm.stagger,
        .cohesion = swarm.cohesion,
        .separation = swarm.separation,
        .responsiveness = swarm.responsiveness,
        .settling = swarm.settling,
        .wander = swarm.wander,
        .variation = swarm.variation};
}

inline hyprimate::SwarmCreature swarmCreature(const hyprimate::config::SConfig &config) {
    switch (config.swarm.creature) {
        case hyprimate::config::Creature::Spider:
            return hyprimate::SwarmCreature::Spider;
        case hyprimate::config::Creature::Cat:
            return hyprimate::SwarmCreature::Cat;
    }
    return hyprimate::SwarmCreature::Spider;
}

inline hyprimate::EdgeSet swarmSides(
    hyprimate::config::Flags<hyprimate::config::LoneWindowSides> sides) {
    using hyprimate::Edge;
    using hyprimate::config::LoneWindowSides;
    hyprimate::EdgeSet out;
    if (sides.has(LoneWindowSides::Top)) out.add(Edge::Top);
    if (sides.has(LoneWindowSides::Bottom)) out.add(Edge::Bottom);
    if (sides.has(LoneWindowSides::Left)) out.add(Edge::Left);
    if (sides.has(LoneWindowSides::Right)) out.add(Edge::Right);
    return out;
}
