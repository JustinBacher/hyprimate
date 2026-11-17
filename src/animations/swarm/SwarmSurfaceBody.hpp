#pragma once

namespace hyprimate {
    // Below log(2) / (sqrt(2) - 1): two adjacent rounded boxes must fill their shared
    // outer corners instead of leaving a notch in the mass's exposed edge.
    inline constexpr double K_SWARM_SURFACE_CORNER_SCALE = 1.6;

    // Analytic rounded box in standing-layer device pixels, streamed directly to the GPU.
    struct SwarmSurfaceBody {
        float centre_x, centre_y;
        float half_width, half_height;
        float corner_radius;
    };
}  // namespace hyprimate
