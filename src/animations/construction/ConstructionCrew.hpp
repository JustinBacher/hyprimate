#pragma once

#include "ConstructionArt.hpp"

namespace hyprimate::construction {

// Shared job-local cues: the walk ends before the panel lowers onto the sill.
inline constexpr double CARRY_WALK_END = .49;
inline constexpr double GUIDE_LATCH = CARRY_WALK_END;
inline constexpr double PANEL_SEAT = .66;
inline constexpr double FASTENER_STRIKE = .75;
inline constexpr double WELD_COMPLETE = FASTENER_STRIKE + .085;
inline constexpr double CREW_HEIGHT = 44;

enum class CrewJob { Carry, Guide, Fasten, Brace, Weld };

struct CrewLadder {
    Vec3 base{}, top{}; // Supported centers of the bottom and top of the rails.
};

struct CrewCues {
    Vec3 floor; // Final stance anchor, with y at the top of the ledge.
    Vec3 far_contact{}, near_contact{}; // Actual sockets on the posed load; Fasten uses near_contact for the bolt.
    CrewJob job;
    double facing, approach; // Facing sign and signed world-space offset at the start of the walk.
    double arrival, work, departure; // Walk progress, job-local time, retreat progress.
    double hold_until = PANEL_SEAT; // Release cue for the supporting grip in job-local time.
    double cadence = 1; // Gait bias, independent of the load's shared travel distance.
    CrewLadder ladder{}; // Optional: near/far work soles use rungs 3/6 and 4/6.
    bool aim_contacts = false; // Opt in to side/overhead socket alignment.
    bool steady_during_finish = false; // Fasten/Weld keep the far glove on far_contact.
};

// Shared by the carrying walk and the load so neither can get ahead of the other.
inline double walk_offset(double approach, double arrival) noexcept {
    return approach * (1 - smoothstep(0, 1, arrival));
}

void draw_crew(ConstructionBatch &batch, const CrewCues &cues) noexcept;

}  // namespace hyprimate::construction
