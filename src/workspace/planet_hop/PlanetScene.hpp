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
    enum class Layout { Linear, SolarSystem, Dynamic };
    enum class DurationMode { Constant, DistanceScaled, Adaptive };

    // All distances are in desktop half-height units; angles are radians, time seconds.
    struct SceneOptions {
        Layout planetLayout = Layout::Linear;
        double planetSize = 1.0;
        double planetSeparation = 4.0;
        double planetCoverage = 0.7;
        double elevation = 0.2;
        double solarVerticalSpread = 0.65;
        bool rotationEnabled = true;
        double rotationLimit = 0.35;
        double duration = 0.8;
        DurationMode skipDurationMode = DurationMode::Adaptive;
        double distanceScale = 0.25;
        double maxDuration = 2.2;
        std::uint32_t adaptiveBoundary = 3;
    };

    struct Journey {
        JourneyEndpoint from;
        JourneyEndpoint to;
        SceneOptions options;
        Vec3 sourceCenter;
        Vec3 destinationCenter;
        double aspect = 1; // Effective render aspect used to place these cached anchors.
    };

    struct Frame {
        // All frame positions and matrices share a floating origin. Add worldOrigin only
        // when inspecting world motion; draw directly with these relative coordinates.
        Vec3 worldOrigin;
        Vec3 cameraPosition;
        Vec3 cameraTarget;
        Vec3 sourceCenter;
        Vec3 destinationCenter;
        // Column-major OpenGL matrices; camera aim leads the direct path, +Y remains up.
        std::array<float, 16> viewProjection{};
        std::array<float, 16> inverseViewProjection{};
        std::array<float, 9> sourceRotation{};
        std::array<float, 9> destinationRotation{};
        // Each desktop bends only at its own native endpoint; the remote planet stays round.
        double sourceCurvature = 0;
        double destinationCurvature = 1;
        // Global environmental fade only; material elevation/relief uses its planet's curvature.
        double endpointEnvelope = 0;
        double travelProgress = 0;
        double radius = 1;
        double coverage = 0.7;
        double aspect = 1;
    };

    // Cache this interleaved vertex grid by aspect/radius/coverage/quality, not per frame.
    // UV is top-left-origin. The flat desktop is at local z=radius, half-height=1.
    // Chart angles are bounded and preserve aspect at the center. A closed shell uses
    // make_surface_vertex(u, v, 2, radius, 1); its flat positions are not desktop geometry.
    struct SurfaceVertex {
        std::array<float, 3> flatPosition;
        std::array<float, 3> spherePosition;
        std::array<float, 2> uv;
    };
    struct SurfacePoint {
        Vec3 position;
        Vec3 normal;
    };

    // Aspect is the effective render width/height (not a rounded logical monitor size).
    // Subtract Frame::worldOrigin before drawing any returned absolute world anchor.
    [[nodiscard]] Vec3 layout_position(const JourneyEndpoint &, const SceneOptions & = {}, double aspect = 1);
    // Dynamic placement is chosen once here; solar anchors use the same aspect as evaluate.
    [[nodiscard]] Journey make_journey(JourneyEndpoint from, JourneyEndpoint to, SceneOptions = {}, double aspect = 1);
    [[nodiscard]] std::uint32_t logical_distance(const JourneyEndpoint &, const JourneyEndpoint &);
    [[nodiscard]] double duration_seconds(const JourneyEndpoint &, const JourneyEndpoint &, const SceneOptions & = {});
    // Pure/reversible: generic WorkspaceTurn owns time, velocity, reversal and queueing.
    [[nodiscard]] Frame evaluate(const Journey &, double progress, double aspect);
    [[nodiscard]] SurfaceVertex make_surface_vertex(double u, double v, double aspect, double radius, double coverage = 0.7);
    // CPU reference for the material vertex deformation, also useful for floating anchors.
    [[nodiscard]] SurfacePoint project_surface(double u, double v, double aspect, double radius,
        double coverage, double curvature, const std::array<float, 9> &rotation, Vec3 center = {}, double elevation = 0);
}
