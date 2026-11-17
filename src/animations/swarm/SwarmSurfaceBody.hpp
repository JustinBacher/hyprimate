#pragma once

namespace hyprimate {
    // Below log(2) / (sqrt(2) - 1): two adjacent rounded boxes must fill their shared
    // outer corners instead of leaving a notch in the mass's exposed edge.
    inline constexpr double kSwarmSurfaceCornerScale = 1.6;

    // Analytic rounded box in standing-layer device pixels, streamed directly to the GPU.
    struct SwarmSurfaceBody {
        float centreX, centreY;
        float halfWidth, halfHeight;
        float cornerRadius;
    };
}  // namespace hyprimate
