#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    enum class PaperOrigin : std::uint8_t { Center, Random, Cursor, Left, Right, Top, Bottom };
    enum class PaperForce : std::uint8_t { Inward, Directional, Rotational };
    enum class PaperQuality : std::uint8_t { Adaptive, Low, Medium, High };
    struct PaperParams {
        double duration = 0.5, crumpliness = 0.6, shading = 0.7, variation = 0.5;
        double fold_density = 0.55, paper_stiffness = 0.72, crumple_size = 0.18;
        double rotation_intensity = 0.22, unfold_tension = 0.62, settling = 0.45,
               crease_persistence = 0.62;
        PaperOrigin crumple_origin = PaperOrigin::Center;
        PaperForce force_direction = PaperForce::Inward;
        PaperQuality simulation_quality = PaperQuality::Adaptive;
    };
    // Positions relative to capture centre, x right, y down, z towards the viewer;
    // one unit is sqrt(rest width * rest height). UV is immutable material space, top-left (0,0).
    inline constexpr double PAPER_PATCH_BULGE =
        0.006;  // maximum reconstruction displacement in these units
    struct PaperPoint {
        Vec3 position{};
        Vec2 uv{};
        bool contact = false;
    };
    struct PaperTriangle {
        std::array<std::uint32_t, 3> vertex{};
        // At each edge (i,i+1): 0 = smooth, 1 = a sharp physical hinge.
        std::array<float, 3> crease{};
    };
    struct PaperSurface {
        std::vector<PaperPoint> points;
        std::vector<PaperTriangle> faces;
        Vec2 size{};
        float thickness =
            0.00516F;  // separation used by the contact solver, in material-space units
        float opacity = 1, shading = 0.7F;
    };
}  // namespace hyprimate
