#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <string>

#include "Check.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceTurn.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

using namespace hyprimate::workspace::planet_hop;
using hyprimate::workspace::JourneyEndpoint;

namespace {
    constexpr std::array<float, 9> IDENTITY{1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool close(double first, double second, double tolerance = 1e-5) {
        return std::abs(first - second) < tolerance;
    }
    bool close(Vec3 first, Vec3 second, double tolerance = 1e-5) {
        return close(first.x, second.x, tolerance) && close(first.y, second.y, tolerance) && close(first.z, second.z, tolerance);
    }
    double length(Vec3 value) { return std::hypot(value.x, value.y, value.z); }
    std::array<double, 4> transform(const std::array<float, 16> &matrix, std::array<double, 4> point) {
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

    void duration_uses_logical_order() {
        const auto FIRST = numbered(1, 1, 0);
        const auto SPARSE = numbered(1000000, 1000000, 1);
        const auto THIRD = numbered(5000000, 5000000, 2);
        CHECK(logical_distance(FIRST, SPARSE) == 1);
        CHECK(logical_distance(FIRST, THIRD) == 2);
        CHECK(logical_distance(THIRD, FIRST) == 2);
        CHECK(logical_distance(FIRST, FIRST) == 0);
        const JourneyEndpoint NAMED{.identity = std::numeric_limits<std::int64_t>::min(),
            .numbered = {},
            .special = false,
            .logical_order = 99};
        const JourneyEndpoint SPECIAL{
            .identity = -2, .numbered = 99, .special = true, .logical_order = 99};
        CHECK(logical_distance(FIRST, NAMED) == 1);
        CHECK(logical_distance(FIRST, SPECIAL) == 1);
        SceneOptions options;
        CHECK(close(duration_seconds(FIRST, SPARSE, options), .8));
        CHECK(close(duration_seconds(FIRST, NAMED, options), .8));
        options.skip_duration_mode = DurationMode::Constant;
        CHECK(close(duration_seconds(FIRST, THIRD, options), .8));
        options.skip_duration_mode = DurationMode::DistanceScaled;
        CHECK(close(duration_seconds(FIRST, THIRD, options), 1.0));
        const auto FAR = numbered(8, 8, 8);
        const double LINEAR = duration_seconds(FIRST, FAR, options);
        CHECK(close(LINEAR, options.max_duration));
        CHECK(close(
            duration_seconds(FIRST, numbered(100, 100, 100000), options), options.max_duration));
        options.max_duration = .1;
        CHECK(close(duration_seconds(FIRST, FAR, options), options.duration));
        options.max_duration = 2.2;
        options.skip_duration_mode = DurationMode::Adaptive;
        CHECK(duration_seconds(FIRST, FAR, options) < LINEAR);
        CHECK(close(duration_seconds(FIRST, THIRD, options), .8));
        CHECK(close(duration_seconds(FIRST, numbered(4, 4, 3), options), .8));
        CHECK(close(duration_seconds(FIRST, FAR, options), .8 * (1 + (.25 * std::log2(6)))));
        options.adaptive_boundary = 1;
        CHECK(close(duration_seconds(FIRST, FAR, options), 1.4));
        CHECK(close(
            duration_seconds(FIRST, numbered(100, 100, 100000), options), options.max_duration));
        options.max_duration = .1;
        CHECK(close(duration_seconds(FIRST, FAR, options), options.duration));
        options.duration = std::numeric_limits<double>::quiet_NaN();
        CHECK(std::isfinite(duration_seconds(FIRST, FAR, options)));
    }

    void layouts_are_deterministic_and_locked() {
        const auto FIRST = numbered(11, 1, 0);
        const auto LAST = numbered(99, 1000000, 2);
        SceneOptions options;
        CHECK(close(layout_position(FIRST, options), {0, 0, 0}));
        CHECK(close(layout_position(LAST, options), {8, 0, 0}));
        options.planet_layout = Layout::SolarSystem;
        const auto SOLAR = layout_position(FIRST, options);
        auto changed_rank = FIRST;
        changed_rank.logical_order = 100;
        CHECK(close(SOLAR, layout_position(changed_rank, options)));
        CHECK(close(SOLAR, make_journey(FIRST, LAST, options).source_center));
        CHECK(close(SOLAR, make_journey(LAST, FIRST, options).destination_center));
        options.planet_layout = Layout::Dynamic;
        const auto JOURNEY = make_journey(FIRST, LAST, options);
        CHECK(close(JOURNEY.source_center, {}));
        CHECK(close(JOURNEY.destination_center, {4, 0, 0}));
        CHECK(close(make_journey(LAST, FIRST, options).destination_center, {-4, 0, 0}));
        CHECK(close(make_journey(FIRST, FIRST, options).source_center,
            make_journey(FIRST, FIRST, options).destination_center));
        for (double const PROGRESS : {0., .1, .5, .9, 1.}) {
            const auto FRAME = evaluate(JOURNEY, PROGRESS, 16. / 9);
            CHECK(close(Vec3{FRAME.source_center.x + FRAME.world_origin.x,
                            FRAME.source_center.y + FRAME.world_origin.y,
                            FRAME.source_center.z + FRAME.world_origin.z},
                JOURNEY.source_center));
            CHECK(close(Vec3{FRAME.destination_center.x + FRAME.world_origin.x,
                            FRAME.destination_center.y + FRAME.world_origin.y,
                            FRAME.destination_center.z + FRAME.world_origin.z},
                JOURNEY.destination_center));
        }
        for (auto const LAYOUT : {Layout::Linear, Layout::SolarSystem, Layout::Dynamic}) {
            options.planet_layout = LAYOUT;
            const JourneyEndpoint NAMED{.identity = std::numeric_limits<std::int64_t>::min(),
                .numbered = {},
                .special = false,
                .logical_order = 0};
            const auto CENTER = layout_position(NAMED, options);
            CHECK(std::isfinite(CENTER.x) && std::isfinite(CENTER.y) && std::isfinite(CENTER.z));
            CHECK(close(CENTER, layout_position(NAMED, options)));
        }
    }

    void geometry_has_native_endpoints() {
        const auto JOURNEY = make_journey(numbered(1, 1, 0), numbered(9, 9000, 2));
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            for (double const PROGRESS : {0., 1.}) {
                const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                const double CURVATURE =
                    PROGRESS == 0 ? FRAME.source_curvature : FRAME.destination_curvature;
                CHECK(CURVATURE == 0);
                CHECK((PROGRESS == 0 ? FRAME.destination_curvature : FRAME.source_curvature) == 1);
                CHECK(FRAME.endpoint_envelope == 0);
                CHECK(FRAME.source_rotation == IDENTITY);
                CHECK(FRAME.destination_rotation == IDENTITY);
                const auto CENTER = PROGRESS == 0 ? FRAME.source_center : FRAME.destination_center;
                for (double const COORD_U : {0., .3, 1.}) {
                    for (double const COORD_V : {0., .7, 1.}) {
                        const auto POINT = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = FRAME.radius,
                            .coverage = FRAME.coverage,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY,
                            .center = CENTER,
                            .elevation = 20});
                        CHECK(close(POINT.position,
                            {CENTER.x + (((2 * COORD_U) - 1) * ASPECT),
                                CENTER.y + 1 - (2 * COORD_V),
                                CENTER.z + FRAME.radius}));
                        CHECK(close(POINT.normal, {0, 0, 1}));
                        const auto CLIP = transform(FRAME.view_projection,
                            {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                        CHECK(close(CLIP[0] / CLIP[3], (2 * COORD_U) - 1));
                        CHECK(close(CLIP[1] / CLIP[3], 1 - (2 * COORD_V)));
                    }
                }
            }
        }
    }

    void projection_is_spherical_and_aspect_correct() {
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            for (double const COORD_U : {0., .25, .5, 1.}) {
                for (double const COORD_V : {0., .3, .5, 1.}) {
                    const auto VERTEX = make_surface_vertex(COORD_U, COORD_V, ASPECT, 2, .7);
                    CHECK(close(length({VERTEX.sphere_position[0],
                                    VERTEX.sphere_position[1],
                                    VERTEX.sphere_position[2]}),
                        2));
                    const auto POINT = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = ASPECT,
                        .radius = 2,
                        .coverage = .7,
                        .curvature = 1,
                        .rotation = IDENTITY});
                    CHECK(close(length(POINT.position), 2));
                    CHECK(close(length(POINT.normal), 1));
                    const auto ELEVATED = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = ASPECT,
                        .radius = 2,
                        .coverage = .7,
                        .curvature = 1,
                        .rotation = IDENTITY,
                        .elevation = .2});
                    CHECK(close(length(ELEVATED.position), 2.2));
                }
            }
            const auto RIGHT = make_surface_vertex(.5001, .5, ASPECT, 1);
            const auto VERTICAL = make_surface_vertex(.5, .4999, ASPECT, 1);
            CHECK(close(RIGHT.sphere_position[0] / VERTICAL.sphere_position[1], ASPECT, 1e-3));
        }
        // The backing sphere's seam closes, and both polar rows collapse exactly.
        for (double const COORD_V : {0., .2, .5, .8, 1.}) {
            const auto LEFT = make_surface_vertex(0, COORD_V, 2, 1, 1);
            const auto RIGHT = make_surface_vertex(1, COORD_V, 2, 1, 1);
            CHECK(close(
                Vec3{LEFT.sphere_position[0], LEFT.sphere_position[1], LEFT.sphere_position[2]},
                {RIGHT.sphere_position[0], RIGHT.sphere_position[1], RIGHT.sphere_position[2]}));
        }
        CHECK(close(make_surface_vertex(.3, 0, 2, 1, 1).sphere_position[1], 1));
        CHECK(close(make_surface_vertex(.8, 1, 2, 1, 1).sphere_position[1], -1));
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
            CHECK(close(FRAME.world_origin.x,
                FIRST.world_origin.x +
                    ((LAST.world_origin.x - FIRST.world_origin.x) * FRAME.travel_progress)));
            CHECK(close(FRAME.world_origin.y,
                FIRST.world_origin.y +
                    ((LAST.world_origin.y - FIRST.world_origin.y) * FRAME.travel_progress)));
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

    void solar_packing_has_no_overlap() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        options.planet_separation = .1;  // The physical non-intersection bound wins.
        options.elevation = .2;
        std::array<Vec3, 256> centers{};
        for (std::size_t index = 0; index < centers.size(); ++index) {
            centers[index] = layout_position(numbered(static_cast<std::int64_t>(index + 1),
                static_cast<std::uint32_t>(index + 1), 0), options);
            for (std::size_t previous = 0; previous < index; ++previous) {
                CHECK(length({centers[index].x - centers[previous].x, centers[index].y - centers[previous].y, 0}) >= 2.4 - 1e-5);
            }
        }
        const std::array<std::uint32_t, 6> EXTREMES{0, 1, 2, UINT32_MAX - 2, UINT32_MAX - 1, UINT32_MAX};
        for (std::size_t index = 0; index < EXTREMES.size(); ++index) {
            const auto FIRST = layout_position(numbered(INT64_MIN + static_cast<std::int64_t>(index), EXTREMES[index], 0), options);
            for (std::size_t previous = 0; previous < index; ++previous) {
                const auto SECOND = layout_position(numbered(INT64_MIN + static_cast<std::int64_t>(previous), EXTREMES[previous], 0), options);
                CHECK(length({FIRST.x - SECOND.x, FIRST.y - SECOND.y, 0}) >= 2.4 - 1e-4);
            }
        }
    }
    void solar_paths_preserve_gaps_and_reveal_real_intermediates() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        const auto FIRST = numbered(1, 1, 0);
        const auto SECOND = numbered(2, 2, 1);
        const auto FIFTH = numbered(5, 5, 2); // Sparse inventory rank must not compress 1 -> 5.
        for (double const ASPECT : {.6, 1., 16. / 9, 2.4}) {
            const auto SHORT = make_journey(FIRST, SECOND, options, ASPECT);
            const auto LONG = make_journey(FIRST, FIFTH, options, ASPECT);
            const auto DISTANCE = [](const Journey &journey) {
                return length({.x = journey.destination_center.x - journey.source_center.x,
                    .y = journey.destination_center.y - journey.source_center.y,
                    .z = journey.destination_center.z - journey.source_center.z});
            };
            CHECK(DISTANCE(LONG) > DISTANCE(SHORT) * 2.5);
            CHECK(close(LONG.destination_center, layout_position(FIFTH, options, ASPECT)));
            CHECK(close(LONG.aspect, ASPECT));
            const double GAP_A = layout_position(SECOND, options, ASPECT).x - LONG.source_center.x;
            const double GAP_B = layout_position(numbered(3, 3, 2), options, ASPECT).x -
                                 layout_position(SECOND, options, ASPECT).x;
            CHECK(!close(GAP_A, GAP_B));
            CHECK(std::abs(LONG.destination_center.z - LONG.source_center.z) > .01);
            CHECK(std::abs(LONG.destination_center.y - LONG.source_center.y) > .01);
            for (std::uint32_t number = 2; number <= 4; ++number) {
                const auto ANCHOR = layout_position(numbered(number, number, 999), options, ASPECT);
                bool visible = false;
                for (double const PROGRESS : {.35, .4, .5, .6, .65}) {
                    const auto FRAME = evaluate(LONG, PROGRESS, ASPECT);
                    const auto CLIP = transform(FRAME.view_projection,
                        {ANCHOR.x - FRAME.world_origin.x,
                            ANCHOR.y - FRAME.world_origin.y,
                            ANCHOR.z - FRAME.world_origin.z,
                            1});
                    visible = visible || (CLIP[3] > 0 && std::abs(CLIP[0]) < CLIP[3] &&
                        std::abs(CLIP[1]) < CLIP[3] && std::abs(CLIP[2]) < CLIP[3]);
                    CHECK(close(FRAME.world_origin.z,
                        LONG.source_center.z + ((LONG.destination_center.z - LONG.source_center.z) *
                                                   FRAME.travel_progress)));
                }
                CHECK(visible);
            }
            // Fractional-scale monitors must use the actual render aspect everywhere.
            const double FRACTIONAL_ASPECT = (2560. / 1.5) / (1440. / 1.5);
            CHECK(close(layout_position(FIFTH, options, FRACTIONAL_ASPECT),
                layout_position(FIFTH, options, 2560. / 1440.)));
            for (double const PROGRESS : {0., 1.}) {
                const auto FRAME = evaluate(LONG, PROGRESS, ASPECT);
                const auto CENTER = PROGRESS == 0 ? FRAME.source_center : FRAME.destination_center;
                const auto POINT = project_surface({.u = 1,
                    .v = 0,
                    .aspect = ASPECT,
                    .radius = FRAME.radius,
                    .coverage = FRAME.coverage,
                    .curvature = 0,
                    .rotation = IDENTITY,
                    .center = CENTER});
                const auto CLIP = transform(FRAME.view_projection,
                    {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                CHECK(close(CLIP[0] / CLIP[3], 1));
                CHECK(close(CLIP[1] / CLIP[3], 1));
            }
            const auto FORWARD = evaluate(LONG, .43, ASPECT);
            (void)evaluate(LONG, .71, ASPECT);
            const auto REVERSED = evaluate(LONG, .43, ASPECT);
            CHECK(FORWARD.view_projection == REVERSED.view_projection);
            CHECK(close(FORWARD.world_origin, REVERSED.world_origin));
        }
        CHECK(layout_position(FIFTH, options, 2).x > layout_position(FIFTH, options, 1).x);
        options.solar_vertical_spread = 0;
        CHECK(layout_position(FIRST, options).y == 0);
        CHECK(layout_position(FIFTH, options).y == 0);
    }

    void named_solar_cells_are_local_stable_and_disjoint() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        options.planet_separation = .1;
        std::array<Vec3, 128> centers{};
        for (double const SPREAD : {0., .65, 4.}) {
            options.solar_vertical_spread = SPREAD;
            for (std::size_t index = 0; index < centers.size(); ++index) {
                JourneyEndpoint endpoint{.identity = INT64_MIN + static_cast<std::int64_t>(index),
                    .numbered = {},
                    .special = index % 2 == 0,
                    .logical_order = 1000,
                    .placement = (index * 2) + 1};
                centers[index] = layout_position(endpoint, options, 1.8);
                endpoint.identity = static_cast<std::int64_t>(index);
                endpoint.logical_order = 0;
                CHECK(close(centers[index], layout_position(endpoint, options, 1.8)));
                CHECK(length(centers[index]) < 180); // No hash/lifetime-ID-sized coordinate jumps.
                for (std::size_t previous = 0; previous < index; ++previous) {
                    CHECK(length({centers[index].x - centers[previous].x,
                        centers[index].y - centers[previous].y,
                        centers[index].z - centers[previous].z}) >= 2.4 - 1e-5);
                }
                const auto ZERO = layout_position(numbered(0, 0, 0), options, 1.8);
                CHECK(length({centers[index].x - ZERO.x, centers[index].y - ZERO.y,
                    centers[index].z - ZERO.z}) >= 2.4 - 1e-5);
            }
        }
        const JourneyEndpoint NAMED{.identity = 20, .placement = 17};
        options.solar_vertical_spread = 0;
        const auto FLAT = layout_position(NAMED, options);
        options.solar_vertical_spread = .65;
        CHECK(std::abs(layout_position(NAMED, options).y - FLAT.y) > .1);
        const JourneyEndpoint EXTREME_SPECIAL{.identity = INT64_MIN, .special = true, .placement = UINT64_MAX};
        const auto EXTREME = make_journey(NAMED, EXTREME_SPECIAL, options, 1.8);
        for (double const PROGRESS : {0., .2, .32, .5, .68, .8, 1.}) {
            const auto FRAME = evaluate(EXTREME, PROGRESS, 1.8);
            CHECK(std::isfinite(FRAME.world_origin.x) && std::isfinite(FRAME.world_origin.y) &&
                  std::isfinite(FRAME.world_origin.z));
            CHECK(FRAME.camera_position.z < 30);
            for (auto const VALUE : FRAME.view_projection) {
                CHECK(std::isfinite(VALUE));
            }
        }
    }

    void distant_planets_keep_stable_coordinates_and_framing() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        const auto FIRST = numbered(1, 1, 0);
        const auto DISTANT = numbered(INT64_MAX, UINT32_MAX, 1);
        const auto JOURNEY = make_journey(FIRST, DISTANT, options);
        CHECK(close(JOURNEY.destination_center, layout_position(DISTANT, options)));
        CHECK(close(JOURNEY.destination_center,
            make_journey(numbered(9, 9, 3), DISTANT, options).destination_center));
        for (double const ASPECT : {.5, 1., 3.}) {
            for (int step = 0; step <= 20; ++step) {
                const auto FRAME = evaluate(JOURNEY, step / 20., ASPECT);
                CHECK(std::isfinite(FRAME.camera_position.z));
                // A long skip may leave both endpoints behind; the actual nearest
                // numbered cell must remain legible instead of shrinking the universe.
                const double CELL_WIDTH = 5 * std::max(1.0, JOURNEY.aspect);
                const auto NEAREST = static_cast<std::uint32_t>(
                    std::clamp(std::round(FRAME.world_origin.x / CELL_WIDTH),
                        0.,
                        static_cast<double>(UINT32_MAX)));
                const auto ANCHOR = layout_position(numbered(NEAREST, NEAREST, 0), options, JOURNEY.aspect);
                const auto CLIP = transform(FRAME.view_projection,
                    {ANCHOR.x - FRAME.world_origin.x,
                        ANCHOR.y - FRAME.world_origin.y,
                        ANCHOR.z - FRAME.world_origin.z,
                        1});
                if (step >= 7 && step <= 13) {
                    CHECK(CLIP[3] > 0);
                    CHECK(std::abs(CLIP[0]) < CLIP[3] && std::abs(CLIP[1]) < CLIP[3]);
                    const double DIAMETER_PIXELS = 720 * 2.414213562373095 * .75 / CLIP[3];
                    CHECK(DIAMETER_PIXELS >= 35 && DIAMETER_PIXELS <= 80);
                    CHECK(FRAME.camera_position.z < 30);
                    CHECK(length(FRAME.camera_target) <= (.4 * options.planet_separation) + 1e-5);
                    CHECK(FRAME.world_origin.z + FRAME.camera_position.z >
                          1.75 + options.planet_size + options.elevation);
                }
                CHECK(step / 20. > .32 || FRAME.travel_progress == 0);
                CHECK(step / 20. < .68 || FRAME.travel_progress == 1);
                if (step == 0 || step == 20) {
                    const auto CENTER = step == 0 ? FRAME.source_center : FRAME.destination_center;
                    CHECK(close(CENTER, {}));
                    CHECK(FRAME.camera_position.z == FRAME.radius + 2.414213562373095);
                    CHECK(FRAME.camera_target.x == 0);
                    CHECK(FRAME.camera_target.y == 0);
                    CHECK(FRAME.camera_target.z == 0);
                    const auto POINT = project_surface({.u = 1,
                        .v = 0,
                        .aspect = ASPECT,
                        .radius = FRAME.radius,
                        .coverage = FRAME.coverage,
                        .curvature = 0,
                        .rotation = IDENTITY,
                        .center = CENTER});
                    const auto CLIP = transform(FRAME.view_projection,
                        {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                    CHECK(close(CLIP[0] / CLIP[3], 1));
                    CHECK(close(CLIP[1] / CLIP[3], 1));
                }
            }
        }
    }

    void named_and_special_direction_matches_navigation() {
        SceneOptions options;
        options.planet_layout = Layout::Dynamic;
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
        CHECK(close(BENDING.camera_position, NATIVE.camera_position));
        CHECK(BENDING.source_curvature > .3);
        CHECK(EARLY_REVEAL.source_curvature > .85);
        CHECK(EARLY_REVEAL.travel_progress < .01);
        CHECK(EARLY_REVEAL.camera_position.z - NATIVE.camera_position.z <
              .1 * (REVEAL.camera_position.z - NATIVE.camera_position.z));
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

    void placements_survive_recreation_and_resolve_collisions() {
        using hyprimate::workspace::CompositionPlacements;
        CompositionPlacements placements;
        hyprimate::workspace::CompositionIdentities<std::shared_ptr<int>, std::weak_ptr<int>> lifetimes;
        auto original = std::make_shared<int>(7);
        const auto OLD_ID = lifetimes.get(original, {});
        const auto NUMBERED_SLOT = placements.get(7, {}, std::nullopt);
        original.reset();
        auto const RECREATED = std::make_shared<int>(7);
        const auto NEW_ID = lifetimes.get(RECREATED, {});
        CHECK(OLD_ID != NEW_ID);
        CHECK(NUMBERED_SLOT == 14);
        CHECK(placements.get(7, {}, std::nullopt) == NUMBERED_SLOT);
        CHECK(CompositionPlacements{}.get(7, {}, std::nullopt) == NUMBERED_SLOT);
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        auto before = numbered(OLD_ID, 7, 0);
        before.placement = NUMBERED_SLOT;
        auto after = numbered(NEW_ID, 7, 100);
        after.placement = placements.get(7, {}, std::nullopt);
        CHECK(close(layout_position(before, options), layout_position(after, options)));
        CHECK(before.identity != after.identity);
        const hyprimate::workspace::TurnComposition<int> COMPOSITION{.identity = NEW_ID,
            .workspace = 7,
            .numbered = 7,
            .special = false,
            .logical_order = 100,
            .placement = NUMBERED_SLOT};
        CHECK(COMPOSITION.endpoint().identity == NEW_ID);
        CHECK(COMPOSITION.endpoint().placement == NUMBERED_SLOT);

        const auto NAMED_SLOT = placements.get(std::nullopt, "name:dev", std::nullopt);
        const auto SPECIAL_SLOT = placements.get(7, "7", std::string_view{"special:scratch"});
        const auto OTHER_PAIR = placements.get(8, "8", std::string_view{"special:scratch"});
        CHECK(NAMED_SLOT % 2 == 1 && SPECIAL_SLOT % 2 == 1);
        CHECK(NAMED_SLOT != SPECIAL_SLOT && SPECIAL_SLOT != OTHER_PAIR);
        CHECK(placements.get(std::nullopt, "dev", std::nullopt) == NAMED_SLOT);
        CHECK(placements.get(7, "7", std::string_view{"scratch"}) == SPECIAL_SLOT);
        CHECK(placements.get(std::nullopt, "dev", std::string_view{}) != NAMED_SLOT);
        const JourneyEndpoint NAMED_BEFORE{.identity = 10,
            .numbered = {},
            .special = false,
            .logical_order = 0,
            .placement = NAMED_SLOT};
        auto named_after = NAMED_BEFORE;
        named_after.identity = 999;
        named_after.placement = placements.get(std::nullopt, "name:dev", std::nullopt);
        CHECK(close(layout_position(NAMED_BEFORE, options), layout_position(named_after, options)));
        const JourneyEndpoint SPECIAL_BEFORE{.identity = 11,
            .numbered = 7,
            .special = true,
            .logical_order = 0,
            .placement = SPECIAL_SLOT};
        auto special_after = SPECIAL_BEFORE;
        special_after.identity = 1000;
        special_after.placement = placements.get(7, "7", std::string_view{"special:scratch"});
        CHECK(close(layout_position(SPECIAL_BEFORE, options), layout_position(special_after, options)));
        CHECK(!close(layout_position(NAMED_BEFORE, options), layout_position(SPECIAL_BEFORE, options)));
        CHECK(!close(layout_position(before, options), layout_position(SPECIAL_BEFORE, options)));

        CompositionPlacements collisions;
        std::array<std::uint64_t, 128> slots{};
        bool resolved_beyond_initial_range = false;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const auto NAME = "name:collision-" + std::to_string(index);
            slots[index] = collisions.get(std::nullopt, NAME, std::nullopt);
            CHECK(slots[index] % 2 == 1);
            CHECK(slots[index] < (2 * (64 + slots.size())) + 1);
            resolved_beyond_initial_range = resolved_beyond_initial_range || slots[index] >= 129;
            for (std::size_t previous = 0; previous < index; ++previous) {
                CHECK(slots[index] != slots[previous]);
            }
        }
        // 128 names starting in 64 cells necessarily exercise collision resolution.
        CHECK(resolved_beyond_initial_range);
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const auto NAME = "collision-" + std::to_string(index);
            CHECK(collisions.get(std::nullopt, NAME, std::nullopt) == slots[index]);
        }
    }

    void angular_bend_is_convex_and_stable() {
        constexpr double RADIUS = 1.3;
        constexpr double COVERAGE = .7;
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            const double LON_LIMIT = std::numbers::pi * COVERAGE * std::min(1.0, ASPECT / 2);
            const double LAT_LIMIT = std::numbers::pi * .5 * COVERAGE * std::min(1.0, 2 / ASPECT);
            for (double const CURVATURE : {0., 1e-12, .001, .05, .4, .8, 1.}) {
                for (double const COORD_U : {0., .2, .5, .8, 1.}) {
                    for (double const COORD_V : {0., .3, .5, .9, 1.}) {
                        const auto POINT = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = RADIUS,
                            .coverage = COVERAGE,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY});
                        CHECK(std::isfinite(POINT.position.x) && std::isfinite(POINT.position.y) && std::isfinite(POINT.position.z));
                        CHECK(close(length(POINT.normal), 1, 1e-10));
                        if (CURVATURE == 0) {
                            CHECK(POINT.position.x == ((2 * COORD_U) - 1) * ASPECT);
                            CHECK(POINT.position.y == 1 - (2 * COORD_V));
                            CHECK(POINT.position.z == RADIUS);
                            CHECK(close(POINT.normal, {0, 0, 1}, 1e-12));
                        } else if (CURVATURE < 1e-10) {
                            CHECK(close(POINT.position,
                                {((2 * COORD_U) - 1) * ASPECT, 1 - (2 * COORD_V), RADIUS},
                                1e-9));
                        } else {
                            // Every intermediate chart lies on one convex ellipsoid, not a
                            // flat/sphere chord blend that can create wings or inward folds.
                            const double AXIS_X =
                                ((1 - CURVATURE) * ASPECT / LON_LIMIT) + (CURVATURE * RADIUS);
                            const double AXIS_Y =
                                ((1 - CURVATURE) / LAT_LIMIT) + (CURVATURE * RADIUS);
                            const double NORMALIZED_X = CURVATURE * POINT.position.x / AXIS_X;
                            const double NORMALIZED_Y = CURVATURE * POINT.position.y / AXIS_Y;
                            const double NORMALIZED_Z =
                                1 + (CURVATURE * (POINT.position.z - RADIUS) / RADIUS);
                            CHECK(close((NORMALIZED_X * NORMALIZED_X) +
                                            (NORMALIZED_Y * NORMALIZED_Y) +
                                            (NORMALIZED_Z * NORMALIZED_Z),
                                1,
                                1e-10));
                        }
                        const auto ELEVATED = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = RADIUS,
                            .coverage = COVERAGE,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY,
                            .elevation = .2});
                        CHECK(close(ELEVATED.position,
                            {POINT.position.x + (.2 * CURVATURE * POINT.normal.x),
                                POINT.position.y + (.2 * CURVATURE * POINT.normal.y),
                                POINT.position.z + (.2 * CURVATURE * POINT.normal.z)},
                            1e-10));
                    }
                }
            }
        }
    }

    void angular_bend_normals_match_tangents_and_rotation() {
        constexpr double ANGLE = .7;
        const auto COSINE = static_cast<float>(std::cos(ANGLE));
        const auto SINE = static_cast<float>(std::sin(ANGLE));
        const std::array<float, 9> ROTATION{COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        const double MATRIX_ANGLE = std::atan2(ROTATION[6], ROTATION[0]);
        for (double const CURVATURE : {0., .05, .4, .8, 1.}) {
            const auto CENTER = project_surface({.u = .5,
                .v = .5,
                .aspect = 1.7,
                .radius = 1.3,
                .coverage = .7,
                .curvature = CURVATURE,
                .rotation = ROTATION});
            CHECK(close(CENTER.position,
                {1.3 * std::sin(MATRIX_ANGLE * CURVATURE),
                    0,
                    1.3 * std::cos(MATRIX_ANGLE * CURVATURE)},
                1e-10));
            CHECK(close(CENTER.normal,
                {std::sin(MATRIX_ANGLE * CURVATURE), 0, std::cos(MATRIX_ANGLE * CURVATURE)},
                1e-10));
            for (double const COORD_U : {.2, .5, .8}) {
                for (double const COORD_V : {.2, .5, .8}) {
                    const auto POINT = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const double EPSILON = 1e-5;
                    const auto LEFT = project_surface({.u = COORD_U - EPSILON,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto RIGHT = project_surface({.u = COORD_U + EPSILON,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto ABOVE = project_surface({.u = COORD_U,
                        .v = COORD_V - EPSILON,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto DOWN = project_surface({.u = COORD_U,
                        .v = COORD_V + EPSILON,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const Vec3 TANGENT_U{.x = RIGHT.position.x - LEFT.position.x,
                        .y = RIGHT.position.y - LEFT.position.y,
                        .z = RIGHT.position.z - LEFT.position.z};
                    const Vec3 TANGENT_V{.x = DOWN.position.x - ABOVE.position.x,
                        .y = DOWN.position.y - ABOVE.position.y,
                        .z = DOWN.position.z - ABOVE.position.z};
                    const Vec3 CROSS{.x = (TANGENT_V.y * TANGENT_U.z) - (TANGENT_V.z * TANGENT_U.y),
                        .y = (TANGENT_V.z * TANGENT_U.x) - (TANGENT_V.x * TANGENT_U.z),
                        .z = (TANGENT_V.x * TANGENT_U.y) - (TANGENT_V.y * TANGENT_U.x)};
                    const double LENGTH = length(CROSS);
                    CHECK(LENGTH > 0);
                    CHECK(close(POINT.normal, {CROSS.x / LENGTH, CROSS.y / LENGTH, CROSS.z / LENGTH}, 1e-5));
                }
            }
        }
    }
}

int main() {
    duration_uses_logical_order();
    layouts_are_deterministic_and_locked();
    geometry_has_native_endpoints();
    projection_is_spherical_and_aspect_correct();
    motion_is_continuous_and_direct();
    solar_packing_has_no_overlap();
    solar_paths_preserve_gaps_and_reveal_real_intermediates();
    named_solar_cells_are_local_stable_and_disjoint();
    distant_planets_keep_stable_coordinates_and_framing();
    named_and_special_direction_matches_navigation();
    bending_precedes_reveal_and_approach_grows();
    placements_survive_recreation_and_resolve_collisions();
    angular_bend_is_convex_and_stable();
    angular_bend_normals_match_tangents_and_rotation();
    return g_failures == 0 ? 0 : 1;
}
