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
    interrupted_close_is_continuous();
    interrupted_resize_keeps_window_rupture();
    bounds_contain_the_surfaces();
    wide_windows_keep_tight_vertical_bounds();
    deterministic_and_invalid_input();
    return g_failures == 0 ? 0 : 1;
}
