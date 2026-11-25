#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>

#include "Check.hpp"
#include "shared/Core.hpp"
#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleCurl.hpp"
#include "window/bubble/BubbleRupture.hpp"
#include "window/bubble/BubbleRuptureEvents.hpp"
#include "window/bubble/BubbleSheets.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{.x = 1000, .y = 700};

    void advance_for(Bubble &bubble, double seconds, double step = 0.01) {
        while (seconds > 1e-12) {
            const double DELTA_TIME = std::min(seconds, step);
            bubble.advance(DELTA_TIME);
            seconds -= DELTA_TIME;
        }
    }
    void rupture_covers_corners() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Close, BubbleDuration{1}, 19, SIZE);
            auto film = bubble.frame(SIZE).window;
            CHECK(film.visibility == 1 && bubble.frame(SIZE).shell.visibility == 0);
            CHECK(std::get<0>(film.holes).centre.x != 0 && std::get<0>(film.holes).centre.y != 0);
            CHECK(std::get<0>(film.holes).radius == 0);
            advance_for(bubble, 0.9);
            film = bubble.frame(SIZE).window;
            const auto &hole = std::get<0>(film.holes);
            const double FARTHEST = std::hypot(
                (SIZE.x / 2) + std::abs(hole.centre.x), (SIZE.y / 2) + std::abs(hole.centre.y));
            CHECK(hole.radius * .95 >= FARTHEST);
            advance_for(bubble, 0.2);
            CHECK(bubble.closed() && !bubble.active());
            CHECK(bubble.frame(SIZE).window.visibility == 0 &&
                  bubble.frame(SIZE).shell.visibility == 0);
            const Box BOUNDS = bubble.bounds(SIZE);
            CHECK(BOUNDS.w == 0 && BOUNDS.h == 0);
        }
    }

    void localized_rupture_and_coverage() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Close, BubbleDuration{.42}, 19, SIZE);
            advance_for(bubble, .42 * .149);
            auto surface = bubble.frame(SIZE).window;
            CHECK(std::get<0>(surface.holes).radius == 0);
            CHECK(std::get<0>(surface.holes).precursor > .9);
            CHECK(surface.film == 0);
            const auto *hole = std::next(surface.holes.begin());
            for (std::size_t i = 1; i < surface.hole_count; ++i, ++hole) {
                Bubble probe;
                probe.begin(Transition::Close, BubbleDuration{.42}, 19, SIZE);
                advance_for(probe, .42 * hole->start);
                const auto AT_BIRTH = probe.frame(SIZE).window;
                CHECK(AT_BIRTH.holes[i].start > std::get<0>(AT_BIRTH.holes).start);
                const auto PRIOR_HOLES = std::span(AT_BIRTH.holes).first(i);
                const auto CENTRE = AT_BIRTH.holes[i].centre;
                for (const auto &prior_hole : PRIOR_HOLES) {
                    CHECK(
                        std::hypot(CENTRE.x - prior_hole.centre.x, CENTRE.y - prior_hole.centre.y) >
                        bubble_hole_boundary(
                            prior_hole, bubble_hole_azimuth(prior_hole, CENTRE, false)));
                }
            }
            advance_for(bubble, .42 * .21);
            const auto FIRST = bubble.frame(SIZE).window;
            advance_for(bubble, .42 * .10);
            const auto SECOND = bubble.frame(SIZE).window;
            CHECK(std::abs((std::get<0>(SECOND.holes).radius - std::get<0>(FIRST.holes).radius) -
                           (SECOND.speed * .1)) < SECOND.speed * .006);
            advance_for(bubble, .42 * .151);
            surface = bubble.frame(SIZE).window;
            for (int row = -5; row <= 5; ++row) {
                for (int col = -5; col <= 5; ++col) {
                    const Vec3 POINT{.x = SIZE.x * .1 * col, .y = SIZE.y * .1 * row, .z = 0};
                    bool covered = false;
                    for (const auto &hole : std::span(surface.holes).first(surface.hole_count)) {
                        covered |=
                            std::hypot(POINT.x - hole.centre.x, POINT.y - hole.centre.y) <=
                            bubble_hole_boundary(hole, bubble_hole_azimuth(hole, POINT, false));
                    }
                    CHECK(covered);
                }
            }
        }
    }

    void precursor_and_remnants() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 42, SIZE);
        auto surface = bubble.frame(SIZE).window;
        CHECK(surface.film == 0 && surface.thickness >= 380 && surface.thickness <= 650);
        advance_for(bubble, .42 * .02);
        CHECK(std::get<0>(bubble.frame(SIZE).window.holes).precursor > 0);
        advance_for(bubble, .42 * .13);
        CHECK(std::get<0>(bubble.frame(SIZE).window.holes).radius < 1e-8);
        advance_for(bubble, 1.0 / 60);
        surface = bubble.frame(SIZE).window;
        CHECK(std::get<0>(surface.holes).radius <
              .05 * std::min(surface.half_size.x, surface.half_size.y));
        advance_for(bubble, .42 * .20);
        surface = bubble.frame(SIZE).window;
        std::array<BubbleParticle, K_BUBBLE_MAX_PARTICLES> particles{};
        auto count = bubble_particles(surface, particles);
        std::size_t ligaments = 0;
        for (const auto &particle : std::span(particles).first(count)) {
            ligaments += static_cast<std::size_t>(particle.tail_radius > 0);
        }
        CHECK(ligaments > 0);
        advance_for(bubble, .42 * .42);
        surface = bubble.frame(SIZE).window;
        count = bubble_particles(surface, particles);
        CHECK(count > 0);
        advance_for(bubble, .42 * .2);
        CHECK(bubble.closed());
        CHECK(bubble_particles(bubble.frame(SIZE).window, particles) == 0);
    }

    void seeded_flat_coverage() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            for (std::uint64_t seed = 0; seed < 16; ++seed) {
                Bubble bubble;
                bubble.begin(Transition::Close, BubbleDuration{.42}, seed, SIZE);
                advance_for(bubble, .42 * .62);
                const auto SURFACE = bubble.frame(SIZE).window;
                for (int row = -3; row <= 3; ++row) {
                    for (int col = -3; col <= 3; ++col) {
                        const Vec3 POINT{.x = col * SIZE.x / 6, .y = row * SIZE.y / 6, .z = 0};
                        bool covered = false;
                        for (const auto &hole :
                            std::span(SURFACE.holes).first(SURFACE.hole_count)) {
                            covered |=
                                std::hypot(POINT.x - hole.centre.x, POINT.y - hole.centre.y) <=
                                bubble_hole_boundary(hole, bubble_hole_azimuth(hole, POINT, false));
                        }
                        CHECK(covered);
                    }
                }
            }
        }
    }
    void impact_footprints_are_representable() {
        BubbleImpact impact{.position = {5, -3}, .radius = 2, .drift = {3, 4}};
        const auto bounds = impact.bounds({100, 50}, 0.5);
        CHECK(std::abs(bounds.w - 14.2) < 1e-12 && bounds.h == bounds.w);
        CHECK(std::abs(bounds.x + 0.5 * bounds.w - 105) < 1e-12);
        CHECK(std::abs(bounds.y + 0.5 * bounds.h - 47) < 1e-12);
        const double max = std::numeric_limits<double>::max();
        // A finite radius can still overflow the diameter or the far corner.
        impact.radius = max / 2;
        CHECK(!is_usable(impact.bounds({}, 0.5)));
        impact.radius = max / 8;
        CHECK(!is_usable(impact.bounds({max, 0}, 0.5)));
        CHECK(!is_usable(impact.bounds({0, -max}, 0.5)));
    }

    void impacts_use_animation_time() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 19, SIZE);
        const auto FIRST = bubble.frame(SIZE).window;
        std::array<BubbleImpact, K_BUBBLE_MAX_IMPACTS> first_impacts{};
        std::array<BubbleImpact, K_BUBBLE_MAX_IMPACTS> later_impacts{};
        const auto FIRST_COUNT = bubble_impacts(FIRST, first_impacts);
        CHECK(FIRST_COUNT <= K_BUBBLE_MAX_IMPACTS);
        advance_for(bubble, .01);
        const auto LATER_COUNT = bubble_impacts(bubble.frame(SIZE).window, later_impacts);
        CHECK(FIRST_COUNT == LATER_COUNT);
        auto later = std::span(later_impacts).begin();
        for (const auto &first : std::span(first_impacts).first(FIRST_COUNT)) {
            CHECK(std::abs((first.delay - later->delay) - .01) < 1e-10);
            CHECK(first.position.x == later->position.x && first.position.y == later->position.y);
            ++later;
        }
    }

    void impacts_follow_droplets() {
        for (const auto SEED : {42ULL, 7ULL, 99ULL, 1234ULL}) {
            for (const auto DIRECTION : {Transition::Open, Transition::Close}) {
                const bool OPENING = DIRECTION == Transition::Open;
                Bubble bubble;
                bubble.begin(
                    DIRECTION, BubbleDuration{OPENING ? .62 : .42}, SEED, {.x = 900, .y = 560});
                if (OPENING) {
                    advance_for(bubble, .62 * .31);
                }
                auto frame = bubble.frame({.x = 900, .y = 560});
                auto surface = frame.window;
                std::array<BubbleImpact, K_BUBBLE_MAX_IMPACTS> impacts{};
                const auto COUNT = bubble_impacts(surface, impacts);
                CHECK(COUNT <= 2U);
                if (OPENING) {
                    CHECK(COUNT == 0);
                }
                for (const auto &impact : std::span(impacts).first(COUNT)) {
                    CHECK(impact.radius >= 1.5 && impact.radius <= 3.5);
                }
                if (COUNT == 0U) {
                    continue;
                }
                CHECK(std::get<0>(impacts).delay > .00002);
                advance_for(bubble, std::get<0>(impacts).delay - .00001);
                frame = bubble.frame({.x = 900, .y = 560});
                surface = frame.window;
                std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> particles{};
                const auto BEFORE = detail::bubble_particles_with_ids(surface, particles);
                std::uint32_t source = 0;
                bool found = false;
                for (const auto &drop : std::span(particles).first(BEFORE)) {
                    if (drop.particle.blur > .95 &&
                        std::hypot(drop.particle.centre.x - std::get<0>(impacts).position.x,
                            drop.particle.centre.y - std::get<0>(impacts).position.y) < 1) {
                        source = drop.id;
                        found = true;
                        break;
                    }
                }
                CHECK(found);
                advance_for(bubble, .00002);
                frame = bubble.frame({.x = 900, .y = 560});
                surface = frame.window;
                const auto AFTER = detail::bubble_particles_with_ids(surface, particles);
                const auto *particle = particles.begin();
                for (std::size_t i = 0; found && i < AFTER; ++i, ++particle) {
                    CHECK(particle->id != source);
                }
            }
        }
    }

    void particle_continuity_and_evaporation() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 19, SIZE);
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> previous{};
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> current{};
        std::size_t old_count = 0;
        std::size_t peak = 0;
        bool burst = false;
        for (int frame = 0; frame < 101; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubble_particles_with_ids(SURFACE, current);
            peak = std::max(peak, COUNT);
            for (const auto &current_particle : std::span(current).first(COUNT)) {
                burst |= current_particle.id >= 1000 && current_particle.id < 2000;
                for (const auto &previous_particle : std::span(previous).first(old_count)) {
                    if (previous_particle.id != current_particle.id) {
                        continue;
                    }
                    const auto &prior_particle = previous_particle.particle;
                    const bool ATTACHED = current_particle.id >= 2000 && current_particle.id < 3000;
                    // Attached ligaments follow the rolling film, not just the puncture front.
                    const double SPEED_LIMIT = SURFACE.speed * (ATTACHED ? 2 : 1);
                    CHECK(std::hypot(current_particle.particle.centre.x - prior_particle.centre.x,
                              current_particle.particle.centre.y - prior_particle.centre.y) <=
                          (SPEED_LIMIT * .01) + 2);
                    if (ATTACHED) {
                        CHECK(current_particle.particle.radius + 1e-9 >= prior_particle.radius);
                    } else if (current_particle.particle.blur == 0 && prior_particle.blur == 0) {
                        CHECK(current_particle.particle.radius <= prior_particle.radius + 1e-9);
                    }
                }
            }
            previous = current;
            old_count = COUNT;
            bubble.advance(.0042);
        }
        CHECK(peak > 0 && peak <= K_BUBBLE_MAX_PARTICLES);
        CHECK(burst);
        CHECK(old_count == 0);
    }
    void fingers_start_gradually() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 42, SIZE);
        advance_for(bubble, .42 * .17);
        auto surface = bubble.frame(SIZE).window;
        CHECK(std::get<0>(surface.holes).radius < std::max(24.0, .08 * surface.half_size.y));
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> particles{};
        CHECK(detail::bubble_particles_with_ids(surface, particles) == 0);
        Bubble shell;
        shell.begin(Transition::Open, BubbleDuration{.62}, 42, SIZE);
        advance_for(shell, .62 * .32);
        const auto EARLY_SHELL = shell.frame(SIZE).shell;
        CHECK(std::get<0>(EARLY_SHELL.holes).radius * EARLY_SHELL.half_size.x <
              std::max(24.0, .08 * EARLY_SHELL.half_size.x));
        CHECK(detail::bubble_particles_with_ids(EARLY_SHELL, particles) == 0);
        bool varied = false;
        bool satellite = false;
        for (int frame = 0; frame < 20; ++frame) {
            bubble.advance(.005);
            surface = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubble_particles_with_ids(surface, particles);
            double shortest = 1e9;
            double longest = 0;
            std::size_t forming = 0;
            const auto *particle = particles.begin();
            for (std::size_t i = 0; i < COUNT; ++i, ++particle) {
                if (particle->id >= 10000) {
                    const auto SOURCE = (particle->id - 10000) / 2;
                    const auto *candidate = particles.begin();
                    for (std::size_t j = 0; j < COUNT; ++j, ++candidate) {
                        if (candidate->id != SOURCE) {
                            continue;
                        }
                        satellite = true;
                        CHECK(particle->particle.radius < candidate->particle.radius);
                    }
                }
                const auto &drop = *particle;
                if (drop.id < 2000 || drop.id >= 3000) {
                    continue;
                }
                ++forming;
                CHECK(drop.particle.radius > drop.particle.tail_radius);
                const double LENGTH = std::hypot(drop.particle.centre.x - drop.particle.tail.x,
                    drop.particle.centre.y - drop.particle.tail.y);
                shortest = std::min(shortest, LENGTH);
                longest = std::max(longest, LENGTH);
            }
            varied |= forming >= 3 && longest - shortest > 3;
        }
        CHECK(varied && satellite);
    }

    void curled_geometry_and_attachments() {
        BubbleSurface surface;
        surface.half_size = {.x = 300, .y = 200};
        surface.aureole = 1;
        BubbleHole hole;
        hole.phase = {.2, -.4, 0, 0};
        const auto SECTION = bubble_curl_section(surface, hole, 100, .7);
        const Vec3 BASE{.x = 100, .y = 20, .z = 0};
        const Vec3 OUTWARD{.x = 1, .y = 0, .z = 0};
        const Vec3 NORMAL{.x = 0, .y = 0, .z = 1};
        const auto ROOT = bubble_curl_sample(BASE, OUTWARD, NORMAL, SECTION, 0);
        CHECK(ROOT.position.x == BASE.x && ROOT.position.y == BASE.y && ROOT.position.z == BASE.z);
        CHECK(ROOT.normal.x == 0 && ROOT.normal.y == 0 && ROOT.normal.z == 1);
        for (int i = 0; i <= 20; ++i) {
            const auto SAMPLE = bubble_curl_sample(BASE, OUTWARD, NORMAL, SECTION, i / 20.0);
            CHECK(std::abs(std::hypot(SAMPLE.normal.x, SAMPLE.normal.z) - 1) < 1e-12);
            CHECK(std::hypot(SAMPLE.position.x - BASE.x, SAMPLE.position.z - BASE.z) <=
                  SECTION.width + 1e-9);
        }
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 42, SIZE);
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> particles{};
        bool attached = false;
        for (int frame = 0; frame < 90; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubble_particles_with_ids(SURFACE, particles);
            for (const auto &drop : std::span(particles).first(COUNT)) {
                if (drop.id < 2000 || drop.id >= 3000) {
                    continue;
                }
                const std::size_t HOLE_INDEX = (drop.id - 2000) / 104;
                const auto &source_hole = SURFACE.holes[HOLE_INDEX];
                const double PHI = std::atan2(drop.particle.tail.y - source_hole.centre.y,
                    drop.particle.tail.x - source_hole.centre.x);
                const auto EXPECTED = bubble_peel_sample(SURFACE, source_hole, PHI, 1);
                CHECK(std::hypot(EXPECTED.position.x - drop.particle.tail.x,
                          EXPECTED.position.y - drop.particle.tail.y) < 1e-6);
                attached = true;
            }
            bubble.advance(.0042);
        }
        CHECK(attached);
    }

    void spherical_peel_azimuth_round_trip() {
        BubbleSurface surface;
        surface.spherical = true;
        surface.half_size = {.x = 200, .y = 200};
        surface.aureole = .8;
        surface.hole_count = 4;
        const std::array<double, 4> heights{.2, -.899, .9, -.96};
        for (std::size_t i = 0; i < surface.hole_count; ++i) {
            auto &hole = surface.holes[i];
            const double Z = heights[i];
            hole.centre = {.x = std::sqrt(1 - (Z * Z)), .y = 0, .z = Z};
            hole.radius = .45 + (.03 * static_cast<double>(i));
            hole.amplitude = {.03, -.02, .01, .005};
            hole.phase = {.2, -.4, .7, -1.1};
        }
        // Expected tangent signs for the four fixed centres, including z == .9.
        const std::array<double, 4> tangent_y{1, 1, -1, 1};
        for (std::size_t i = 0; i < surface.hole_count; ++i) {
            const auto &hole = surface.holes[i];
            for (const double PHI : std::array{-3.0, -1.2, 0.0, .7, 2.8}) {
                const auto SAMPLE = bubble_peel_sample(surface, hole, PHI, 0);
                const double ACTUAL = bubble_hole_azimuth(hole, SAMPLE.position, true);
                CHECK(std::abs(std::remainder(ACTUAL - PHI, 2 * std::numbers::pi)) < 1e-12);
                const double BOUNDARY = bubble_hole_boundary(hole, PHI);
                const auto SECTION = bubble_curl_section(surface, hole, BOUNDARY * 200, PHI);
                const double ROOT = BOUNDARY + (SECTION.width / 200);
                // These centres lie in the x-z plane. Independently, z-axis cross centre
                // points along +y; x-axis cross centre points along -sign(z) y.
                const double TANGENT_Y = tangent_y[i];
                const double RADIAL = 200 * std::sin(ROOT);
                const double AXIAL_DISTANCE = 200 * std::cos(ROOT);
                const Vec3 EXPECTED{
                    .x = (hole.centre.x * AXIAL_DISTANCE) -
                         (hole.centre.z * TANGENT_Y * std::sin(PHI) * RADIAL),
                    .y = TANGENT_Y * std::cos(PHI) * RADIAL,
                    .z = (hole.centre.z * AXIAL_DISTANCE) +
                         (hole.centre.x * TANGENT_Y * std::sin(PHI) * RADIAL),
                };
                CHECK(std::abs(SAMPLE.position.x - EXPECTED.x) < 1e-10);
                CHECK(std::abs(SAMPLE.position.y - EXPECTED.y) < 1e-10);
                CHECK(std::abs(SAMPLE.position.z - EXPECTED.z) < 1e-10);
                const double EXPECTED_AZIMUTH = bubble_hole_azimuth(hole, EXPECTED, true);
                CHECK(std::abs(std::remainder(EXPECTED_AZIMUTH - PHI, 2 * std::numbers::pi)) <
                      1e-12);
                const double AXIAL = ((hole.centre.x * SAMPLE.position.x) +
                                         (hole.centre.y * SAMPLE.position.y) +
                                         (hole.centre.z * SAMPLE.position.z)) /
                                     200;
                CHECK(std::abs(AXIAL - std::cos(ROOT)) < 1e-12);
                CHECK(std::abs(std::hypot(SAMPLE.position.x, SAMPLE.position.y, SAMPLE.position.z) -
                               200) < 1e-10);
                CHECK(std::abs(SAMPLE.normal.x - (SAMPLE.position.x / 200)) < 1e-12);
                CHECK(std::abs(SAMPLE.normal.y - (SAMPLE.position.y / 200)) < 1e-12);
                CHECK(std::abs(SAMPLE.normal.z - (SAMPLE.position.z / 200)) < 1e-12);
            }
        }
    }

    void all_pairwise_merges_fit() {
        static_assert(K_BUBBLE_MAX_MERGES == 6);
        static_assert(K_BUBBLE_MAX_SHEETS == K_BUBBLE_MAX_MERGES + 1);
        BubbleSurface surface;
        surface.half_size = {.x = 200, .y = 200};
        surface.speed = 300;
        surface.hole_count = K_BUBBLE_HOLES;
        const double OFFSET = std::sqrt(5000.0);
        surface.holes[0].centre = {0, -OFFSET, 0};
        surface.holes[1].centre = {OFFSET, 0, 0};
        surface.holes[2].centre = {0, OFFSET, 0};
        surface.holes[3].centre = {-OFFSET, 0, 0};
        // The four fronts nearly meet at the centre. Recede there so both diagonal
        // merges survive. The first farthest corner belongs to holes 0 and 3; recede
        // there as well, leaving visible material inside the rectangular boundary.
        for (std::size_t i = 0; i < surface.holes.size(); ++i) {
            auto &hole = surface.holes[i];
            const double PHI = std::atan2(-hole.centre.y, -hole.centre.x);
            const double CORNER_PHI = std::atan2(-200 - hole.centre.y, -200 - hole.centre.x);
            const double DELTA = (i == 0 || i == 3)
                                     ? std::remainder(CORNER_PHI - PHI, 2 * std::numbers::pi)
                                     : 0;
            for (std::size_t mode = 0; mode < K_BUBBLE_RIM_MODES.size(); ++mode) {
                const double HALF = K_BUBBLE_RIM_MODES[mode] * DELTA * .5;
                hole.phase[mode] = std::numbers::pi - (K_BUBBLE_RIM_MODES[mode] * PHI) - HALF +
                                   (std::cos(HALF) < 0 ? std::numbers::pi : 0);
            }
        }
        const auto &EVENTS = detail::collapse(surface);
        CHECK(EVENTS.merge_count == K_BUBBLE_MAX_MERGES);
        const std::array<std::uint32_t, K_BUBBLE_MAX_MERGES> EXPECTED{
            1005, 1010, 1015, 1030, 1035, 1055};
        for (std::size_t i = 0; i < EXPECTED.size(); ++i) {
            CHECK(EVENTS.merges[i].id == EXPECTED[i]);
            CHECK(EVENTS.merges[i].time > 0 && EVENTS.merges[i].time < 1);
            CHECK(std::isfinite(EVENTS.merges[i].position.x));
            CHECK(std::isfinite(EVENTS.merges[i].position.y));
        }
        surface.closing = true;
        surface.aureole = 1;
        surface.clock = 1;
        std::array<BubbleSheet, K_BUBBLE_MAX_SHEETS> sheets{};
        const auto COUNT = bubble_sheets(surface, sheets);
        CHECK(COUNT == K_BUBBLE_MAX_SHEETS);
        const auto ACTUAL = std::span(sheets).first(COUNT);
        for (const auto ID : EXPECTED) {
            CHECK(std::ranges::any_of(ACTUAL, [ID](const BubbleSheet &sheet) {
                return sheet.id == ID;
            }));
        }
        CHECK(std::ranges::any_of(
            ACTUAL, [](const BubbleSheet &sheet) { return sheet.id == 1200; }));
    }

    void sheets_contract() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{2.0}, 19, SIZE);
        std::array<BubbleSheet, K_BUBBLE_MAX_SHEETS> sheets{};
        std::array<BubbleSheet, K_BUBBLE_MAX_SHEETS> again{};
        bool born = false;
        bool moved = false;
        bool expired = false;
        std::array<std::uint32_t, K_BUBBLE_MAX_SHEETS> retired{};
        std::size_t retired_count = 0;
        for (int frame = 0; frame < 999; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = bubble_sheets(SURFACE, sheets);
            CHECK(COUNT <= K_BUBBLE_MAX_SHEETS);
            CHECK(bubble_sheets(SURFACE, again) == COUNT);
            const auto *sheet_it = sheets.begin();
            auto again_it = std::span(again).begin();
            for (std::size_t i = 0; i < COUNT; ++i, ++sheet_it, ++again_it) {
                const auto &sheet = *sheet_it;
                CHECK(sheet.id == again_it->id && sheet.birth == again_it->birth);
                for (const auto RETIRED_ID : std::span(retired).first(retired_count)) {
                    if (RETIRED_ID == sheet.id) {
                        CHECK(sheet.scale == 0);
                    }
                }
                const auto RETIRED_IDS = std::span(retired).first(retired_count);
                if (sheet.scale == 0 &&
                    std::ranges::find(RETIRED_IDS, sheet.id) == RETIRED_IDS.end()) {
                    retired[retired_count++] = sheet.id;
                }
                CHECK(sheet.half_angle > 0 && sheet.half_angle <= .35);
                CHECK(sheet.scale >= 0 && sheet.scale <= 1);
                auto birth = SURFACE;
                birth.clock = sheet.birth;
                std::array<BubbleSheet, K_BUBBLE_MAX_SHEETS> at_birth{};
                const auto BIRTH_COUNT = bubble_sheets(birth, at_birth);
                for (const auto &birth_sheet : std::span(at_birth).first(BIRTH_COUNT)) {
                    if (birth_sheet.id != sheet.id) {
                        continue;
                    }
                    auto birth_hole = birth.holes[sheet.hole];
                    birth_hole.radius = sheet.radii[sheet.hole];
                    std::size_t mode = 0;
                    for (auto &amplitude : birth_hole.amplitude) {
                        amplitude =
                            sheet.onsets[sheet.hole] * (.014 - (.002 * static_cast<double>(mode)));
                        ++mode;
                    }
                    const auto SOURCE = bubble_peel_sample(birth, birth_hole, sheet.phi, .5);
                    const auto DETACHED = bubble_sheet_sample(birth, birth_sheet, sheet.phi, .5);
                    CHECK(std::hypot(SOURCE.position.x - DETACHED.position.x,
                              SOURCE.position.y - DETACHED.position.y) < 1e-8);
                    CHECK(std::abs(SOURCE.position.z - DETACHED.position.z) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.x - DETACHED.normal.x) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.y - DETACHED.normal.y) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.z - DETACHED.normal.z) < 1e-8);
                    const double BOUNDARY = bubble_hole_boundary(birth_hole, sheet.phi);
                    const double WIDTH =
                        bubble_curl_section(birth, birth_hole, BOUNDARY, sheet.phi).width;
                    const Vec3 MATERIAL{
                        .x =
                            birth_hole.centre.x + ((BOUNDARY + (WIDTH * .5)) * std::cos(sheet.phi)),
                        .y =
                            birth_hole.centre.y + ((BOUNDARY + (WIDTH * .5)) * std::sin(sheet.phi)),
                        .z = 0,
                    };
                    CHECK(bubble_sheet_contains(birth, sheet, MATERIAL));
                    born = true;
                }
                moved |= sheet.age > 0 && sheet.scale < 1 && sheet.scale > 0 &&
                         std::hypot(sheet.translation.x, sheet.translation.y) > 0;
                expired |= sheet.scale == 0;
                const auto BOX = bubble.bounds(SIZE);
                for (const double OFFSET : {-1.0, 0.0, 1.0}) {
                    for (const double FRACTION : {0.0, .5, 1.0}) {
                        const auto POINT = bubble_sheet_sample(
                            SURFACE, sheet, sheet.phi + (OFFSET * sheet.half_angle), FRACTION)
                                               .position;
                        CHECK(POINT.x + (SIZE.x * .5) >= BOX.x &&
                              POINT.x + (SIZE.x * .5) <= BOX.x + BOX.w);
                        CHECK(POINT.y + (SIZE.y * .5) >= BOX.y &&
                              POINT.y + (SIZE.y * .5) <= BOX.y + BOX.h);
                    }
                }
            }
            bubble.advance(.002);
        }
        CHECK(born && moved && expired);
    }

    void pinch_birth_is_continuous() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{.42}, 42, SIZE);
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> previous{};
        std::array<detail::IdentifiedParticle, K_BUBBLE_MAX_PARTICLES> current{};
        std::size_t old_count = 0;
        bool observed = false;
        for (int frame = 0; frame < 2000; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubble_particles_with_ids(SURFACE, current);
            for (const auto &current_entry : std::span(current).first(COUNT)) {
                if (current_entry.id >= 2000) {
                    continue;
                }
                for (const auto &previous_particle : std::span(previous).first(old_count)) {
                    if (previous_particle.id != current_entry.id + 2000) {
                        continue;
                    }
                    const auto &prior_particle = previous_particle.particle;
                    const auto &current_particle = current_entry.particle;
                    CHECK(std::hypot(prior_particle.centre.x - current_particle.centre.x,
                              prior_particle.centre.y - current_particle.centre.y) <
                          (SURFACE.speed * SURFACE.rate * .00021) + 1);
                    CHECK(std::hypot(prior_particle.tail.x - current_particle.tail.x,
                              prior_particle.tail.y - current_particle.tail.y) <
                          (SURFACE.speed * SURFACE.rate * .00021) + 1);
                    observed = true;
                }
            }
            previous = current;
            old_count = COUNT;
            bubble.advance(.00021);
        }
        CHECK(observed);
    }

    void sheets_keep_their_source_material() {
        BubbleSurface surface;
        surface.half_size = {.x = 200, .y = 200};
        surface.hole_count = 2;
        surface.aureole = 1;
        std::get<1>(surface.holes).centre = {.x = 20, .y = 0, .z = 0};
        BubbleSheet sheet;
        sheet.hole = 0;
        sheet.half_angle = .35;
        sheet.radii = {100, 100, 0, 0};
        // Both annuli cover this point, but the second hole owns it.
        CHECK(!bubble_sheet_contains(surface, sheet, {125, 0, 0}));
        sheet.phi = std::numbers::pi;
        CHECK(bubble_sheet_contains(surface, sheet, {-125, 0, 0}));
        // Later front motion cannot change which original material detached.
        std::get<0>(surface.holes).radius = 500;
        std::get<1>(surface.holes).radius = 0;
        CHECK(bubble_sheet_contains(surface, sheet, {-125, 0, 0}));
        sheet.phi = 0;
        CHECK(!bubble_sheet_contains(surface, sheet, {125, 0, 0}));
    }
}  // namespace

int main() {
    rupture_covers_corners();
    localized_rupture_and_coverage();
    precursor_and_remnants();
    seeded_flat_coverage();
    impact_footprints_are_representable();
    impacts_use_animation_time();
    impacts_follow_droplets();
    particle_continuity_and_evaporation();
    fingers_start_gradually();
    curled_geometry_and_attachments();
    spherical_peel_azimuth_round_trip();
    all_pairwise_merges_fit();
    sheets_contract();
    pinch_birth_is_continuous();
    sheets_keep_their_source_material();
    return g_failures == 0 ? 0 : 1;
}
