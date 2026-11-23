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

    void layouts_are_deterministic_and_locked() {
        const auto FIRST = numbered(11, 1, 0);
        const auto LAST = numbered(99, 1000000, 2);
        SceneOptions options;
        CHECK(close(make_journey(FIRST, LAST, options).source_center, {}));
        CHECK(close(make_journey(FIRST, LAST, options).destination_center, {4, 0, 0}));
        options.planet_layout = Layout::SolarSystem;
        const auto SOLAR = layout_position(FIRST, options);
        auto changed_rank = FIRST;
        changed_rank.logical_order = 100;
        CHECK(close(SOLAR, layout_position(changed_rank, options)));
        CHECK(close(SOLAR, make_journey(FIRST, LAST, options).source_center));
        CHECK(close(SOLAR, make_journey(LAST, FIRST, options).destination_center));
        options.planet_layout = Layout::Focused;
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
        for (auto const LAYOUT :
            {Layout::Focused, Layout::Monitor, Layout::All, Layout::SolarSystem}) {
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

    void topology_scope_and_geometry_are_independent() {
        auto from = numbered(1, 1, 0);
        from.owner = 10;
        from.monitor_width = 1920;
        from.monitor_height = 1080;
        auto to = numbered(2, 2, 1);
        to.owner = 20;
        to.monitor_x = 1920;
        to.monitor_width = 1280;
        to.monitor_height = 720;
        auto local = numbered(3, 3, 2);
        local.owner = 10;
        auto other_involved = numbered(4, 4, 3);
        other_involved.owner = 20;
        auto uninvolved = numbered(5, 5, 4);
        uninvolved.owner = 30;
        auto special = uninvolved;
        special.special = true;
        for (const auto LAYOUT :
            {Layout::Focused, Layout::Monitor, Layout::All, Layout::SolarSystem}) {
            CHECK(scene_member(from, from, to, LAYOUT));
            CHECK(scene_member(to, from, to, LAYOUT));
            CHECK(!scene_member(special, from, to, LAYOUT));
            CHECK(scene_member(local, from, to, LAYOUT) == (LAYOUT != Layout::Focused));
            CHECK(scene_member(other_involved, from, to, LAYOUT) == (LAYOUT != Layout::Focused));
            CHECK(scene_member(uninvolved, from, to, LAYOUT) ==
                  (LAYOUT == Layout::All || LAYOUT == Layout::SolarSystem));
            CHECK(scene_member(local, to, from, LAYOUT) == scene_member(local, from, to, LAYOUT));
        }
        CHECK(!scene_member(other_involved, from, local, Layout::Monitor));
        auto special_source = from;
        special_source.special = true;
        CHECK(scene_member(special, special_source, to, Layout::All));
        CHECK(!scene_member(special, special_source, to, Layout::Monitor));
        for (const auto LAYOUT : {Layout::Monitor, Layout::All, Layout::SolarSystem}) {
            SceneOptions options;
            options.planet_layout = LAYOUT;
            const auto JOURNEY = make_journey(from, to, options);
            CHECK(close(JOURNEY.source_center, layout_position(from, options)));
            CHECK(close(JOURNEY.destination_center, layout_position(to, options)));
            auto recreated = from;
            recreated.identity = 999;
            recreated.logical_order = 999;
            CHECK(close(layout_position(recreated, options), JOURNEY.source_center));
            uninvolved.monitor_x = -4000;
            CHECK(close(layout_position(from, options), JOURNEY.source_center));
            auto moved = from;
            moved.owner = 20;
            moved.monitor_x = 1920;
            CHECK(!close(layout_position(moved, options), JOURNEY.source_center));
        }
        SceneOptions all;
        all.planet_layout = Layout::All;
        auto same_slot = from;
        same_slot.monitor_x = to.monitor_x;
        same_slot.monitor_y = to.monitor_y;
        CHECK(layout_position(same_slot, all).x > layout_position(from, all).x);
        same_slot.monitor_y = -720;
        CHECK(layout_position(same_slot, all).y > layout_position(from, all).y);
        CHECK(std::abs(layout_position(to, all).z) <= 2);
        const hyprimate::workspace::TurnComposition<int> COMPOSITION{.identity = 1,
            .workspace = 1,
            .numbered = std::nullopt,
            .owner = 10,
            .monitor_x = 1920,
            .monitor_y = -1080,
            .monitor_width = 1920,
            .monitor_height = 1080};
        CHECK(COMPOSITION.endpoint().owner == 10);
        CHECK(COMPOSITION.endpoint().monitor_y == -1080);
        CHECK(COMPOSITION.endpoint().monitor_width == 1920);
    }

    void projected_topology_preserves_native_handoff() {
        for (const auto LAYOUT : {Layout::Monitor, Layout::All, Layout::SolarSystem}) {
            for (const double ASPECT : {.6, 1., 16. / 9, 2.4}) {
                SceneOptions options;
                options.planet_layout = LAYOUT;
                auto from = numbered(1, 1, 0);
                auto to = numbered(3, 3, 1);
                from.owner = to.owner = 10;
                from.monitor_width = to.monitor_width = 1920;
                from.monitor_height = to.monitor_height = 1080;
                std::array<JourneyEndpoint, 5> inventory{};
                constexpr std::array<std::uint32_t, 5> NUMBERS{2, 4, 5, 6, 7};
                for (std::size_t index = 0; index < inventory.size(); ++index) {
                    inventory[index] = numbered(NUMBERS[index], NUMBERS[index], 0);
                    inventory[index].owner = index % 2 == 0 ? 10 : 20;
                    inventory[index].monitor_x = index % 2 == 0 ? 0 : 1920;
                    inventory[index].monitor_width = index % 2 == 0 ? 1920 : 1280;
                    inventory[index].monitor_height = index % 2 == 0 ? 1080 : 720;
                }
                inventory.back().numbered.reset();
                inventory.back().placement = 65;
                const auto JOURNEY = make_journey(from, to, options, ASPECT);
                const auto RELATIVE = [](Vec3 position, const Frame &frame) {
                    return Vec3{position.x - frame.world_origin.x,
                        position.y - frame.world_origin.y,
                        position.z - frame.world_origin.z};
                };
                for (const double PROGRESS : {0., .08, .92, 1.}) {
                    const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                    const auto NATIVE =
                        PROGRESS < .5 ? FRAME.source_center : FRAME.destination_center;
                    CHECK(!neighbor_visible(FRAME, {NATIVE.x, NATIVE.y, NATIVE.z + 1.5}, .75));
                    CHECK(neighbor_visible(FRAME, {NATIVE.x, NATIVE.y, NATIVE.z - 3}, .75));
                    for (const auto &endpoint : inventory) {
                        const auto ANCHOR = layout_position(endpoint, options, ASPECT);
                        CHECK(close(
                            ANCHOR, layout_position(endpoint, options, ASPECT == .6 ? 2.4 : .6)));
                        const auto CENTER = RELATIVE(ANCHOR, FRAME);
                        if (!neighbor_visible(FRAME, CENTER, .75)) {
                            continue;
                        }
                        const auto CLIP =
                            transform(FRAME.view_projection, {CENTER.x, CENTER.y, CENTER.z, 1});
                        const auto NATIVE_CLIP =
                            transform(FRAME.view_projection, {NATIVE.x, NATIVE.y, NATIVE.z, 1});
                        CHECK(CLIP[3] >= NATIVE_CLIP[3] || CLIP[3] <= 0 ||
                              std::abs(CLIP[0]) > CLIP[3] || std::abs(CLIP[1]) > CLIP[3]);
                    }
                }
                unsigned revealed = 0;
                for (const auto &endpoint : inventory) {
                    bool visible = false;
                    for (int sample = 32; sample <= 68; ++sample) {
                        const double PROGRESS = sample / 100.;
                        const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                        const auto CENTER =
                            RELATIVE(layout_position(endpoint, options, ASPECT), FRAME);
                        const auto CLIP =
                            transform(FRAME.view_projection, {CENTER.x, CENTER.y, CENTER.z, 1});
                        visible = visible || (CLIP[3] > 0 && std::abs(CLIP[0]) < CLIP[3] &&
                                                 std::abs(CLIP[1]) < CLIP[3]);
                    }
                    revealed += static_cast<unsigned>(visible);
                }
                CHECK(revealed >= 1);  // Local workspace 2 is on the actual orbital leg.
            }
        }
    }

    void origin_and_named_handoffs_have_no_foreground_inventory() {
        constexpr std::array<std::uint64_t, 7> SLOTS{0, 2, 4, 6, 65, 127, 255};
        constexpr std::array<Vec3, 7> SAMPLES{Vec3{},
            Vec3{.x = 1},
            Vec3{.x = -1},
            Vec3{.y = 1},
            Vec3{.y = -1},
            Vec3{.z = 1},
            Vec3{.z = -1}};
        const auto ENDPOINT = [](std::uint64_t slot) {
            return JourneyEndpoint{.identity = static_cast<std::int64_t>(slot + 1),
                .placement = slot,
                .owner = 1,
                .monitor_width = 1920,
                .monitor_height = 1080};
        };
        constexpr double ASPECT = 16. / 9;
        constexpr double RADIUS = .75;
        for (const auto LAYOUT : {Layout::Monitor, Layout::All, Layout::SolarSystem}) {
            SceneOptions options;
            options.planet_layout = LAYOUT;
            for (const auto SOURCE : {0ULL, 65ULL, 255ULL}) {
                const auto JOURNEY = make_journey(ENDPOINT(SOURCE), ENDPOINT(4), options, ASPECT);
                for (const double PROGRESS : {0., 1.}) {
                    const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                    const auto NATIVE =
                        PROGRESS == 0 ? FRAME.source_center : FRAME.destination_center;
                    const auto DESKTOP = transform(
                        FRAME.view_projection, {NATIVE.x, NATIVE.y, NATIVE.z + FRAME.radius, 1});
                    for (const auto SLOT : SLOTS) {
                        if (SLOT == SOURCE || SLOT == 4) {
                            continue;
                        }
                        const auto WORLD = layout_position(ENDPOINT(SLOT), options, ASPECT);
                        const Vec3 CENTER{WORLD.x - FRAME.world_origin.x,
                            WORLD.y - FRAME.world_origin.y,
                            WORLD.z - FRAME.world_origin.z};
                        if (!neighbor_visible(FRAME, CENTER, RADIUS)) {
                            continue;
                        }
                        for (const auto SAMPLE : SAMPLES) {
                            const auto CLIP = transform(FRAME.view_projection,
                                {CENTER.x + (SAMPLE.x * RADIUS * ASPECT),
                                    CENTER.y + (SAMPLE.y * RADIUS),
                                    CENTER.z + (SAMPLE.z * RADIUS),
                                    1});
                            const bool ONSCREEN = CLIP[3] > 0 && std::abs(CLIP[0]) < CLIP[3] &&
                                                  std::abs(CLIP[1]) < CLIP[3];
                            CHECK(!ONSCREEN || CLIP[2] / CLIP[3] >= DESKTOP[2] / DESKTOP[3]);
                        }
                    }
                }
            }
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
        // All four material edges meet the geometric hemisphere silhouette,
        // not an interior chart boundary on a larger backing sphere.
        for (double const COORD : {0., .2, .5, .8, 1.}) {
            for (const auto UV : {std::array{0., COORD},
                     std::array{1., COORD},
                     std::array{COORD, 0.},
                     std::array{COORD, 1.}}) {
                const auto EDGE = make_surface_vertex(UV[0], UV[1], 2, 1, .7);
                CHECK(close(EDGE.sphere_position[2], 0));
                CHECK(close(std::hypot(EDGE.sphere_position[0], EDGE.sphere_position[1]), 1));
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

    void solar_packing_has_no_overlap() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        options.planet_separation = .1;  // The physical non-intersection bound wins.
        options.elevation = .2;
        std::array<Vec3, 256> centers{};
        for (std::size_t index = 0; index < centers.size(); ++index) {
            centers[index] = layout_position(
                numbered(
                    static_cast<std::int64_t>(index + 1), static_cast<std::uint32_t>(index + 1), 0),
                options);
            for (std::size_t previous = 0; previous < index; ++previous) {
                CHECK(length({centers[index].x - centers[previous].x,
                          centers[index].y - centers[previous].y,
                          0}) >= 2.4 - 1e-5);
            }
        }
        const std::array<std::uint32_t, 6> EXTREMES{
            0, 1, 2, UINT32_MAX - 2, UINT32_MAX - 1, UINT32_MAX};
        for (std::size_t index = 0; index < EXTREMES.size(); ++index) {
            const auto FIRST = layout_position(
                numbered(INT64_MIN + static_cast<std::int64_t>(index), EXTREMES[index], 0),
                options);
            for (std::size_t previous = 0; previous < index; ++previous) {
                const auto SECOND = layout_position(
                    numbered(
                        INT64_MIN + static_cast<std::int64_t>(previous), EXTREMES[previous], 0),
                    options);
                CHECK(length({FIRST.x - SECOND.x, FIRST.y - SECOND.y, 0}) >= 2.4 - 1e-4);
            }
        }
    }
    void solar_paths_preserve_gaps_and_reveal_real_intermediates() {
        SceneOptions options;
        options.planet_layout = Layout::SolarSystem;
        const auto FIRST = numbered(1, 1, 0);
        const auto FIFTH = numbered(5, 5, 2);
        for (double const ASPECT : {.6, 1., 16. / 9, 2.4}) {
            const auto JOURNEY = make_journey(FIRST, FIFTH, options, ASPECT);
            CHECK(close(JOURNEY.destination_center, layout_position(FIFTH, options, ASPECT)));
            auto changed_rank = FIFTH;
            changed_rank.logical_order = 900;
            CHECK(
                close(layout_position(changed_rank, options, ASPECT), JOURNEY.destination_center));
            const auto REVERSE = make_journey(FIFTH, FIRST, options, ASPECT);
            CHECK(close(REVERSE.source_center, JOURNEY.destination_center));
            CHECK(close(REVERSE.destination_center, JOURNEY.source_center));
            const auto MID = evaluate(JOURNEY, .5, ASPECT);
            CHECK(std::hypot(MID.camera_position.x, MID.camera_position.y) > 1);
            CHECK(std::abs(JOURNEY.destination_center.z - JOURNEY.source_center.z) > .01);
            CHECK(std::abs(JOURNEY.destination_center.y - JOURNEY.source_center.y) > 1);
            // Common 1 -> 3 flight reveals the actual nearby workspace 2 at a
            // readable scale. Orbital numeric intermediates are not a row.
            const auto LOCAL = make_journey(FIRST, numbered(3, 3, 2), options, ASPECT);
            const auto ANCHOR = layout_position(numbered(2, 2, 1), options, ASPECT);
            bool visible = false;
            for (int sample = 32; sample <= 68; ++sample) {
                const auto FRAME = evaluate(LOCAL, sample / 100., ASPECT);
                const auto CLIP = transform(FRAME.view_projection,
                    {ANCHOR.x - FRAME.world_origin.x,
                        ANCHOR.y - FRAME.world_origin.y,
                        ANCHOR.z - FRAME.world_origin.z,
                        1});
                visible = visible || (CLIP[3] > 0 && std::abs(CLIP[0]) < CLIP[3] &&
                                         std::abs(CLIP[1]) < CLIP[3] &&
                                         720 * 2.414213562373095 * .75 / CLIP[3] >= 30);
                CHECK(FRAME.camera_position.z < 40);
            }
            CHECK(visible);
            for (double const PROGRESS : {0., 1.}) {
                const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                CHECK(close(FRAME.camera_position, {0, 0, FRAME.radius + 2.414213562373095}));
                const auto CENTER = PROGRESS == 0 ? FRAME.source_center : FRAME.destination_center;
                const auto CLIP =
                    transform(FRAME.view_projection, {ASPECT, 1, CENTER.z + FRAME.radius, 1});
                CHECK(close(CLIP[0] / CLIP[3], 1));
                CHECK(close(CLIP[1] / CLIP[3], 1));
            }
            const auto FORWARD = evaluate(JOURNEY, .43, ASPECT);
            (void)evaluate(JOURNEY, .71, ASPECT);
            const auto REVERSED = evaluate(JOURNEY, .43, ASPECT);
            CHECK(FORWARD.view_projection == REVERSED.view_projection);
            CHECK(close(FORWARD.world_origin, REVERSED.world_origin));
        }
        CHECK(close(layout_position(FIFTH, options, (2560. / 1.5) / (1440. / 1.5)),
            layout_position(FIFTH, options, 2560. / 1440.)));
        const auto WIDE = layout_position(FIFTH, options, 2);
        CHECK(close(WIDE, layout_position(FIFTH, options, 1)));
        options.solar_vertical_spread = 0;
        const auto BASE = layout_position(FIFTH, options);
        options.solar_vertical_spread = 1;
        CHECK(close(layout_position(FIFTH, options).y, BASE.y * 2));
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
                CHECK(length(centers[index]) < 500);  // Bounded by orbit radius, not lifetime ID.
                for (std::size_t previous = 0; previous < index; ++previous) {
                    CHECK(length({centers[index].x - centers[previous].x,
                              centers[index].y - centers[previous].y,
                              centers[index].z - centers[previous].z}) >= 2.4 - 1e-5);
                }
                const auto ZERO = layout_position(numbered(0, 0, 0), options, 1.8);
                CHECK(length({centers[index].x - ZERO.x,
                          centers[index].y - ZERO.y,
                          centers[index].z - ZERO.z}) >= 2.4 - 1e-5);
            }
        }
        const JourneyEndpoint NAMED{.identity = 20, .placement = 17};
        options.solar_vertical_spread = 0;
        const auto FLAT = layout_position(NAMED, options);
        options.solar_vertical_spread = .65;
        CHECK(std::abs(layout_position(NAMED, options).y - FLAT.y) > .1);
        const JourneyEndpoint EXTREME_SPECIAL{
            .identity = INT64_MIN, .special = true, .placement = UINT64_MAX};
        const auto EXTREME = make_journey(NAMED, EXTREME_SPECIAL, options, 1.8);
        for (double const PROGRESS : {0., .2, .32, .5, .68, .8, 1.}) {
            const auto FRAME = evaluate(EXTREME, PROGRESS, 1.8);
            CHECK(std::isfinite(FRAME.world_origin.x) && std::isfinite(FRAME.world_origin.y) &&
                  std::isfinite(FRAME.world_origin.z));
            CHECK(FRAME.camera_position.z < 150);
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
                CHECK(std::isfinite(FRAME.world_origin.x));
                CHECK(std::isfinite(FRAME.world_origin.y));
                CHECK(std::isfinite(FRAME.camera_position.z));
                CHECK(FRAME.camera_position.z < 300);
                CHECK(length(FRAME.camera_target) <= (.4 * options.planet_separation) + 1e-5);
                CHECK(step / 20. > .32 || FRAME.travel_progress == 0);
                CHECK(step / 20. < .68 || FRAME.travel_progress == 1);
                for (const auto VALUE : FRAME.view_projection) {
                    CHECK(std::isfinite(VALUE));
                }
                if (step == 0 || step == 20) {
                    const auto CENTER = step == 0 ? FRAME.source_center : FRAME.destination_center;
                    CHECK(close(CENTER, {}));
                    CHECK(close(FRAME.camera_position, {0, 0, FRAME.radius + 2.414213562373095}));
                    CHECK(close(FRAME.camera_target, {}));
                }
            }
        }
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

    void placements_survive_recreation_and_resolve_collisions() {
        using hyprimate::workspace::CompositionPlacements;
        CompositionPlacements placements;
        hyprimate::workspace::CompositionIdentities<std::shared_ptr<int>, std::weak_ptr<int>>
            lifetimes;
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
        CHECK(close(
            layout_position(SPECIAL_BEFORE, options), layout_position(special_after, options)));
        CHECK(!close(
            layout_position(NAMED_BEFORE, options), layout_position(SPECIAL_BEFORE, options)));
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
                        CHECK(std::isfinite(POINT.position.x) && std::isfinite(POINT.position.y) &&
                              std::isfinite(POINT.position.z));
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
                            // The front is a graph with bounded XY; it has no
                            // extended exterior sheet or receding hidden body.
                            const double HALF_WIDTH = (1 - CURVATURE) * ASPECT + CURVATURE * RADIUS;
                            const double HALF_HEIGHT = 1 - CURVATURE + CURVATURE * RADIUS;
                            CHECK(std::abs(POINT.position.x) <= HALF_WIDTH + 1e-12);
                            CHECK(std::abs(POINT.position.y) <= HALF_HEIGHT + 1e-12);
                            CHECK(POINT.position.z >= RADIUS * (1 - CURVATURE) - 1e-12);
                            CHECK(POINT.position.z <= RADIUS + 1e-12);
                            if (COORD_U == 0 || COORD_U == 1 || COORD_V == 0 || COORD_V == 1)
                                CHECK(close(POINT.position.z, RADIUS * (1 - CURVATURE), 1e-12));
                            if (CURVATURE == 1) CHECK(close(length(POINT.position), RADIUS, 1e-10));
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
                    CHECK(close(POINT.normal,
                        {CROSS.x / LENGTH, CROSS.y / LENGTH, CROSS.z / LENGTH},
                        1e-5));
                }
            }
        }
    }
}  // namespace

int main() {
    layouts_are_deterministic_and_locked();
    topology_scope_and_geometry_are_independent();
    projected_topology_preserves_native_handoff();
    origin_and_named_handoffs_have_no_foreground_inventory();
    geometry_has_native_endpoints();
    projection_is_spherical_and_aspect_correct();
    motion_is_continuous_and_direct();
    solar_packing_has_no_overlap();
    solar_paths_preserve_gaps_and_reveal_real_intermediates();
    named_solar_cells_are_local_stable_and_disjoint();
    distant_planets_keep_stable_coordinates_and_framing();
    named_and_special_direction_matches_navigation();
    bending_precedes_reveal_and_approach_grows();
    flattening_tracks_projected_native_coverage();
    placements_survive_recreation_and_resolve_collisions();
    angular_bend_is_convex_and_stable();
    angular_bend_normals_match_tangents_and_rotation();
    return g_failures == 0 ? 0 : 1;
}
