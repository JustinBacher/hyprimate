#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Check.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceTurn.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

using namespace hyprimate::workspace::planet_hop;
using hyprimate::workspace::JourneyEndpoint;

namespace {
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
}  // namespace

namespace planet_scene_layout_tests {
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
}  // namespace planet_scene_layout_tests
