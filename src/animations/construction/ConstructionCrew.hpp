#pragma once

#include "ConstructionArt.hpp"

namespace hyprimate::construction {

inline constexpr double WALK_END = .178;
// Authoring control: leave time after pickup for the free-hand signal before take-up.
inline constexpr double TOOL_PICKUP = .216;
inline constexpr double GUIDE_LATCH = .530;
inline constexpr double CREW_HEIGHT = 44;

struct CrewCues {
    Vec3 floor;  // Seam x, top of the staging plank y, and worker's base depth.
    double scale, time, seat, strike, release, retire;
    Vec3 guide, bolt;  // These are sockets on the evaluated load, not keyframed duplicates.
};

struct Limb {
    Vec3 root, joint, end;
};

struct CrewPose {
    Limb far_leg, near_leg, far_arm, near_arm;
    Vec3 hips, neck, far_fingers, near_fingers, hammer_grip, hammer_tip;
    double scale, lean, opacity;
};

CrewPose evaluate_crew(const CrewCues &cues) noexcept;
void draw_crew_far(ConstructionBatch &batch, const CrewPose &crew) noexcept;
void draw_crew_near(ConstructionBatch &batch, const CrewPose &crew) noexcept;

}  // namespace hyprimate::construction
