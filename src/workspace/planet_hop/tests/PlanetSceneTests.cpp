#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Check.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

using namespace hyprimate::workspace::planet_hop;
using hyprimate::workspace::JourneyEndpoint;

namespace planet_scene_layout_tests {
    void layouts_are_deterministic_and_locked();
    void topology_scope_and_geometry_are_independent();
    void projected_topology_preserves_native_handoff();
    void origin_and_named_handoffs_have_no_foreground_inventory();
    void solar_packing_has_no_overlap();
    void solar_paths_preserve_gaps_and_reveal_real_intermediates();
    void named_solar_cells_are_local_stable_and_disjoint();
    void distant_planets_keep_stable_coordinates_and_framing();
    void placements_survive_recreation_and_resolve_collisions();
}

namespace planet_scene_surface_tests {
    void geometry_has_native_endpoints();
    void projection_is_spherical_and_aspect_correct();
    void angular_bend_is_convex_and_stable();
    void angular_bend_normals_match_tangents_and_rotation();
}

namespace {
    constexpr std::array<float, 9> IDENTITY{1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool close(double first, double second, double tolerance = 1e-5) {
        return std::abs(first - second) < tolerance;
    }
    bool close(Vec3 first, Vec3 second, double tolerance = 1e-5) {
        return close(first.x, second.x, tolerance) && close(first.y, second.y, tolerance) &&
               close(first.z, second.z, tolerance);
    }
    double length(Vec3 value) {
        return std::hypot(value.x, value.y, value.z);
    }
    std::array<double, 4> transform(
        const std::array<float, 16> &matrix, std::array<double, 4> point) {
        std::array<double, 4> result{};
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                result[row] += matrix[(column * 4) + row] * point[column];
            }
        }
        return result;
    }
    JourneyEndpoint numbered(std::int64_t identity, std::uint32_t number, std::uint32_t rank) {
        return {.identity = identity,
            .numbered = number,
            .special = false,
            .logical_order = rank,
            .placement = static_cast<std::uint64_t>(number) * 2};
    }

    void owner_extents_have_lattice_clearance() {
        constexpr std::array<double, 5> ASPECTS{1, 2, .5, 4, 8};
        const auto endpoint = [](std::uint64_t slot, double aspect, double offset) {
            return JourneyEndpoint{.identity = static_cast<std::int64_t>(slot + 1),
                .placement = slot,
                .owner = slot + 1,
                .monitor_x = offset,
                .monitor_y = -offset,
                .monitor_width = 1080 * aspect,
                .monitor_height = 1080};
        };
        const auto separated = [](const JourneyEndpoint &first,
                                   const JourneyEndpoint &second,
                                   const SceneOptions &options) {
            const auto A = layout_position(first, options);
            const auto B = layout_position(second, options);
            const double DIAMETER = 2 * (options.planet_size + std::abs(options.elevation));
            return std::hypot(A.x - B.x, A.y - B.y, A.z - B.z) >= DIAMETER - 1e-9;
        };
        for (const auto LAYOUT : {Layout::Monitor, Layout::All, Layout::SolarSystem}) {
            SceneOptions options;
            options.planet_layout = LAYOUT;
            options.planet_separation = .1;  // Exercise the minimum clearance.
            options.solar_vertical_spread = 0;
            CHECK(separated(endpoint(1, 2, 0), endpoint(7, 1, 0), options));
            for (std::uint64_t first = 0; first < 16; ++first) {
                for (std::uint64_t second = first + 1; second < 16; ++second) {
                    for (const double FIRST_ASPECT : ASPECTS) {
                        for (const double SECOND_ASPECT : ASPECTS) {
                            for (const double OFFSET : {-10000., 0., 10000.}) {
                                CHECK(separated(endpoint(first, FIRST_ASPECT, OFFSET),
                                    endpoint(second, SECOND_ASPECT, -OFFSET),
                                    options));
                            }
                        }
                    }
                }
            }
        }
    }

    void motion_is_continuous_and_direct() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        const auto JOURNEY = make_journey(numbered(10, 1, 0), numbered(30, 3, 2), options);
        const auto FIRST = evaluate(JOURNEY, 0, 1.7);
        const auto LAST = evaluate(JOURNEY, 1, 1.7);
        const double EPSILON = 1e-5;
        const auto NEAR_FIRST = evaluate(JOURNEY, EPSILON, 1.7);
        const auto NEAR_LAST = evaluate(JOURNEY, 1 - EPSILON, 1.7);
        CHECK(close(FIRST.camera_position, NEAR_FIRST.camera_position, 1e-6));
        CHECK(close(LAST.camera_position, NEAR_LAST.camera_position, 1e-6));
        CHECK(close(NEAR_FIRST.source_curvature / EPSILON, 0, .001));
        CHECK(close(NEAR_LAST.destination_curvature / EPSILON, 0, .001));
        CHECK(close(NEAR_FIRST.source_rotation[2] / EPSILON, 0, .001));
        double previous = -1;
        for (int step = 0; step <= 100; ++step) {
            const double PROGRESS = step / 100.;
            const auto FRAME = evaluate(JOURNEY, PROGRESS, 1.7);
            CHECK(FRAME.travel_progress >= previous);
            previous = FRAME.travel_progress;
            const auto &PATH = JOURNEY.solar_path;
            const double NORMALIZED_X =
                FRAME.world_origin.x /
                std::lerp(PATH.from_aspect, PATH.to_aspect, FRAME.travel_progress);
            const double NORMALIZED_Y = FRAME.world_origin.y / (1 + options.solar_vertical_spread);
            CHECK(close(std::hypot(NORMALIZED_X, NORMALIZED_Y),
                std::lerp(PATH.from_radius, PATH.to_radius, FRAME.travel_progress)));
            const Vec3 DIRECTION{.x = JOURNEY.destination_center.x - JOURNEY.source_center.x,
                .y = JOURNEY.destination_center.y - JOURNEY.source_center.y,
                .z = 0};
            CHECK(
                (FRAME.camera_target.x * DIRECTION.x) + (FRAME.camera_target.y * DIRECTION.y) >= 0);
            if (step == 50) {
                CHECK(length(FRAME.camera_target) > 0);
                CHECK(std::abs(FRAME.view_projection[2]) + std::abs(FRAME.view_projection[6]) > 0);
            }
            // Identical progress during reversal reproduces every visible property exactly.
            const auto REVISITED = evaluate(JOURNEY, PROGRESS, 1.7);
            CHECK(REVISITED.view_projection == FRAME.view_projection);
            CHECK(REVISITED.source_rotation == FRAME.source_rotation);
            CHECK(REVISITED.source_curvature == FRAME.source_curvature);
            CHECK(REVISITED.destination_curvature == FRAME.destination_curvature);
            const std::array<double, 4> POINT{.1, -.2, .3, 1};
            const auto RECOVERED =
                transform(FRAME.inverse_view_projection, transform(FRAME.view_projection, POINT));
            for (int component = 0; component < 4; ++component) {
                CHECK(close(RECOVERED[component], POINT[component], .003));
            }
        }
        CHECK(close(evaluate(JOURNEY, -5, 1.7).camera_position, FIRST.camera_position));
        CHECK(close(evaluate(JOURNEY, 5, 1.7).camera_position, LAST.camera_position));
        CHECK(
            close(evaluate(JOURNEY, std::numeric_limits<double>::quiet_NaN(), 1.7).camera_position,
                FIRST.camera_position));
        options.rotation_enabled = false;
        const auto NO_ROTATION = evaluate(make_journey(JOURNEY.from, JOURNEY.to, options), .5, 1);
        CHECK(NO_ROTATION.source_rotation == IDENTITY);
        CHECK(NO_ROTATION.destination_rotation == IDENTITY);
    }




    void named_and_special_direction_matches_navigation() {
        SceneOptions options;
        options.planet_layout = Layout::Focused;
        const JourneyEndpoint NAMED_A{
            .identity = 100, .numbered = {}, .special = false, .logical_order = 0};
        const JourneyEndpoint NAMED_B{
            .identity = 1, .numbered = {}, .special = false, .logical_order = 0};
        const JourneyEndpoint SPECIAL{
            .identity = -1, .numbered = {}, .special = true, .logical_order = 0};
        const auto NUMBERED = numbered(20, 20, 3);
        CHECK(make_journey(NAMED_A, NAMED_B, options).destination_center.x > 0);
        CHECK(make_journey(NUMBERED, SPECIAL, options).destination_center.x > 0);
        CHECK(make_journey(SPECIAL, NUMBERED, options).destination_center.x < 0);
        CHECK(evaluate(make_journey(NAMED_A, NAMED_B, options), .5, 1).source_rotation[2] < 0);
        CHECK(evaluate(make_journey(SPECIAL, NUMBERED, options), .5, 1).source_rotation[2] > 0);
    }

    void bending_precedes_reveal_and_approach_grows() {
        const auto JOURNEY = make_journey(numbered(1, 1, 0), numbered(2, 2, 1));
        const auto NATIVE = evaluate(JOURNEY, 0, 1.7);
        const auto BENDING = evaluate(JOURNEY, .10, 1.7);
        const auto EARLY_REVEAL = evaluate(JOURNEY, .16, 1.7);
        const auto REVEAL = evaluate(JOURNEY, .32, 1.7);
        CHECK(close(BENDING.world_origin, NATIVE.world_origin));
        CHECK(BENDING.camera_position.z > NATIVE.camera_position.z);
        CHECK(BENDING.source_curvature > 0);
        CHECK(EARLY_REVEAL.source_curvature == 1);
        CHECK(EARLY_REVEAL.travel_progress < .01);
        CHECK(EARLY_REVEAL.camera_position.z - NATIVE.camera_position.z <
              REVEAL.camera_position.z - NATIVE.camera_position.z);
        CHECK(REVEAL.source_curvature == 1);
        CHECK(REVEAL.destination_curvature == 1);
        double previous_distance = std::numeric_limits<double>::infinity();
        double previous_altitude = previous_distance;
        double previous_angular_radius = 0;
        for (int step = 32; step <= 100; ++step) {
            const auto FRAME = evaluate(JOURNEY, step / 100., 1.7);
            const double DISTANCE =
                length({.x = FRAME.destination_center.x - FRAME.camera_position.x,
                    .y = FRAME.destination_center.y - FRAME.camera_position.y,
                    .z = FRAME.destination_center.z - FRAME.camera_position.z});
            const double ANGULAR_RADIUS = std::asin(FRAME.radius / DISTANCE);
            CHECK(DISTANCE <= previous_distance);
            CHECK(FRAME.camera_position.z <= previous_altitude);
            CHECK(ANGULAR_RADIUS >= previous_angular_radius);
            previous_distance = DISTANCE;
            previous_altitude = FRAME.camera_position.z;
            previous_angular_radius = ANGULAR_RADIUS;
        }
        double previous_source_angle = std::numeric_limits<double>::infinity();
        double previous_destination_angle = 0;
        for (int step = 32; step <= 68; ++step) {
            const auto FRAME = evaluate(JOURNEY, step / 100., 1.7);
            const double SOURCE_DISTANCE =
                length({.x = FRAME.source_center.x - FRAME.camera_position.x,
                    .y = FRAME.source_center.y - FRAME.camera_position.y,
                    .z = FRAME.source_center.z - FRAME.camera_position.z});
            const double DESTINATION_DISTANCE =
                length({.x = FRAME.destination_center.x - FRAME.camera_position.x,
                    .y = FRAME.destination_center.y - FRAME.camera_position.y,
                    .z = FRAME.destination_center.z - FRAME.camera_position.z});
            const double SOURCE_ANGLE = std::asin(FRAME.radius / SOURCE_DISTANCE);
            const double DESTINATION_ANGLE = std::asin(FRAME.radius / DESTINATION_DISTANCE);
            CHECK(SOURCE_ANGLE <= previous_source_angle);
            CHECK(DESTINATION_ANGLE >= previous_destination_angle);
            CHECK(close(FRAME.camera_position.z, REVEAL.camera_position.z));
            previous_source_angle = SOURCE_ANGLE;
            previous_destination_angle = DESTINATION_ANGLE;
        }
        CHECK(evaluate(JOURNEY, .68, 1.7).travel_progress - REVEAL.travel_progress > .7);
        // Stage boundaries introduce neither position nor velocity cuts.
        const double EPSILON = 1e-5;
        for (double const BOUNDARY : {.10, .12, .22, .32, .68, .78, .90}) {
            const auto BEFORE = evaluate(JOURNEY, BOUNDARY - EPSILON, 1.7);
            const auto AT_BOUNDARY = evaluate(JOURNEY, BOUNDARY, 1.7);
            const auto AFTER = evaluate(JOURNEY, BOUNDARY + EPSILON, 1.7);
            CHECK(close((AT_BOUNDARY.world_origin.x - BEFORE.world_origin.x) / EPSILON,
                (AFTER.world_origin.x - AT_BOUNDARY.world_origin.x) / EPSILON,
                .01));
            CHECK(close((AT_BOUNDARY.camera_position.z - BEFORE.camera_position.z) / EPSILON,
                (AFTER.camera_position.z - AT_BOUNDARY.camera_position.z) / EPSILON,
                .01));
            CHECK(close((AT_BOUNDARY.camera_target.x - BEFORE.camera_target.x) / EPSILON,
                (AFTER.camera_target.x - AT_BOUNDARY.camera_target.x) / EPSILON,
                .01));
            CHECK(close((AT_BOUNDARY.source_curvature - BEFORE.source_curvature) / EPSILON,
                (AFTER.source_curvature - AT_BOUNDARY.source_curvature) / EPSILON,
                .01));
            CHECK(
                close((AT_BOUNDARY.destination_curvature - BEFORE.destination_curvature) / EPSILON,
                    (AFTER.destination_curvature - AT_BOUNDARY.destination_curvature) / EPSILON,
                    .01));
        }
    }

    void flattening_tracks_projected_native_coverage() {
        for (const auto LAYOUT : {Layout::Focused, Layout::SolarSystem}) {
            for (const double ASPECT : {.6, 1., 16. / 9, 2.4}) {
                SceneOptions options;
                options.planet_layout = LAYOUT;
                const auto JOURNEY =
                    make_journey(numbered(1, 1, 0), numbered(3, 3, 2), options, ASPECT);
                bool unbent = false;
                std::array<double, 2> first_bend{1, 1};
                std::array<double, 2> last_bend{};
                std::array<unsigned, 2> bend_states{};
                std::array<bool, 2> overlapping_motion{};
                for (int sample = 0; sample <= 1000; ++sample) {
                    const auto FRAME = evaluate(JOURNEY, sample / 1000., ASPECT);
                    for (int endpoint = 0; endpoint < 2; ++endpoint) {
                        const auto CENTER =
                            endpoint == 0 ? FRAME.source_center : FRAME.destination_center;
                        const double CURVATURE =
                            endpoint == 0 ? FRAME.source_curvature : FRAME.destination_curvature;
                        double left = 1e20, right = -1e20, bottom = 1e20, top = -1e20;
                        bool in_front = true;
                        for (const double X : {-ASPECT, ASPECT}) {
                            for (const double Y : {-1., 1.}) {
                                const auto CLIP = transform(FRAME.view_projection,
                                    {CENTER.x + X, CENTER.y + Y, CENTER.z + FRAME.radius, 1});
                                in_front = in_front && CLIP[3] > 0;
                                left = std::min(left, CLIP[0] / CLIP[3]);
                                right = std::max(right, CLIP[0] / CLIP[3]);
                                bottom = std::min(bottom, CLIP[1] / CLIP[3]);
                                top = std::max(top, CLIP[1] / CLIP[3]);
                            }
                        }
                        const double COVERAGE = std::min(right - left, top - bottom) / 2;
                        if (CURVATURE < .99999) {
                            CHECK(in_front);
                            CHECK(COVERAGE > .78);
                        }
                        if (CURVATURE < .1) {
                            CHECK(COVERAGE > .93);
                        }
                        if (endpoint == 1 && CURVATURE > .01 && CURVATURE < .99) {
                            unbent = true;
                        }
                        if (CURVATURE > .05 && CURVATURE < .95) {
                            first_bend[endpoint] = std::min(first_bend[endpoint], sample / 1000.);
                            last_bend[endpoint] = std::max(last_bend[endpoint], sample / 1000.);
                            bend_states[endpoint] |= CURVATURE < .3 ? 1U : CURVATURE < .7 ? 2U : 4U;
                            overlapping_motion[endpoint] =
                                overlapping_motion[endpoint] ||
                                FRAME.camera_position.z > FRAME.radius + 2.414213562373095 + .0001;
                        }
                    }
                }
                CHECK(unbent);
                for (std::size_t endpoint = 0; endpoint < first_bend.size(); ++endpoint) {
                    CHECK(last_bend[endpoint] - first_bend[endpoint] >= .07);
                    CHECK(bend_states[endpoint] == 7);
                    CHECK(overlapping_motion[endpoint]);
                }
                CHECK(evaluate(JOURNEY, .001, ASPECT).source_curvature > 0);
                CHECK(evaluate(JOURNEY, .999, ASPECT).destination_curvature > 0);
                const double EPSILON = 1e-5;
                CHECK(evaluate(JOURNEY, EPSILON, ASPECT).source_curvature / EPSILON < .001);
                CHECK(
                    evaluate(JOURNEY, 1 - EPSILON, ASPECT).destination_curvature / EPSILON < .001);
                auto translated_from = JOURNEY.from;
                auto translated_to = JOURNEY.to;
                translated_from.monitor_x += 10000;
                translated_to.monitor_x += 10000;
                const auto TRANSLATED =
                    make_journey(translated_from, translated_to, options, ASPECT);
                CHECK(close(evaluate(JOURNEY, .5, ASPECT).camera_position.z,
                    evaluate(TRANSLATED, .5, ASPECT).camera_position.z));
            }
        }
    }

}  // namespace

int main() {
    planet_scene_layout_tests::layouts_are_deterministic_and_locked();
    owner_extents_have_lattice_clearance();
    planet_scene_layout_tests::topology_scope_and_geometry_are_independent();
    planet_scene_layout_tests::projected_topology_preserves_native_handoff();
    planet_scene_layout_tests::origin_and_named_handoffs_have_no_foreground_inventory();
    planet_scene_surface_tests::geometry_has_native_endpoints();
    planet_scene_surface_tests::projection_is_spherical_and_aspect_correct();
    motion_is_continuous_and_direct();
    planet_scene_layout_tests::solar_packing_has_no_overlap();
    planet_scene_layout_tests::solar_paths_preserve_gaps_and_reveal_real_intermediates();
    planet_scene_layout_tests::named_solar_cells_are_local_stable_and_disjoint();
    planet_scene_layout_tests::distant_planets_keep_stable_coordinates_and_framing();
    named_and_special_direction_matches_navigation();
    bending_precedes_reveal_and_approach_grows();
    flattening_tracks_projected_native_coverage();
    planet_scene_layout_tests::placements_survive_recreation_and_resolve_collisions();
    planet_scene_surface_tests::angular_bend_is_convex_and_stable();
    planet_scene_surface_tests::angular_bend_normals_match_tangents_and_rotation();
    return g_failures == 0 ? 0 : 1;
}
