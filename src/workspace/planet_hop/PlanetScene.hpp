#pragma once

#include <array>
#include <cstdint>

#include "workspace/WorkspaceJourney.hpp"

namespace hyprimate::workspace::planet_hop {
    struct Vec3 {
        double x = 0;
        double y = 0;
        double z = 0;
    };
    enum class Layout { Focused, Monitor, All, SolarSystem };

    // All distances are in desktop half-height units; angles are radians, time seconds.
    struct SceneOptions {
        Layout planet_layout = Layout::Focused;
        double planet_size = 1.0;
        double planet_separation = 4.0;
        double planet_coverage = 0.7;
        double elevation = 0.2;
        double solar_vertical_spread = 0.65;
        bool rotation_enabled = true;
        double rotation_limit = 0.35;
    };

    struct Journey {
        JourneyEndpoint from;
        JourneyEndpoint to;
        SceneOptions options;
        Vec3 source_center;
        Vec3 destination_center;
        double aspect = 1;  // Effective render aspect; anchors use their owner's geometry.
        double neighborhood_width = 0;
        struct SolarPath {
            double from_angle = 0;
            double angle_delta = 0;
            double from_radius = 0;
            double to_radius = 0;
            double from_aspect = 1;
            double to_aspect = 1;
        } solar_path{};
    };

    struct Frame {
        // All frame positions and matrices share a floating origin. Add worldOrigin only
        // when inspecting world motion; draw directly with these relative coordinates.
        Vec3 world_origin;
        Vec3 camera_position;
        Vec3 camera_target;
        Vec3 source_center;
        Vec3 destination_center;
        // Column-major OpenGL matrices; camera aim leads the direct path, +Y remains up.
        std::array<float, 16> view_projection{};
        std::array<float, 16> inverse_view_projection{};
        std::array<float, 9> source_rotation{};
        std::array<float, 9> destination_rotation{};
        // Each desktop bends only at its own native endpoint; the remote planet stays round.
        double source_curvature = 0;
        double destination_curvature = 1;
        // Global environmental fade only; material elevation/relief uses its planet's curvature.
        double endpoint_envelope = 0;
        double travel_progress = 0;
        double radius = 1;
        double coverage = 0.7;
        double aspect = 1;
    };

    // Cache this interleaved vertex grid by aspect/radius/coverage/quality, not per frame.
    // UV is top-left-origin. The flat desktop is at local z=radius, half-height=1.
    // Chart angles are bounded and preserve aspect at the center. A closed shell uses
    // make_surface_vertex(u, v, 2, radius, 1); its flat positions are not desktop geometry.
    struct SurfaceVertex {
        std::array<float, 3> flat_position;
        std::array<float, 3> sphere_position;
        std::array<float, 2> uv;
    };
    struct SurfacePoint {
        Vec3 position;
        Vec3 normal;
    };

    // World anchors use captured owner geometry, never the viewer's render aspect.
    // Subtract Frame::worldOrigin before drawing any returned absolute world anchor.
    [[nodiscard]] Vec3 layout_position(
        const JourneyEndpoint &endpoint, const SceneOptions &options = {}, double aspect = 1);
    // Focused placement is chosen once here; other anchors remain topology-stable.
    [[nodiscard]] Journey make_journey(JourneyEndpoint from,
        JourneyEndpoint destination,
        SceneOptions options = {},
        double aspect = 1);
    // Membership is separate from whether a workspace currently has capturable pixels.
    [[nodiscard]] bool scene_member(const JourneyEndpoint &candidate,
        const JourneyEndpoint &from,
        const JourneyEndpoint &destination,
        Layout layout);
    // Pure/reversible: generic WorkspaceTurn owns time, velocity, reversal and queueing.
    [[nodiscard]] Frame evaluate(const Journey &journey, double progress, double aspect);
    // Relative travel estimate across departure, cruise and landing; no screen-space
    // or world-unit norm is shared by the camera, curvature and layer effects.
    [[nodiscard]] double remaining_travel(double progress, bool destination, bool solar);
    // Scene members may not cover a desktop while it bends back to native.
    // Center is already relative to the frame's floating origin.
    [[nodiscard]] bool neighbor_visible(const Frame &frame, Vec3 center, double radius);
    [[nodiscard]] SurfaceVertex make_surface_vertex(
        double coord_u, double coord_v, double aspect, double radius, double coverage = 0.7);
    struct SurfaceProjection {
        double u = 0;
        double v = 0;
        double aspect = 1;
        double radius = 1;
        double coverage = .7;
        double curvature = 0;
        std::array<float, 9> rotation{};
        Vec3 center{};
        double elevation = 0;
    };
    // CPU reference for the material vertex deformation, also useful for floating anchors.
    [[nodiscard]] SurfacePoint project_surface(const SurfaceProjection &projection);
}  // namespace hyprimate::workspace::planet_hop
