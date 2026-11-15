#pragma once

#include "AnimationTransformer.hpp"

// The swarm settings as the simulation and the sprite layer take them.
inline liquid_glass::SwarmParams swarmParams(const liquid_glass::config::SConfig &config) {
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

inline liquid_glass::SwarmCreature swarmCreature(const liquid_glass::config::SConfig &config) {
    switch (config.swarm.creature) {
    case liquid_glass::config::Creature::Spider:
        return liquid_glass::SwarmCreature::Spider;
    case liquid_glass::config::Creature::Cat:
        return liquid_glass::SwarmCreature::Cat;
    }
    return liquid_glass::SwarmCreature::Spider;
}

inline liquid_glass::EdgeSet swarmSides(liquid_glass::config::Flags<liquid_glass::config::LoneWindowSides> sides) {
    using liquid_glass::Edge;
    using liquid_glass::config::LoneWindowSides;
    liquid_glass::EdgeSet out;
    if (sides.has(LoneWindowSides::Top))
        out.add(Edge::Top);
    if (sides.has(LoneWindowSides::Bottom))
        out.add(Edge::Bottom);
    if (sides.has(LoneWindowSides::Left))
        out.add(Edge::Left);
    if (sides.has(LoneWindowSides::Right))
        out.add(Edge::Right);
    return out;
}
