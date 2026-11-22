#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <span>

#include "Check.hpp"
#include "shared/Core.hpp"
#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleCurl.hpp"
#include "window/bubble/BubbleFoam.hpp"
#include "window/bubble/BubbleFormation.hpp"
#include "window/bubble/BubbleRupture.hpp"
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

    void formation_during_opening() {
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE);
            auto previous = bubble.frame(WINDOW_SIZE);
            const double SHORT_HALF = std::min(WINDOW_SIZE.x, WINDOW_SIZE.y) / 2;
            bool contact = false;
            bool deformed = false;
            bool lingered = false;
            bool late_birth = false;
            bool short_first = WINDOW_SIZE.x == WINDOW_SIZE.y;
            bool early_morph = false;
            bool grew_from_companion = false;
            double emerged = -1;
            std::size_t peak_early = 0;
            std::size_t births = 0;
            double smallest = std::numeric_limits<double>::infinity();
            double largest = 0;
            for (int i = 1; i <= 1000; ++i) {
                bubble.advance(.001);
                const auto FRAME = bubble.frame(WINDOW_SIZE);
                CHECK(FRAME.formation_count <= FRAME.formation.size());
                CHECK(FRAME.shell.visibility == 0 && FRAME.window.hole_count == 0);
                // Visibility, size and position of the window bubble change continuously.
                CHECK(std::abs(FRAME.window.visibility - previous.window.visibility) < .05);
                CHECK(std::abs(FRAME.window.content - previous.window.content) < .05);
                if (previous.window.visibility > 0) {
                    CHECK(std::abs(FRAME.window.half_size.x - previous.window.half_size.x) <
                          WINDOW_SIZE.x * .03);
                    CHECK(std::abs(FRAME.window.half_size.y - previous.window.half_size.y) <
                          WINDOW_SIZE.y * .03);
                    CHECK(std::abs(FRAME.window.corner - previous.window.corner) <
                          std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .03);
                }
                if (emerged < 0 && FRAME.window.visibility > 0) {
                    emerged = i / 1000.0;
                    // It is the film it grew from, drawn at the same place and size while its
                    // membrane takes over, and that film was already there a moment before.
                    const auto &origin = std::get<BUBBLE_FOAM_CAPACITY>(FRAME.formation);
                    CHECK(FRAME.formation_count == kBubbleFormationCapacity &&
                          origin.visibility > .9);
                    CHECK(
                        std::abs(origin.radius - FRAME.window.half_size.x) < 1e-6 * WINDOW_SIZE.x);
                    CHECK(origin.centre.x == FRAME.window.motion.carry.x &&
                          origin.centre.y == FRAME.window.motion.carry.y);
                    for (const auto &film :
                        std::span(previous.formation).first(previous.formation_count)) {
                        if (film.visibility <= 0) {
                            continue;
                        }
                        const double GAP = std::hypot(
                            film.centre.x - origin.centre.x, film.centre.y - origin.centre.y);
                        grew_from_companion |=
                            GAP < origin.radius * .2 && film.radius > origin.radius * .8;
                    }
                }
                if (FRAME.window.visibility > 0) {
                    const double X_EDGE =
                        FRAME.window.half_size.x + std::abs(FRAME.window.motion.carry.x);
                    const double Y_EDGE =
                        FRAME.window.half_size.y + std::abs(FRAME.window.motion.carry.y);
                    const bool X_CONTACT = X_EDGE >= (WINDOW_SIZE.x / 2) - (SHORT_HALF * .02);
                    const bool Y_CONTACT = Y_EDGE >= (WINDOW_SIZE.y / 2) - (SHORT_HALF * .02);
                    if (!contact && (X_CONTACT || Y_CONTACT)) {
                        short_first = WINDOW_SIZE.x == WINDOW_SIZE.y ||
                                      (WINDOW_SIZE.x < WINDOW_SIZE.y ? X_CONTACT : Y_CONTACT);
                    }
                    contact |= X_CONTACT || Y_CONTACT;
                    if (std::abs(FRAME.window.half_size.x - FRAME.window.half_size.y) >
                        SHORT_HALF * .1) {
                        CHECK(contact);
                        deformed = true;
                    }
                    if (contact && !(X_CONTACT && Y_CONTACT)) {
                        const double SMALL =
                            std::min(FRAME.window.half_size.x, FRAME.window.half_size.y);
                        constexpr double PI_VALUE = std::numbers::pi;
                        const double AREA =
                            (4 * FRAME.window.half_size.x * FRAME.window.half_size.y) -
                            ((4 - PI_VALUE) * FRAME.window.corner * FRAME.window.corner);
                        // A modest amount of inflation after first contact must
                        // already flatten the ends, before the other walls arrive.
                        if (AREA > 1.08 * PI_VALUE * SMALL * SMALL) {
                            CHECK(FRAME.window.corner < .97 * SMALL);
                            early_morph = true;
                        }
                    }
                }
                std::size_t live = 0;
                std::size_t index = 0;
                const auto *previous_film = previous.formation.begin();
                for (const auto &film : std::span(FRAME.formation).first(FRAME.formation_count)) {
                    if (film.visibility > 0) {
                        ++live;
                        CHECK(film.radius > 0 && std::isfinite(film.centre.x) &&
                              std::isfinite(film.centre.y));
                        CHECK(film.thickness > 250 && film.thickness < 900);
                        if (index < previous.formation_count && previous_film->visibility > 0 &&
                            previous_film->seed == film.seed) {
                            CHECK(std::abs(film.radius - previous_film->radius) <
                                  std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .02);
                            CHECK(std::hypot(film.centre.x - previous_film->centre.x,
                                      film.centre.y - previous_film->centre.y) <
                                  std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .05);
                        } else {
                            ++births;
                            late_birth |= i > 300;
                        }
                        if (film.growth >= 1 && film.collapse == 0) {
                            smallest = std::min(smallest, film.radius);
                            largest = std::max(largest, film.radius);
                        }
                    }
                    ++index;
                    ++previous_film;
                }
                if (i < 500) {
                    peak_early = std::max(peak_early, live);
                }
                lingered |= bubble.landed() && live > 0;
                previous = FRAME;
            }
            // Small bubbles hold the stage for the first half; the window bubble forms late.
            CHECK(emerged >= .5 && emerged <= .72);
            CHECK(grew_from_companion);
            CHECK(peak_early >= 15 && births > peak_early && late_birth);
            CHECK(largest > smallest * 3);
            CHECK(contact && short_first && lingered);
            if (WINDOW_SIZE.x != WINDOW_SIZE.y) {
                CHECK(deformed && early_morph);
            }
            CHECK(!bubble.active() && !bubble.closed());
            CHECK(previous.window.half_size.x == WINDOW_SIZE.x / 2 &&
                  previous.window.half_size.y == WINDOW_SIZE.y / 2);
            CHECK(previous.window.corner == 0 && previous.window.depth == 0);
            CHECK(previous.window.content == 1 && previous.window.visibility == 1);
            CHECK(previous.window.core.x == 0 && previous.window.core.y == 0);
            for (const auto &film : std::span(previous.formation).first(previous.formation_count)) {
                CHECK(film.visibility == 0);
            }
        }
    }

    void foam_rejects_invalid_slots() {
        BubbleFoam foam;
        foam.reset(3, {}, 2);
        const int MISSING = foam.nearest({}, 1);
        CHECK(MISSING == -1);
        const int FIRST = foam.spawn({}, {}, .05, 600, 1, 10);
        CHECK(FIRST == 0);
        const int SECOND = foam.spawn({.x = 1, .y = 0}, {}, .03, 600, 1, 10);
        CHECK(SECOND == 1);
        const int FULL = foam.spawn({}, {}, .05, 600, 1, 10);
        CHECK(FULL == -1);

        const BubbleFoam &view = foam;
        for (const int slot : {MISSING, FULL, -3, 2, static_cast<int>(BUBBLE_FOAM_CAPACITY)}) {
            bool mutable_threw = false;
            bool const_threw = false;
            try {
                (void)foam.film(slot);
            } catch (const std::out_of_range &) {
                mutable_threw = true;
            }
            try {
                (void)view.film(slot);
            } catch (const std::out_of_range &) {
                const_threw = true;
            }
            CHECK(mutable_threw && const_threw);
        }
        foam.film(FIRST).fed = true;
        CHECK(view.film(FIRST).live && view.film(FIRST).fed);
        CHECK(view.film(SECOND).live);

        // The mutable film API also permits an invalid recipient; it must not index outside the
        // budget.
        for (const int recipient : {-3, 2, static_cast<int>(BUBBLE_FOAM_CAPACITY)}) {
            foam.film(FIRST).into = recipient;
            foam.step(.001, {}, nullptr);
            CHECK(view.film(FIRST).live && view.film(FIRST).into == -1);
            CHECK(view.film(SECOND).live);
        }
        foam.take(SECOND);
        CHECK(!view.film(SECOND).live);
        foam.reset(4, {}, 1);
        bool stale_threw = false;
        try {
            (void)view.film(SECOND);
        } catch (const std::out_of_range &) {
            stale_threw = true;
        }
        CHECK(stale_threw);
    }

    // Two pressed films coalesce through a neck: the smaller empties into the larger and their
    // air, a volume, is conserved; nothing is merged that never touched.
    void foam_coalescence_conserves_volume() {
        BubbleFoam foam;
        foam.reset(3, {}, 4);
        const int FIRST = foam.spawn({.x = 0, .y = 0}, {}, .05, 600, 1, 10);
        const int SECOND = foam.spawn({.x = .075, .y = 0}, {}, .03, 600, 1, 10);
        const int FAR = foam.spawn({.x = .6, .y = 0}, {}, .03, 600, 1, 10);
        const double VOLUME = std::pow(.05, 3) + std::pow(.03, 3);
        const BubbleFoamFlow STILL;
        for (int i = 0; i < 360 && foam.film(SECOND).live; ++i) {
            foam.step(1.0 / 360, STILL, nullptr);
        }
        CHECK(foam.film(FIRST).live && !foam.film(SECOND).live);
        CHECK(std::abs(std::pow(foam.film(FIRST).radius, 3) - VOLUME) < VOLUME * .02);
        CHECK(foam.film(FAR).live && std::abs(foam.film(FAR).radius - .03) < 1e-9);
    }

    // Drained films rupture on their own schedule rather than all at once.
    void foam_films_rupture_independently() {
        BubbleFoam foam;
        foam.reset(9, {}, 8);
        for (int i = 0; i < 8; ++i) {
            foam.spawn({.x = i * .3, .y = 0}, {}, .02, 600, 1, 10);
        }
        foam.expire(.5);
        const BubbleFoamFlow STILL;
        std::size_t previous = foam.live_count();
        std::size_t distinct = 0;
        for (int i = 0; i < 300; ++i) {
            foam.step(1.0 / 360, STILL, nullptr);
            if (foam.live_count() != previous) {
                ++distinct;
            }
            CHECK(previous - foam.live_count() <= 2);
            previous = foam.live_count();
        }
        CHECK(previous == 0 && distinct >= 4);
    }

    // Where the window bubble forms comes from the foam's own history, not a fixed point, and
    // its companions keep pushing it while it grows.
    void formation_movement_depends_on_seed() {
        std::array<Vec2, 2> origins{};
        auto *origin = origins.begin();
        for (int seed = 0; seed < 2; ++seed, ++origin) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 40 + seed, SIZE);
            bool emerged = false;
            bool pushed = false;
            Vec2 previous{};
            for (int i = 0; i < 900; ++i) {
                bubble.advance(.001);
                const auto FRAME = bubble.frame(SIZE);
                const auto &surface = FRAME.window;
                if (surface.visibility <= 0) {
                    continue;
                }
                if (!emerged) {
                    emerged = true;
                    *origin = surface.motion.carry;
                } else if (!bubble.landed()) {
                    bool touching = false;
                    for (const auto &other : std::span(FRAME.formation)
                             .first(std::min(BUBBLE_FOAM_CAPACITY, FRAME.formation_count))) {
                        if (other.visibility <= 0) {
                            continue;
                        }
                        const double DISTANCE = std::hypot(surface.motion.carry.x - other.centre.x,
                            surface.motion.carry.y - other.centre.y);
                        touching |= DISTANCE <= std::max(surface.half_size.x, surface.half_size.y) +
                                                    other.radius;
                    }
                    const double STEP = std::hypot(
                        surface.motion.carry.x - previous.x, surface.motion.carry.y - previous.y);
                    pushed |= touching && STEP > 1e-3;
                }
                previous = surface.motion.carry;
            }
            CHECK(emerged && pushed);
        }
        CHECK(
            std::hypot(std::get<0>(origins).x - std::get<1>(origins).x,
                std::get<0>(origins).y - std::get<1>(origins).y) > .02 * std::min(SIZE.x, SIZE.y));
    }

    void pressure_changes_inflation_and_settles() {
        constexpr double PI_VALUE = std::numbers::pi;
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE);
            const auto AREA = [](const BubbleSurface &surface) {
                return (4 * surface.half_size.x * surface.half_size.y) -
                       ((4 - PI_VALUE) * surface.corner * surface.corner);
            };
            double previous_area = AREA(bubble.frame(WINDOW_SIZE).window);
            double min_rate = std::numeric_limits<double>::infinity();
            double max_rate = 0;
            double peak_core = 0;
            double late_core = 0;
            bool positive = false;
            bool negative = false;
            double free_rate = 0;
            double constrained_rate = 0;
            int contact_step = 0;
            for (int i = 1; i <= 100; ++i) {
                bubble.advance(.01);
                const auto FRAME = bubble.frame(WINDOW_SIZE);
                // Equal integration intervals avoid mistaking substep quantisation (or
                // sqrt(area)'s changing radius rate) for variable air flow.
                if (i % 5 == 0) {
                    const double CURRENT_AREA = AREA(FRAME.window);
                    const double RATE = CURRENT_AREA - previous_area;
                    if (i >= 15 && !bubble.landed() &&
                        RATE > WINDOW_SIZE.x * WINDOW_SIZE.y * .001) {
                        min_rate = std::min(min_rate, RATE);
                        max_rate = std::max(max_rate, RATE);
                    }
                    if (std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) >
                        2 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y)) {
                        const double RADIUS =
                            std::min((WINDOW_SIZE.x / 2) - std::abs(FRAME.window.motion.carry.x),
                                (WINDOW_SIZE.y / 2) - std::abs(FRAME.window.motion.carry.y));
                        const double CONTACT_RATIO = CURRENT_AREA / (PI_VALUE * RADIUS * RADIUS);
                        if (contact_step == 0) {
                            if (CONTACT_RATIO < 1) {
                                free_rate = RATE;
                            } else {
                                contact_step = i;
                            }
                        }
                        // Compare complete intervals on either side of contact.
                        if (contact_step > 0 && i == contact_step + 10) {
                            constrained_rate = RATE;
                        }
                    }
                    previous_area = CURRENT_AREA;
                }
                if (bubble.landed()) {
                    peak_core =
                        std::max(peak_core, std::hypot(FRAME.window.core.x, FRAME.window.core.y));
                    positive |=
                        FRAME.window.core.x > .0001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y);
                    negative |=
                        FRAME.window.core.x < -.0001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y);
                }
                if (i == 99) {
                    late_core = std::hypot(FRAME.window.core.x, FRAME.window.core.y);
                }
            }
            CHECK(max_rate > min_rate * 1.15);
            if (std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) >
                2 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y)) {
                CHECK(contact_step > 0 && free_rate > 0 && constrained_rate > 0);
                CHECK(constrained_rate > 1.3 * free_rate);
            }
            CHECK(peak_core > .002 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y));
            CHECK(positive && negative);
            CHECK(late_core < peak_core * .1);
            CHECK(bubble.frame(WINDOW_SIZE).window.core.x == 0 &&
                  bubble.frame(WINDOW_SIZE).window.core.y == 0);
        }
    }

    void formation_controls_keep_the_endpoint() {
        std::array<BubbleFormationParams, 20> cases{};
        std::get<0>(cases).count = 0;
        std::get<1>(cases).count = 1;
        std::get<2>(cases).birthRate = .25;
        std::get<3>(cases).birthRate = 2;
        std::get<4>(cases).sizeVariation = 0;
        std::get<5>(cases).sizeVariation = 1;
        std::get<6>(cases).growthVariation = 0;
        std::get<7>(cases).growthVariation = 1;
        std::get<8>(cases).interaction = .25;
        std::get<9>(cases).interaction = 2;
        std::get<10>(cases).damping = .5;
        std::get<11>(cases).damping = 2;
        std::get<12>(cases).buoyancy = 0;
        std::get<13>(cases).buoyancy = 2;
        std::get<14>(cases).linger = 0;
        std::get<15>(cases).linger = 1;
        std::get<16>(cases) = {.count = 4,
            .birthRate = .25,
            .sizeVariation = 0,
            .growthVariation = 0,
            .interaction = .25,
            .damping = .5,
            .buoyancy = 0,
            .linger = 0};
        std::get<17>(cases) = {.count = 32,
            .birthRate = 2,
            .sizeVariation = 1,
            .growthVariation = 1,
            .interaction = 2,
            .damping = 2,
            .buoyancy = 2,
            .linger = 1};
        std::get<18>(cases) = {.count = 32,
            .birthRate = 2,
            .sizeVariation = 1,
            .growthVariation = 1,
            .interaction = 2,
            .damping = .5,
            .buoyancy = 2,
            .linger = 1};
        std::get<19>(cases) = {.count = 32,
            .birthRate = .25,
            .sizeVariation = 0,
            .growthVariation = 0,
            .interaction = .25,
            .damping = 2,
            .buoyancy = 0,
            .linger = 1};
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}}) {
            for (const auto &params : cases) {
                Bubble bubble;
                bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE, params);
                for (int i = 0; i < 99; ++i) {
                    bubble.advance(.01);
                    const auto FRAME = bubble.frame(WINDOW_SIZE);
                    std::size_t visible = 0;
                    for (const auto &film :
                        std::span(FRAME.formation).first(FRAME.formation_count)) {
                        if (film.visibility > 0) {
                            ++visible;
                        }
                        CHECK(std::isfinite(film.centre.x) && std::isfinite(film.centre.y));
                        CHECK(std::isfinite(film.radius));
                    }
                    // The film the window bubble grew from is drawn once more while it takes over.
                    CHECK(visible <= static_cast<std::size_t>(params.count) + 1);
                    CHECK(std::isfinite(FRAME.window.core.x) && std::isfinite(FRAME.window.core.y));
                }
                const auto BEFORE_END = bubble.frame(WINDOW_SIZE);
                // The exact native endpoint must not hide a failed inflation solver.
                CHECK(bubble.landed());
                CHECK(std::abs(BEFORE_END.window.half_size.x - (WINDOW_SIZE.x / 2)) <
                      .001 * WINDOW_SIZE.x);
                CHECK(std::abs(BEFORE_END.window.half_size.y - (WINDOW_SIZE.y / 2)) <
                      .001 * WINDOW_SIZE.y);
                CHECK(BEFORE_END.window.corner < .001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y));
                bubble.advance(.01);
                CHECK(!bubble.active() && bubble.frame(WINDOW_SIZE).formation_count == 0);
            }
        }
    }

    void check_surface_continuity(const BubbleSurface &before, const BubbleSurface &after) {
        CHECK(before.half_size.x == after.half_size.x && before.half_size.y == after.half_size.y);
        CHECK(before.corner == after.corner && before.depth == after.depth);
        CHECK(before.core.x == after.core.x && before.core.y == after.core.y);
        CHECK(before.content == after.content && before.film == after.film);
        CHECK(before.visibility == after.visibility && before.ripple == after.ripple);
        if (before.closing) {
            CHECK(before.rupture_visibility == after.rupture_visibility);
            CHECK(before.clock == after.clock);
            CHECK(before.hole_count == after.hole_count);
            auto after_hole = std::span(after.holes).begin();
            for (const auto &before_hole : std::span(before.holes).first(before.hole_count)) {
                CHECK(before_hole.radius == after_hole->radius);
                ++after_hole;
            }
        }
    }

    void interrupted_close_is_continuous() {
        for (const double TIME : {0.0, 0.2, 0.38, 0.44, 0.55, 0.62, 0.85, 0.9}) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 9, SIZE);
            advance_for(bubble, TIME);
            const auto BEFORE = bubble.frame(SIZE);
            bubble.begin(Transition::Close, BubbleDuration{.42}, 11, SIZE);
            const auto AFTER = bubble.frame(SIZE);
            CHECK(bubble.active() && !bubble.opening());
            CHECK(BEFORE.seed == AFTER.seed);
            check_surface_continuity(BEFORE.window, AFTER.window);
            CHECK(AFTER.window.rupture_visibility == BEFORE.window.visibility);
            CHECK(AFTER.formation_count == BEFORE.formation_count);
            const auto *after_film = AFTER.formation.begin();
            for (const auto &film : std::span(BEFORE.formation)
                     .first(std::min(BEFORE.formation_count, AFTER.formation_count))) {
                CHECK(
                    film.centre.x == after_film->centre.x && film.centre.y == after_film->centre.y);
                CHECK(
                    film.radius == after_film->radius && film.visibility == after_film->visibility);
                CHECK(film.collapse == after_film->collapse &&
                      film.thickness == after_film->thickness);
                ++after_film;
            }
            advance_for(bubble, .05);
            const auto PROGRESSING = bubble.frame(SIZE);
            CHECK(std::get<0>(PROGRESSING.window.holes).radius >=
                  std::get<0>(AFTER.window.holes).radius);
            advance_for(bubble, .5);
            CHECK(bubble.closed() && !bubble.active());
            const auto CLOSED = bubble.frame(SIZE);
            for (const auto &film : std::span(CLOSED.formation).first(CLOSED.formation_count)) {
                CHECK(film.visibility == 0);
            }
        }
    }

    void interrupted_resize_keeps_window_rupture() {
        Bubble bubble;
        bubble.begin(Transition::Open, BubbleDuration{1}, 9, SIZE);
        advance_for(bubble, 0.44);
        bubble.begin(Transition::Close, BubbleDuration{1}, 11, SIZE);
        const auto ORIGINAL = bubble.frame(SIZE);
        const auto RESIZED = bubble.frame({.x = SIZE.x * 1.5, .y = SIZE.y * 0.5});
        CHECK(RESIZED.window.half_size.x == ORIGINAL.window.half_size.x * 1.5);
        CHECK(RESIZED.window.half_size.y == ORIGINAL.window.half_size.y * 0.5);
        CHECK(std::get<0>(RESIZED.window.holes).centre.x ==
              std::get<0>(ORIGINAL.window.holes).centre.x * 1.5);
        CHECK(std::get<0>(RESIZED.window.holes).centre.y ==
              std::get<0>(ORIGINAL.window.holes).centre.y * .5);
        CHECK(RESIZED.window.speed == ORIGINAL.window.speed * 1.5);
        CHECK(std::get<0>(bubble.frame(SIZE).window.holes).radius ==
              std::get<0>(ORIGINAL.window.holes).radius);
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

    void check_surface_bounds(const Box &box, Vec2 size, const BubbleSurface &surface) {
        if (surface.visibility <= 0) {
            return;
        }
        const Vec2 CENTRE{
            .x = (size.x / 2) + surface.motion.carry.x, .y = (size.y / 2) + surface.motion.carry.y};
        CHECK(box.x <= CENTRE.x - surface.half_size.x - std::abs(surface.ripple));
        CHECK(box.y <= CENTRE.y - surface.half_size.y - std::abs(surface.ripple));
        CHECK(box.x + box.w >= CENTRE.x + surface.half_size.x + std::abs(surface.ripple));
        CHECK(box.y + box.h >= CENTRE.y + surface.half_size.y + std::abs(surface.ripple));
    }

    void bounds_contain_the_surfaces() {
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}, SIZE}) {
            const auto VERIFY = [&](Transition direction) {
                Bubble bubble;
                bubble.begin(direction, BubbleDuration{1}, 8, WINDOW_SIZE);
                CHECK(bubble.bounds(WINDOW_SIZE).w > 0 && bubble.bounds(WINDOW_SIZE).h > 0);
                for (int i = 0; i < 100; ++i) {
                    const auto FRAME = bubble.frame(WINDOW_SIZE);
                    const auto BOX = bubble.bounds(WINDOW_SIZE);
                    check_surface_bounds(BOX, WINDOW_SIZE, FRAME.window);
                    check_surface_bounds(BOX, WINDOW_SIZE, FRAME.shell);
                    for (const auto &film :
                        std::span(FRAME.formation).first(FRAME.formation_count)) {
                        if (film.visibility <= 0) {
                            continue;
                        }
                        const double REACH =
                            film.radius * (1 + std::abs(film.squash.x) + std::abs(film.squash.y));
                        CHECK(BOX.x <= (WINDOW_SIZE.x / 2) + film.centre.x - REACH);
                        CHECK(BOX.y <= (WINDOW_SIZE.y / 2) + film.centre.y - REACH);
                        CHECK(BOX.x + BOX.w >= (WINDOW_SIZE.x / 2) + film.centre.x + REACH);
                        CHECK(BOX.y + BOX.h >= (WINDOW_SIZE.y / 2) + film.centre.y + REACH);
                    }
                    std::array<BubbleParticle, K_BUBBLE_MAX_PARTICLES> particles{};
                    for (const auto *surface : {&FRAME.window, &FRAME.shell}) {
                        const auto COUNT = bubble_particles(*surface, particles);
                        for (const auto &particle : std::span(particles).first(COUNT)) {
                            CHECK(
                                BOX.x <= (WINDOW_SIZE.x / 2) + particle.centre.x - particle.radius);
                            CHECK(BOX.x + BOX.w >=
                                  (WINDOW_SIZE.x / 2) + particle.centre.x + particle.radius);
                            CHECK(
                                BOX.y <= (WINDOW_SIZE.y / 2) + particle.centre.y - particle.radius);
                            CHECK(BOX.y + BOX.h >=
                                  (WINDOW_SIZE.y / 2) + particle.centre.y + particle.radius);
                            CHECK(BOX.x <=
                                  (WINDOW_SIZE.x / 2) + particle.tail.x - particle.tail_radius);
                            CHECK(BOX.x + BOX.w >=
                                  (WINDOW_SIZE.x / 2) + particle.tail.x + particle.tail_radius);
                            CHECK(BOX.y <=
                                  (WINDOW_SIZE.y / 2) + particle.tail.y - particle.tail_radius);
                            CHECK(BOX.y + BOX.h >=
                                  (WINDOW_SIZE.y / 2) + particle.tail.y + particle.tail_radius);
                        }
                    }
                    bubble.advance(0.01);
                }
            };
            VERIFY(Transition::Open);
            VERIFY(Transition::Close);
        }
    }

    void wide_windows_keep_tight_vertical_bounds() {
        Bubble normal;
        Bubble wide;
        normal.begin(Transition::Close, BubbleDuration{1}, 8, {.x = 600, .y = 200});
        wide.begin(Transition::Close, BubbleDuration{1}, 8, {.x = 1800, .y = 200});
        advance_for(normal, 0.6);
        advance_for(wide, 0.6);
        CHECK(normal.bounds({600, 200}).h == wide.bounds({1800, 200}).h);
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

    void deterministic_and_invalid_input() {
        Bubble first_bubble;
        Bubble bubble;
        first_bubble.begin(Transition::Open, BubbleDuration{1}, 55, SIZE);
        bubble.begin(Transition::Open, BubbleDuration{1}, 55, SIZE);
        advance_for(first_bubble, 0.84, 0.01);
        advance_for(bubble, 0.84, 0.02);
        const auto FIRST_FRAME = first_bubble.frame(SIZE);
        const auto SECOND_FRAME = bubble.frame(SIZE);
        CHECK(std::abs(FIRST_FRAME.window.half_size.x - SECOND_FRAME.window.half_size.x) < 1e-9);
        CHECK(std::abs(FIRST_FRAME.window.depth - SECOND_FRAME.window.depth) < 1e-9 &&
              FIRST_FRAME.seed == SECOND_FRAME.seed);
        CHECK(std::abs(FIRST_FRAME.window.core.x - SECOND_FRAME.window.core.x) < 1e-9 &&
              std::abs(FIRST_FRAME.window.core.y - SECOND_FRAME.window.core.y) < 1e-9);
        CHECK(FIRST_FRAME.formation_count == SECOND_FRAME.formation_count);
        const auto *second_film = SECOND_FRAME.formation.begin();
        for (const auto &film : std::span(FIRST_FRAME.formation)
                 .first(std::min(FIRST_FRAME.formation_count, SECOND_FRAME.formation_count))) {
            CHECK(std::abs(film.radius - second_film->radius) < 1e-9);
            CHECK(std::abs(film.centre.x - second_film->centre.x) < 1e-9);
            CHECK(std::abs(film.centre.y - second_film->centre.y) < 1e-9);
            CHECK(std::abs(film.visibility - second_film->visibility) < 1e-9);
            ++second_film;
        }
        first_bubble.advance(-1);
        first_bubble.advance(std::numeric_limits<double>::quiet_NaN());
        CHECK(first_bubble.frame(SIZE).window.half_size.x == FIRST_FRAME.window.half_size.x);
        first_bubble.advance(30);
        CHECK(std::isfinite(first_bubble.frame(SIZE).window.half_size.x));
        first_bubble.reset();
        CHECK(!first_bubble.active() && !first_bubble.closed());
        first_bubble.begin(Transition::Close, BubbleDuration{0}, 1, SIZE);
        CHECK(!first_bubble.active() && !first_bubble.closed());
        first_bubble.begin(Transition::Open, BubbleDuration{1}, 1, {.x = 0, .y = 100});
        CHECK(!first_bubble.active());
        const Box INVALID =
            first_bubble.bounds({.x = std::numeric_limits<double>::infinity(), .y = 2});
        CHECK(INVALID.w == 0 && INVALID.h == 0);
    }
}  // namespace

int main() {
    formation_during_opening();
    formation_movement_depends_on_seed();
    foam_coalescence_conserves_volume();
    foam_rejects_invalid_slots();
    foam_films_rupture_independently();
    pressure_changes_inflation_and_settles();
    formation_controls_keep_the_endpoint();
    interrupted_close_is_continuous();
    interrupted_resize_keeps_window_rupture();
    rupture_covers_corners();
    bounds_contain_the_surfaces();
    wide_windows_keep_tight_vertical_bounds();
    deterministic_and_invalid_input();
    localized_rupture_and_coverage();
    impact_footprints_are_representable();
    impacts_use_animation_time();
    impacts_follow_droplets();
    precursor_and_remnants();
    seeded_flat_coverage();
    particle_continuity_and_evaporation();
    fingers_start_gradually();
    curled_geometry_and_attachments();
    sheets_contract();
    sheets_keep_their_source_material();
    pinch_birth_is_continuous();
    return g_failures == 0 ? 0 : 1;
}
