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
                result[row] += matrix[column * 4 + row] * point[column];
            }
        }
        return result;
    }
    JourneyEndpoint numbered(std::int64_t identity, std::uint32_t number, std::uint32_t rank) {
        return {.identity = identity, .numbered = number, .special = false, .logicalOrder = rank,
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
            .numbered = {}, .special = false, .logicalOrder = 99};
        const JourneyEndpoint SPECIAL{.identity = -2, .numbered = 99, .special = true, .logicalOrder = 99};
        CHECK(logical_distance(FIRST, NAMED) == 1);
        CHECK(logical_distance(FIRST, SPECIAL) == 1);
        SceneOptions options;
        CHECK(close(duration_seconds(FIRST, SPARSE, options), .8));
        CHECK(close(duration_seconds(FIRST, NAMED, options), .8));
        options.skipDurationMode = DurationMode::Constant;
        CHECK(close(duration_seconds(FIRST, THIRD, options), .8));
        options.skipDurationMode = DurationMode::DistanceScaled;
        CHECK(close(duration_seconds(FIRST, THIRD, options), 1.0));
        const auto FAR = numbered(8, 8, 8);
        const double LINEAR = duration_seconds(FIRST, FAR, options);
        CHECK(close(LINEAR, options.maxDuration));
        CHECK(close(duration_seconds(FIRST, numbered(100, 100, 100000), options), options.maxDuration));
        options.maxDuration = .1;
        CHECK(close(duration_seconds(FIRST, FAR, options), options.duration));
        options.maxDuration = 2.2;
        options.skipDurationMode = DurationMode::Adaptive;
        CHECK(duration_seconds(FIRST, FAR, options) < LINEAR);
        CHECK(close(duration_seconds(FIRST, THIRD, options), .8));
        CHECK(close(duration_seconds(FIRST, numbered(4, 4, 3), options), .8));
        CHECK(close(duration_seconds(FIRST, FAR, options), .8 * (1 + .25 * std::log2(6))));
        options.adaptiveBoundary = 1;
        CHECK(close(duration_seconds(FIRST, FAR, options), 1.4));
        CHECK(close(duration_seconds(FIRST, numbered(100, 100, 100000), options), options.maxDuration));
        options.maxDuration = .1;
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
        options.planetLayout = Layout::SolarSystem;
        const auto SOLAR = layout_position(FIRST, options);
        auto changed_rank = FIRST;
        changed_rank.logicalOrder = 100;
        CHECK(close(SOLAR, layout_position(changed_rank, options)));
        CHECK(close(SOLAR, make_journey(FIRST, LAST, options).sourceCenter));
        CHECK(close(SOLAR, make_journey(LAST, FIRST, options).destinationCenter));
        options.planetLayout = Layout::Dynamic;
        const auto JOURNEY = make_journey(FIRST, LAST, options);
        CHECK(close(JOURNEY.sourceCenter, {}));
        CHECK(close(JOURNEY.destinationCenter, {4, 0, 0}));
        CHECK(close(make_journey(LAST, FIRST, options).destinationCenter, {-4, 0, 0}));
        CHECK(close(make_journey(FIRST, FIRST, options).sourceCenter,
            make_journey(FIRST, FIRST, options).destinationCenter));
        for (double progress : {0., .1, .5, .9, 1.}) {
            const auto FRAME = evaluate(JOURNEY, progress, 16. / 9);
            CHECK(close(Vec3{FRAME.sourceCenter.x + FRAME.worldOrigin.x,
                FRAME.sourceCenter.y + FRAME.worldOrigin.y, FRAME.sourceCenter.z + FRAME.worldOrigin.z}, JOURNEY.sourceCenter));
            CHECK(close(Vec3{FRAME.destinationCenter.x + FRAME.worldOrigin.x,
                FRAME.destinationCenter.y + FRAME.worldOrigin.y, FRAME.destinationCenter.z + FRAME.worldOrigin.z}, JOURNEY.destinationCenter));
        }
        for (auto layout : {Layout::Linear, Layout::SolarSystem, Layout::Dynamic}) {
            options.planetLayout = layout;
            const JourneyEndpoint NAMED{.identity = std::numeric_limits<std::int64_t>::min(),
                .numbered = {}, .special = false, .logicalOrder = 0};
            const auto CENTER = layout_position(NAMED, options);
            CHECK(std::isfinite(CENTER.x) && std::isfinite(CENTER.y) && std::isfinite(CENTER.z));
            CHECK(close(CENTER, layout_position(NAMED, options)));
        }
    }

    void geometry_has_native_endpoints() {
        const auto JOURNEY = make_journey(numbered(1, 1, 0), numbered(9, 9000, 2));
        for (double aspect : {.5, 1., 16. / 9, 3.}) {
            for (double progress : {0., 1.}) {
                const auto FRAME = evaluate(JOURNEY, progress, aspect);
                const double CURVATURE = progress == 0 ? FRAME.sourceCurvature : FRAME.destinationCurvature;
                CHECK(CURVATURE == 0);
                CHECK((progress == 0 ? FRAME.destinationCurvature : FRAME.sourceCurvature) == 1);
                CHECK(FRAME.endpointEnvelope == 0);
                CHECK(FRAME.sourceRotation == IDENTITY);
                CHECK(FRAME.destinationRotation == IDENTITY);
                const auto CENTER = progress == 0 ? FRAME.sourceCenter : FRAME.destinationCenter;
                for (double u : {0., .3, 1.}) {
                    for (double v : {0., .7, 1.}) {
                        const auto POINT = project_surface(u, v, aspect, FRAME.radius, FRAME.coverage,
                            CURVATURE, IDENTITY, CENTER, 20);
                        CHECK(close(POINT.position, {CENTER.x + (2 * u - 1) * aspect,
                            CENTER.y + 1 - 2 * v, CENTER.z + FRAME.radius}));
                        CHECK(close(POINT.normal, {0, 0, 1}));
                        const auto CLIP = transform(FRAME.viewProjection,
                            {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                        CHECK(close(CLIP[0] / CLIP[3], 2 * u - 1));
                        CHECK(close(CLIP[1] / CLIP[3], 1 - 2 * v));
                    }
                }
            }
        }
    }

    void projection_is_spherical_and_aspect_correct() {
        for (double aspect : {.5, 1., 16. / 9, 3.}) {
            for (double u : {0., .25, .5, 1.}) {
                for (double v : {0., .3, .5, 1.}) {
                    const auto VERTEX = make_surface_vertex(u, v, aspect, 2, .7);
                    CHECK(close(length({VERTEX.spherePosition[0], VERTEX.spherePosition[1], VERTEX.spherePosition[2]}), 2));
                    const auto POINT = project_surface(u, v, aspect, 2, .7, 1, IDENTITY);
                    CHECK(close(length(POINT.position), 2));
                    CHECK(close(length(POINT.normal), 1));
                    const auto ELEVATED = project_surface(u, v, aspect, 2, .7, 1, IDENTITY, {}, .2);
                    CHECK(close(length(ELEVATED.position), 2.2));
                }
            }
            const auto RIGHT = make_surface_vertex(.5001, .5, aspect, 1);
            const auto UP = make_surface_vertex(.5, .4999, aspect, 1);
            CHECK(close(RIGHT.spherePosition[0] / UP.spherePosition[1], aspect, 1e-3));
        }
        // The backing sphere's seam closes, and both polar rows collapse exactly.
        for (double v : {0., .2, .5, .8, 1.}) {
            const auto LEFT = make_surface_vertex(0, v, 2, 1, 1);
            const auto RIGHT = make_surface_vertex(1, v, 2, 1, 1);
            CHECK(close(Vec3{LEFT.spherePosition[0], LEFT.spherePosition[1], LEFT.spherePosition[2]},
                {RIGHT.spherePosition[0], RIGHT.spherePosition[1], RIGHT.spherePosition[2]}));
        }
        CHECK(close(make_surface_vertex(.3, 0, 2, 1, 1).spherePosition[1], 1));
        CHECK(close(make_surface_vertex(.8, 1, 2, 1, 1).spherePosition[1], -1));
    }

    void motion_is_continuous_and_direct() {
        SceneOptions options;
        options.planetLayout = Layout::SolarSystem;
        const auto JOURNEY = make_journey(numbered(10, 1, 0), numbered(30, 3, 2), options);
        const auto FIRST = evaluate(JOURNEY, 0, 1.7);
        const auto LAST = evaluate(JOURNEY, 1, 1.7);
        const double EPSILON = 1e-5;
        const auto NEAR_FIRST = evaluate(JOURNEY, EPSILON, 1.7);
        const auto NEAR_LAST = evaluate(JOURNEY, 1 - EPSILON, 1.7);
        CHECK(close(FIRST.cameraPosition, NEAR_FIRST.cameraPosition, 1e-6));
        CHECK(close(LAST.cameraPosition, NEAR_LAST.cameraPosition, 1e-6));
        CHECK(close(NEAR_FIRST.sourceCurvature / EPSILON, 0, .001));
        CHECK(close(NEAR_LAST.destinationCurvature / EPSILON, 0, .001));
        CHECK(close(NEAR_FIRST.sourceRotation[2] / EPSILON, 0, .001));
        double previous = -1;
        for (int step = 0; step <= 100; ++step) {
            const double PROGRESS = step / 100.;
            const auto FRAME = evaluate(JOURNEY, PROGRESS, 1.7);
            CHECK(FRAME.travelProgress >= previous);
            previous = FRAME.travelProgress;
            CHECK(close(FRAME.worldOrigin.x, FIRST.worldOrigin.x +
                (LAST.worldOrigin.x - FIRST.worldOrigin.x) * FRAME.travelProgress));
            CHECK(close(FRAME.worldOrigin.y, FIRST.worldOrigin.y +
                (LAST.worldOrigin.y - FIRST.worldOrigin.y) * FRAME.travelProgress));
            const Vec3 DIRECTION{JOURNEY.destinationCenter.x - JOURNEY.sourceCenter.x,
                JOURNEY.destinationCenter.y - JOURNEY.sourceCenter.y, 0};
            CHECK(FRAME.cameraTarget.x * DIRECTION.x + FRAME.cameraTarget.y * DIRECTION.y >= 0);
            if (step == 50) {
                CHECK(length(FRAME.cameraTarget) > 0);
                CHECK(std::abs(FRAME.viewProjection[2]) + std::abs(FRAME.viewProjection[6]) > 0);
            }
            // Identical progress during reversal reproduces every visible property exactly.
            const auto REVISITED = evaluate(JOURNEY, PROGRESS, 1.7);
            CHECK(REVISITED.viewProjection == FRAME.viewProjection);
            CHECK(REVISITED.sourceRotation == FRAME.sourceRotation);
            CHECK(REVISITED.sourceCurvature == FRAME.sourceCurvature);
            CHECK(REVISITED.destinationCurvature == FRAME.destinationCurvature);
            const std::array<double, 4> POINT{.1, -.2, .3, 1};
            const auto RECOVERED = transform(FRAME.inverseViewProjection, transform(FRAME.viewProjection, POINT));
            for (int component = 0; component < 4; ++component) {
                CHECK(close(RECOVERED[component], POINT[component], .003));
            }
        }
        CHECK(close(evaluate(JOURNEY, -5, 1.7).cameraPosition, FIRST.cameraPosition));
        CHECK(close(evaluate(JOURNEY, 5, 1.7).cameraPosition, LAST.cameraPosition));
        CHECK(close(evaluate(JOURNEY, std::numeric_limits<double>::quiet_NaN(), 1.7).cameraPosition, FIRST.cameraPosition));
        options.rotationEnabled = false;
        const auto NO_ROTATION = evaluate(make_journey(JOURNEY.from, JOURNEY.to, options), .5, 1);
        CHECK(NO_ROTATION.sourceRotation == IDENTITY);
        CHECK(NO_ROTATION.destinationRotation == IDENTITY);
    }

    void solar_packing_has_no_overlap() {
        SceneOptions options;
        options.planetLayout = Layout::SolarSystem;
        options.planetSeparation = .1; // The physical non-intersection bound wins.
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
        options.planetLayout = Layout::SolarSystem;
        const auto FIRST = numbered(1, 1, 0);
        const auto SECOND = numbered(2, 2, 1);
        const auto FIFTH = numbered(5, 5, 2); // Sparse inventory rank must not compress 1 -> 5.
        for (double aspect : {.6, 1., 16. / 9, 2.4}) {
            const auto SHORT = make_journey(FIRST, SECOND, options, aspect);
            const auto LONG = make_journey(FIRST, FIFTH, options, aspect);
            const auto distance = [](const Journey &journey) {
                return length({journey.destinationCenter.x - journey.sourceCenter.x,
                    journey.destinationCenter.y - journey.sourceCenter.y,
                    journey.destinationCenter.z - journey.sourceCenter.z});
            };
            CHECK(distance(LONG) > distance(SHORT) * 2.5);
            CHECK(close(LONG.destinationCenter, layout_position(FIFTH, options, aspect)));
            CHECK(close(LONG.aspect, aspect));
            const double GAP_A = layout_position(SECOND, options, aspect).x - LONG.sourceCenter.x;
            const double GAP_B = layout_position(numbered(3, 3, 2), options, aspect).x -
                layout_position(SECOND, options, aspect).x;
            CHECK(!close(GAP_A, GAP_B));
            CHECK(std::abs(LONG.destinationCenter.z - LONG.sourceCenter.z) > .01);
            CHECK(std::abs(LONG.destinationCenter.y - LONG.sourceCenter.y) > .01);
            for (std::uint32_t number = 2; number <= 4; ++number) {
                const auto ANCHOR = layout_position(numbered(number, number, 999), options, aspect);
                bool visible = false;
                for (double progress : {.35, .4, .5, .6, .65}) {
                    const auto FRAME = evaluate(LONG, progress, aspect);
                    const auto CLIP = transform(FRAME.viewProjection,
                        {ANCHOR.x - FRAME.worldOrigin.x, ANCHOR.y - FRAME.worldOrigin.y,
                            ANCHOR.z - FRAME.worldOrigin.z, 1});
                    visible = visible || (CLIP[3] > 0 && std::abs(CLIP[0]) < CLIP[3] &&
                        std::abs(CLIP[1]) < CLIP[3] && std::abs(CLIP[2]) < CLIP[3]);
                    CHECK(close(FRAME.worldOrigin.z, LONG.sourceCenter.z +
                        (LONG.destinationCenter.z - LONG.sourceCenter.z) * FRAME.travelProgress));
                }
                CHECK(visible);
            }
            // Fractional-scale monitors must use the actual render aspect everywhere.
            const double FRACTIONAL_ASPECT = (2560. / 1.5) / (1440. / 1.5);
            CHECK(close(layout_position(FIFTH, options, FRACTIONAL_ASPECT),
                layout_position(FIFTH, options, 2560. / 1440.)));
            for (double progress : {0., 1.}) {
                const auto FRAME = evaluate(LONG, progress, aspect);
                const auto CENTER = progress == 0 ? FRAME.sourceCenter : FRAME.destinationCenter;
                const auto POINT = project_surface(1, 0, aspect, FRAME.radius, FRAME.coverage, 0, IDENTITY, CENTER);
                const auto CLIP = transform(FRAME.viewProjection, {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                CHECK(close(CLIP[0] / CLIP[3], 1));
                CHECK(close(CLIP[1] / CLIP[3], 1));
            }
            const auto FORWARD = evaluate(LONG, .43, aspect);
            (void)evaluate(LONG, .71, aspect);
            const auto REVERSED = evaluate(LONG, .43, aspect);
            CHECK(FORWARD.viewProjection == REVERSED.viewProjection);
            CHECK(close(FORWARD.worldOrigin, REVERSED.worldOrigin));
        }
        CHECK(layout_position(FIFTH, options, 2).x > layout_position(FIFTH, options, 1).x);
        options.solarVerticalSpread = 0;
        CHECK(layout_position(FIRST, options).y == 0);
        CHECK(layout_position(FIFTH, options).y == 0);
    }

    void named_solar_cells_are_local_stable_and_disjoint() {
        SceneOptions options;
        options.planetLayout = Layout::SolarSystem;
        options.planetSeparation = .1;
        std::array<Vec3, 128> centers{};
        for (double spread : {0., .65, 4.}) {
            options.solarVerticalSpread = spread;
            for (std::size_t index = 0; index < centers.size(); ++index) {
                JourneyEndpoint endpoint{.identity = INT64_MIN + static_cast<std::int64_t>(index),
                    .numbered = {}, .special = index % 2 == 0, .logicalOrder = 1000,
                    .placement = index * 2 + 1};
                centers[index] = layout_position(endpoint, options, 1.8);
                endpoint.identity = static_cast<std::int64_t>(index);
                endpoint.logicalOrder = 0;
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
        options.solarVerticalSpread = 0;
        const auto FLAT = layout_position(NAMED, options);
        options.solarVerticalSpread = .65;
        CHECK(std::abs(layout_position(NAMED, options).y - FLAT.y) > .1);
        const JourneyEndpoint EXTREME_SPECIAL{.identity = INT64_MIN, .special = true, .placement = UINT64_MAX};
        const auto EXTREME = make_journey(NAMED, EXTREME_SPECIAL, options, 1.8);
        for (double progress : {0., .2, .32, .5, .68, .8, 1.}) {
            const auto FRAME = evaluate(EXTREME, progress, 1.8);
            CHECK(std::isfinite(FRAME.worldOrigin.x) && std::isfinite(FRAME.worldOrigin.y) &&
                std::isfinite(FRAME.worldOrigin.z));
            CHECK(FRAME.cameraPosition.z < 30);
            for (auto value : FRAME.viewProjection) {
                CHECK(std::isfinite(value));
            }
        }
    }

    void distant_planets_keep_stable_coordinates_and_framing() {
        SceneOptions options;
        options.planetLayout = Layout::SolarSystem;
        const auto FIRST = numbered(1, 1, 0);
        const auto DISTANT = numbered(INT64_MAX, UINT32_MAX, 1);
        const auto JOURNEY = make_journey(FIRST, DISTANT, options);
        CHECK(close(JOURNEY.destinationCenter, layout_position(DISTANT, options)));
        CHECK(close(JOURNEY.destinationCenter, make_journey(numbered(9, 9, 3), DISTANT, options).destinationCenter));
        for (double aspect : {.5, 1., 3.}) {
            for (int step = 0; step <= 20; ++step) {
                const auto FRAME = evaluate(JOURNEY, step / 20., aspect);
                CHECK(std::isfinite(FRAME.cameraPosition.z));
                // A long skip may leave both endpoints behind; the actual nearest
                // numbered cell must remain legible instead of shrinking the universe.
                const double CELL_WIDTH = 5 * std::max(1.0, JOURNEY.aspect);
                const auto NEAREST = static_cast<std::uint32_t>(std::clamp(
                    std::round(FRAME.worldOrigin.x / CELL_WIDTH), 0., static_cast<double>(UINT32_MAX)));
                const auto ANCHOR = layout_position(numbered(NEAREST, NEAREST, 0), options, JOURNEY.aspect);
                const auto CLIP = transform(FRAME.viewProjection,
                    {ANCHOR.x - FRAME.worldOrigin.x, ANCHOR.y - FRAME.worldOrigin.y,
                        ANCHOR.z - FRAME.worldOrigin.z, 1});
                if (step >= 7 && step <= 13) {
                    CHECK(CLIP[3] > 0);
                    CHECK(std::abs(CLIP[0]) < CLIP[3] && std::abs(CLIP[1]) < CLIP[3]);
                    const double DIAMETER_PIXELS = 720 * 2.414213562373095 * .75 / CLIP[3];
                    CHECK(DIAMETER_PIXELS >= 35 && DIAMETER_PIXELS <= 80);
                    CHECK(FRAME.cameraPosition.z < 30);
                    CHECK(length(FRAME.cameraTarget) <= .4 * options.planetSeparation + 1e-5);
                    CHECK(FRAME.worldOrigin.z + FRAME.cameraPosition.z > 1.75 + options.planetSize + options.elevation);
                }
                CHECK(step / 20. > .32 || FRAME.travelProgress == 0);
                CHECK(step / 20. < .68 || FRAME.travelProgress == 1);
                if (step == 0 || step == 20) {
                    const auto CENTER = step == 0 ? FRAME.sourceCenter : FRAME.destinationCenter;
                    CHECK(close(CENTER, {}));
                    CHECK(FRAME.cameraPosition.z == FRAME.radius + 2.414213562373095);
                    CHECK(FRAME.cameraTarget.x == 0);
                    CHECK(FRAME.cameraTarget.y == 0);
                    CHECK(FRAME.cameraTarget.z == 0);
                    const auto POINT = project_surface(1, 0, aspect, FRAME.radius, FRAME.coverage, 0, IDENTITY, CENTER);
                    const auto CLIP = transform(FRAME.viewProjection, {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                    CHECK(close(CLIP[0] / CLIP[3], 1));
                    CHECK(close(CLIP[1] / CLIP[3], 1));
                }
            }
        }
    }

    void named_and_special_direction_matches_navigation() {
        SceneOptions options;
        options.planetLayout = Layout::Dynamic;
        const JourneyEndpoint NAMED_A{.identity = 100, .numbered = {}, .special = false, .logicalOrder = 0};
        const JourneyEndpoint NAMED_B{.identity = 1, .numbered = {}, .special = false, .logicalOrder = 0};
        const JourneyEndpoint SPECIAL{.identity = -1, .numbered = {}, .special = true, .logicalOrder = 0};
        const auto NUMBERED = numbered(20, 20, 3);
        CHECK(make_journey(NAMED_A, NAMED_B, options).destinationCenter.x > 0);
        CHECK(make_journey(NUMBERED, SPECIAL, options).destinationCenter.x > 0);
        CHECK(make_journey(SPECIAL, NUMBERED, options).destinationCenter.x < 0);
        CHECK(evaluate(make_journey(NAMED_A, NAMED_B, options), .5, 1).sourceRotation[2] < 0);
        CHECK(evaluate(make_journey(SPECIAL, NUMBERED, options), .5, 1).sourceRotation[2] > 0);
    }

    void bending_precedes_reveal_and_approach_grows() {
        const auto JOURNEY = make_journey(numbered(1, 1, 0), numbered(2, 2, 1));
        const auto NATIVE = evaluate(JOURNEY, 0, 1.7);
        const auto BENDING = evaluate(JOURNEY, .10, 1.7);
        const auto EARLY_REVEAL = evaluate(JOURNEY, .16, 1.7);
        const auto REVEAL = evaluate(JOURNEY, .32, 1.7);
        CHECK(close(BENDING.worldOrigin, NATIVE.worldOrigin));
        CHECK(close(BENDING.cameraPosition, NATIVE.cameraPosition));
        CHECK(BENDING.sourceCurvature > .3);
        CHECK(EARLY_REVEAL.sourceCurvature > .85);
        CHECK(EARLY_REVEAL.travelProgress < .01);
        CHECK(EARLY_REVEAL.cameraPosition.z - NATIVE.cameraPosition.z <
            .1 * (REVEAL.cameraPosition.z - NATIVE.cameraPosition.z));
        CHECK(REVEAL.sourceCurvature == 1);
        CHECK(REVEAL.destinationCurvature == 1);
        double previous_distance = std::numeric_limits<double>::infinity();
        double previous_altitude = previous_distance;
        double previous_angular_radius = 0;
        for (int step = 32; step <= 100; ++step) {
            const auto FRAME = evaluate(JOURNEY, step / 100., 1.7);
            const double DISTANCE = length({FRAME.destinationCenter.x - FRAME.cameraPosition.x,
                FRAME.destinationCenter.y - FRAME.cameraPosition.y, FRAME.destinationCenter.z - FRAME.cameraPosition.z});
            const double ANGULAR_RADIUS = std::asin(FRAME.radius / DISTANCE);
            CHECK(DISTANCE <= previous_distance);
            CHECK(FRAME.cameraPosition.z <= previous_altitude);
            CHECK(ANGULAR_RADIUS >= previous_angular_radius);
            previous_distance = DISTANCE;
            previous_altitude = FRAME.cameraPosition.z;
            previous_angular_radius = ANGULAR_RADIUS;
        }
        double previous_source_angle = std::numeric_limits<double>::infinity();
        double previous_destination_angle = 0;
        for (int step = 32; step <= 68; ++step) {
            const auto FRAME = evaluate(JOURNEY, step / 100., 1.7);
            const double SOURCE_DISTANCE = length({FRAME.sourceCenter.x - FRAME.cameraPosition.x,
                FRAME.sourceCenter.y - FRAME.cameraPosition.y, FRAME.sourceCenter.z - FRAME.cameraPosition.z});
            const double DESTINATION_DISTANCE = length({FRAME.destinationCenter.x - FRAME.cameraPosition.x,
                FRAME.destinationCenter.y - FRAME.cameraPosition.y, FRAME.destinationCenter.z - FRAME.cameraPosition.z});
            const double SOURCE_ANGLE = std::asin(FRAME.radius / SOURCE_DISTANCE);
            const double DESTINATION_ANGLE = std::asin(FRAME.radius / DESTINATION_DISTANCE);
            CHECK(SOURCE_ANGLE <= previous_source_angle);
            CHECK(DESTINATION_ANGLE >= previous_destination_angle);
            CHECK(close(FRAME.cameraPosition.z, REVEAL.cameraPosition.z));
            previous_source_angle = SOURCE_ANGLE;
            previous_destination_angle = DESTINATION_ANGLE;
        }
        CHECK(evaluate(JOURNEY, .68, 1.7).travelProgress - REVEAL.travelProgress > .7);
        // Stage boundaries introduce neither position nor velocity cuts.
        const double EPSILON = 1e-5;
        for (double boundary : {.10, .12, .22, .32, .68, .78, .90}) {
            const auto BEFORE = evaluate(JOURNEY, boundary - EPSILON, 1.7);
            const auto AT = evaluate(JOURNEY, boundary, 1.7);
            const auto AFTER = evaluate(JOURNEY, boundary + EPSILON, 1.7);
            CHECK(close((AT.worldOrigin.x - BEFORE.worldOrigin.x) / EPSILON,
                (AFTER.worldOrigin.x - AT.worldOrigin.x) / EPSILON, .01));
            CHECK(close((AT.cameraPosition.z - BEFORE.cameraPosition.z) / EPSILON,
                (AFTER.cameraPosition.z - AT.cameraPosition.z) / EPSILON, .01));
            CHECK(close((AT.cameraTarget.x - BEFORE.cameraTarget.x) / EPSILON,
                (AFTER.cameraTarget.x - AT.cameraTarget.x) / EPSILON, .01));
            CHECK(close((AT.sourceCurvature - BEFORE.sourceCurvature) / EPSILON,
                (AFTER.sourceCurvature - AT.sourceCurvature) / EPSILON, .01));
            CHECK(close((AT.destinationCurvature - BEFORE.destinationCurvature) / EPSILON,
                (AFTER.destinationCurvature - AT.destinationCurvature) / EPSILON, .01));
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
        auto recreated = std::make_shared<int>(7);
        const auto NEW_ID = lifetimes.get(recreated, {});
        CHECK(OLD_ID != NEW_ID);
        CHECK(NUMBERED_SLOT == 14);
        CHECK(placements.get(7, {}, std::nullopt) == NUMBERED_SLOT);
        CHECK(CompositionPlacements{}.get(7, {}, std::nullopt) == NUMBERED_SLOT);
        SceneOptions options;
        options.planetLayout = Layout::SolarSystem;
        auto before = numbered(OLD_ID, 7, 0);
        before.placement = NUMBERED_SLOT;
        auto after = numbered(NEW_ID, 7, 100);
        after.placement = placements.get(7, {}, std::nullopt);
        CHECK(close(layout_position(before, options), layout_position(after, options)));
        CHECK(before.identity != after.identity);
        const hyprimate::workspace::TurnComposition<int> COMPOSITION{
            .identity = NEW_ID, .workspace = 7, .numbered = 7, .special = false,
            .logicalOrder = 100, .placement = NUMBERED_SLOT};
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
        const JourneyEndpoint NAMED_BEFORE{.identity = 10, .numbered = {}, .special = false,
            .logicalOrder = 0, .placement = NAMED_SLOT};
        auto named_after = NAMED_BEFORE;
        named_after.identity = 999;
        named_after.placement = placements.get(std::nullopt, "name:dev", std::nullopt);
        CHECK(close(layout_position(NAMED_BEFORE, options), layout_position(named_after, options)));
        const JourneyEndpoint SPECIAL_BEFORE{.identity = 11, .numbered = 7, .special = true,
            .logicalOrder = 0, .placement = SPECIAL_SLOT};
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
            CHECK(slots[index] < 2 * (64 + slots.size()) + 1);
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
        for (double aspect : {.5, 1., 16. / 9, 3.}) {
            const double LON_LIMIT = std::numbers::pi * COVERAGE * std::min(1.0, aspect / 2);
            const double LAT_LIMIT = std::numbers::pi * .5 * COVERAGE * std::min(1.0, 2 / aspect);
            for (double curvature : {0., 1e-12, .001, .05, .4, .8, 1.}) {
                for (double u : {0., .2, .5, .8, 1.}) {
                    for (double v : {0., .3, .5, .9, 1.}) {
                        const auto POINT = project_surface(u, v, aspect, RADIUS, COVERAGE, curvature, IDENTITY);
                        CHECK(std::isfinite(POINT.position.x) && std::isfinite(POINT.position.y) && std::isfinite(POINT.position.z));
                        CHECK(close(length(POINT.normal), 1, 1e-10));
                        if (curvature == 0) {
                            CHECK(POINT.position.x == (2 * u - 1) * aspect);
                            CHECK(POINT.position.y == 1 - 2 * v);
                            CHECK(POINT.position.z == RADIUS);
                            CHECK(close(POINT.normal, {0, 0, 1}, 1e-12));
                        } else if (curvature < 1e-10) {
                            CHECK(close(POINT.position, {(2 * u - 1) * aspect, 1 - 2 * v, RADIUS}, 1e-9));
                        } else {
                            // Every intermediate chart lies on one convex ellipsoid, not a
                            // flat/sphere chord blend that can create wings or inward folds.
                            const double AXIS_X = (1 - curvature) * aspect / LON_LIMIT + curvature * RADIUS;
                            const double AXIS_Y = (1 - curvature) / LAT_LIMIT + curvature * RADIUS;
                            const double X = curvature * POINT.position.x / AXIS_X;
                            const double Y = curvature * POINT.position.y / AXIS_Y;
                            const double Z = 1 + curvature * (POINT.position.z - RADIUS) / RADIUS;
                            CHECK(close(X * X + Y * Y + Z * Z, 1, 1e-10));
                        }
                        const auto ELEVATED = project_surface(u, v, aspect, RADIUS, COVERAGE, curvature, IDENTITY, {}, .2);
                        CHECK(close(ELEVATED.position, {POINT.position.x + .2 * curvature * POINT.normal.x,
                            POINT.position.y + .2 * curvature * POINT.normal.y,
                            POINT.position.z + .2 * curvature * POINT.normal.z}, 1e-10));
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
        for (double curvature : {0., .05, .4, .8, 1.}) {
            const auto CENTER = project_surface(.5, .5, 1.7, 1.3, .7, curvature, ROTATION);
            CHECK(close(CENTER.position, {1.3 * std::sin(MATRIX_ANGLE * curvature), 0,
                1.3 * std::cos(MATRIX_ANGLE * curvature)}, 1e-10));
            CHECK(close(CENTER.normal, {std::sin(MATRIX_ANGLE * curvature), 0,
                std::cos(MATRIX_ANGLE * curvature)}, 1e-10));
            for (double u : {.2, .5, .8}) {
                for (double v : {.2, .5, .8}) {
                    const auto POINT = project_surface(u, v, 1.7, 1.3, .7, curvature, ROTATION);
                    const double EPSILON = 1e-5;
                    const auto LEFT = project_surface(u - EPSILON, v, 1.7, 1.3, .7, curvature, ROTATION);
                    const auto RIGHT = project_surface(u + EPSILON, v, 1.7, 1.3, .7, curvature, ROTATION);
                    const auto UP = project_surface(u, v - EPSILON, 1.7, 1.3, .7, curvature, ROTATION);
                    const auto DOWN = project_surface(u, v + EPSILON, 1.7, 1.3, .7, curvature, ROTATION);
                    const Vec3 DU{RIGHT.position.x - LEFT.position.x, RIGHT.position.y - LEFT.position.y,
                        RIGHT.position.z - LEFT.position.z};
                    const Vec3 DV{DOWN.position.x - UP.position.x, DOWN.position.y - UP.position.y,
                        DOWN.position.z - UP.position.z};
                    const Vec3 CROSS{DV.y * DU.z - DV.z * DU.y, DV.z * DU.x - DV.x * DU.z, DV.x * DU.y - DV.y * DU.x};
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
