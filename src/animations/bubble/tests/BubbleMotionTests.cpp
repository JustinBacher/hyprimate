#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <span>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleWake.hpp"
#include "animations/shared/Core.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{.x = 900, .y = 600};
    constexpr Box BODY{.x = -440, .y = -290, .w = 880, .h = 580};
    constexpr BubbleMotionParams PARAMS{.strength = .65, .microbubbles = .5, .trail = .5};
    constexpr double TIME_STEP = 1.0 / 60.0;

    // Moves the window at velocity(t) px/s for the given time, one frame at a time.
    void travel(Bubble &bubble,
        double &time,
        double seconds,
        const std::function<Vec2(double)> &velocity,
        BubbleMotionParams params = PARAMS,
        const std::function<void(const Bubble &)> &each = {}) {
        for (const double END = time + seconds; time < END - 1e-9; time += TIME_STEP) {
            const Vec2 VELOCITY = velocity(time);
            bubble.drive({.x = VELOCITY.x * TIME_STEP, .y = VELOCITY.y * TIME_STEP});
            bubble.advance(TIME_STEP, SIZE, BODY, params);
            if (each) {
                each(bubble);
            }
            bubble.clear_emissions();
        }
    }
    Vec2 still([[maybe_unused]] double elapsed) {
        return {};
    }
    double drag(const Bubble &bubble) {
        return bubble.frame(SIZE).window.motion.drag.x;
    }

    void resting_window_is_untouched() {
        Bubble bubble;
        double time = 0;
        travel(bubble, time, 1, still);
        CHECK(!bubble.stirred());
        const auto FRAME = bubble.frame(SIZE);
        CHECK(FRAME.window.motion.still() && FRAME.micro_count == 0);
        CHECK(FRAME.window.core.x == 0 && FRAME.window.core.y == 0 && FRAME.window.film == 0);
        CHECK(bubble.bounds(SIZE).w == SIZE.x + 16);
    }

    void acceleration_lags_steady_travel_holds_and_stopping_overshoots() {
        Bubble bubble;
        bubble.seed_motion(3);
        double time = 0;
        double peak = 0;
        // Setting off to the right: the trailing (left) film lags behind the leading edge.
        travel(
            bubble,
            time,
            .25,
            [](double) { return Vec2{.x = 1200, .y = 0}; },
            PARAMS,
            [&](const Bubble &current) { peak = std::min(peak, drag(current)); });
        CHECK(peak < -8);
        CHECK(drag(bubble) < 0);
        // Sustained travel settles into a steady stretch, smaller than the acceleration's.
        travel(bubble, time, .6, [](double) { return Vec2{.x = 1200, .y = 0}; });
        const double STEADY = drag(bubble);
        double spread = 0;
        travel(
            bubble,
            time,
            .3,
            [](double) { return Vec2{.x = 1200, .y = 0}; },
            PARAMS,
            [&](const Bubble &current) {
                spread = std::max(spread, std::abs(drag(current) - STEADY));
            });
        CHECK(STEADY < -2 && STEADY > peak);
        CHECK(spread < 1.5);
        CHECK(bubble.stirred() && bubble.frame(SIZE).window.motion.sheen > .05);
        // Stopping: the fluid carries on past the window once, then settles without snapping.
        double overshoot = 0;
        double jump = 0;
        double previous = drag(bubble);
        travel(bubble, time, 3, still, PARAMS, [&](const Bubble &current) {
            const double DRAG = drag(current);
            overshoot = std::max(overshoot, DRAG);
            jump = std::max(jump, std::abs(DRAG - previous));
            previous = DRAG;
        });
        CHECK(overshoot > 2);
        CHECK(overshoot < 2 * std::abs(peak));
        CHECK(jump < 6);
        // At rest the window is exactly its undeformed self again.
        CHECK(!bubble.stirred());
        const auto FRAME = bubble.frame(SIZE);
        CHECK(FRAME.window.motion.still() && FRAME.window.core.x == 0 && FRAME.micro_count == 0);
    }

    void reversal_keeps_momentum() {
        Bubble bubble;
        bubble.seed_motion(5);
        double time = 0;
        travel(bubble, time, .8, [](double) { return Vec2{.x = 1000, .y = 0}; });
        const double BEFORE = drag(bubble);
        CHECK(BEFORE < 0);
        // The window turns back; the fluid keeps going right for a moment.
        double first = 0;
        bool sampled = false;
        bool flipped = false;
        double jump = 0;
        double previous = BEFORE;
        travel(
            bubble,
            time,
            .6,
            [](double) { return Vec2{.x = -1000, .y = 0}; },
            PARAMS,
            [&](const Bubble &current) {
                const double DRAG = drag(current);
                if (!sampled) {
                    first = DRAG, sampled = true;
                }
                flipped |= DRAG > 4;
                jump = std::max(jump, std::abs(DRAG - previous));
                previous = DRAG;
            });
        CHECK(std::abs(first - BEFORE) < 6);  // no reset at the turn
        CHECK(flipped);  // trailing film is now on the right
        CHECK(jump < 6);
        double waves = 0;
        const auto &deformation = bubble.frame(SIZE).window.motion;
        const auto *sine = deformation.wave_sin.begin();
        for (const double COSINE : deformation.wave_cos) {
            waves += std::hypot(COSINE, *sine++);
        }
        CHECK(waves > 0);
    }

    void zero_strength_moves_undeformed() {
        Bubble bubble;
        double time = 0;
        travel(
            bubble,
            time,
            .5,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1},
            [&](const Bubble &current) {
                const auto FRAME = current.frame(SIZE);
                CHECK(FRAME.window.motion.drag.x == 0 && FRAME.window.motion.sheen == 0 &&
                      FRAME.micro_count == 0);
                CHECK(current.motion().trail().empty() && current.motion().releases().empty());
            });
    }

    void offset_keeps_the_leading_edge() {
        BubbleDeformation deformation;
        deformation.drag = {.x = -20, .y = 0};
        const Vec2 HALF{.x = SIZE.x / 2, .y = SIZE.y / 2};
        const auto LEADING = bubble_motion_offset(HALF, deformation, {.x = HALF.x, .y = 0});
        const auto TRAILING = bubble_motion_offset(HALF, deformation, {.x = -HALF.x, .y = 0});
        CHECK(std::abs(LEADING.x) < 1e-9 && std::abs(LEADING.y) < 1e-9);
        CHECK(std::abs(TRAILING.x + 20) < 1e-9);
        // The trailing film narrows across the motion.
        const auto CORNER = bubble_motion_offset(HALF, deformation, {.x = -HALF.x, .y = HALF.y});
        CHECK(CORNER.y < 0);
        const auto NONE = bubble_motion_offset(HALF, {}, {.x = 123, .y = -45});
        CHECK(NONE.x == 0 && NONE.y == 0);
        // Capillary modes move the outline, not the centre.
        BubbleDeformation wave;
        wave.wave_cos.front() = 3;
        const auto CENTRE = bubble_motion_offset(HALF, wave, {.x = 0, .y = 0});
        const auto EDGE = bubble_motion_offset(HALF, wave, {.x = HALF.x, .y = 0});
        CHECK(CENTRE.x == 0 && CENTRE.y == 0);
        CHECK(std::abs(EDGE.x - 3) < 1e-9);
    }

    void microbubbles_form_migrate_and_detach() {
        Bubble moving_bubble;
        moving_bubble.seed_motion(11);
        double time = 0;
        std::size_t most = 0;
        std::size_t releases = 0;
        std::size_t trail = 0;
        bool grew = false;
        bool rode_out = false;
        bool trailed_behind = true;
        travel(
            moving_bubble,
            time,
            4,
            [](double elapsed) {
                return Vec2{
                    .x = 1500 * std::cos(elapsed * 1.6),
                    .y = 500 * std::sin(elapsed * 2.3),
                };
            },
            {.strength = .65, .microbubbles = 1, .trail = 1},
            [&](const Bubble &current) {
                const auto FRAME = current.frame(SIZE);
                most = std::max(most, FRAME.micro_count);
                for (const auto &microbubble : std::span(FRAME.micro).first(FRAME.micro_count)) {
                    grew |= microbubble.growth > 0 && microbubble.growth < 1;
                    // A formed bubble sits outside the film it grew from, joined to it by its neck.
                    if (microbubble.growth >= 1 && microbubble.collapse == 0) {
                        rode_out |= std::abs(microbubble.centre.x) > BODY.w / 2 ||
                                    std::abs(microbubble.centre.y) > BODY.h / 2;
                    }
                    CHECK(microbubble.radius > 0 && microbubble.radius < 20 &&
                          microbubble.attach > 0);
                }
                for (const auto &release : current.motion().releases()) {
                    ++releases;
                    CHECK(release.micro.attach == 0 && release.life > 0);
                    // Released bubbles inherit the window's motion but fall behind it.
                    const double SPEED = current.motion().speed();
                    CHECK(std::hypot(release.velocity.x, release.velocity.y) < (SPEED * 1.2) + 80);
                }
                for (const auto &point : current.motion().trail()) {
                    ++trail;
                    CHECK(point.density > 0 && point.density <= 1 && point.half_width > 0);
                    // The film comes off the trailing side.
                    trailed_behind &= (point.position.x * point.direction.x) +
                                          (point.position.y * point.direction.y) <
                                      0;
                }
            });
        CHECK(most > 0 && most <= K_BUBBLE_MICRO_CAPACITY);
        CHECK(grew && rode_out);
        CHECK(releases > 0);
        CHECK(trail > 20 && trailed_behind);
        // Without movement nothing new forms once the window has come to a stop, and every bubble
        // ends.
        travel(moving_bubble, time, .3, still, {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(moving_bubble,
            time,
            4,
            still,
            {.strength = .65, .microbubbles = 1, .trail = 1},
            [&](const Bubble &current) {
                CHECK(current.motion().trail().empty() && current.motion().releases().empty());
            });
        CHECK(!moving_bubble.stirred() && moving_bubble.frame(SIZE).micro_count == 0);
    }

    void slow_movement_leaves_no_trail() {
        Bubble bubble;
        double time = 0;
        std::size_t points = 0;
        travel(
            bubble,
            time,
            1,
            [](double) { return Vec2{.x = 90, .y = 0}; },
            PARAMS,
            [&](const Bubble &current) {
                points += current.motion().trail().size() + current.motion().releases().size();
            });
        CHECK(points == 0);
    }

    void close_inherits_the_deformation() {
        Bubble bubble;
        double time = 0;
        travel(bubble, time, .3, [](double) { return Vec2{.x = 1400, .y = 0}; });
        const auto BEFORE = bubble.frame(SIZE);
        CHECK(BEFORE.window.motion.drag.x < 0);
        bubble.begin(Transition::Close, .42, 9, SIZE);
        const auto AFTER = bubble.frame(SIZE);
        CHECK(AFTER.window.motion.drag.x == BEFORE.window.motion.drag.x);
        CHECK(AFTER.window.core.x == BEFORE.window.core.x);
        CHECK(AFTER.micro_count == BEFORE.micro_count);
        bubble.advance(.42 * .55);
        const auto RELAXED = bubble.frame(SIZE);
        CHECK(RELAXED.window.motion.drag.x == 0 && RELAXED.window.motion.sheen == 0);
        const auto BOUNDS = bubble.bounds(SIZE);
        CHECK(BOUNDS.w > 0);
    }

    void opening_hands_over_to_movement() {
        Bubble bubble;
        bubble.seed_motion(2);
        bubble.begin(Transition::Open, .62, 4, SIZE);
        double time = 0;
        while (!bubble.landed()) {
            bubble.advance(TIME_STEP, SIZE, BODY, PARAMS);
            time += TIME_STEP;
        }
        CHECK(bubble.active() && bubble.opening());
        const auto LANDED = bubble.frame(SIZE);
        // Movement adds to the settling core instead of replacing it.
        travel(bubble, time, .1, [](double) { return Vec2{.x = 1200, .y = 0}; });
        const auto MOVING = bubble.frame(SIZE);
        CHECK(MOVING.window.motion.drag.x < 0);
        CHECK(MOVING.window.half_size.x > 0 && LANDED.window.thickness == MOVING.window.thickness);
        travel(bubble, time, 3, still);
        CHECK(!bubble.active() && !bubble.stirred());
        const auto REST = bubble.frame(SIZE);
        CHECK(REST.window.motion.still() && REST.window.core.x == 0 && REST.window.film == 0);
    }

    void wake_drains_and_ends() {
        BubbleWake wake(7);
        CHECK(wake.done() && wake.bounds().w == 0);
        for (int i = 0; i < 20; ++i) {
            CHECK(wake.extend({{-450, 0}, {1, 0}, 80, .8, i == 0}, {100.0 + (i * 20), 300}));
        }
        BubbleRelease bubble_release;
        bubble_release.micro.radius = 5;
        bubble_release.micro.thickness = 500;
        bubble_release.velocity = {.x = 600, .y = 0};
        bubble_release.life = .4;
        CHECK(wake.release(bubble_release, {500, 300}));
        const auto FIRST = wake.bounds();
        CHECK(FIRST.w > 380 && FIRST.h >= 160);
        BubbleWakeView view;
        wake.view(view);
        CHECK(view.node_count == 20 && view.bubble_count == 1);
        CHECK(std::abs(view.size.x - FIRST.w) < 1e-9);
        CHECK(std::get<19>(view.nodes).arc > std::get<0>(view.nodes).arc);
        // A free bubble drifts on a little, then slows.
        double moved = 0;
        for (int i = 0; i < 12; ++i) {
            BubbleWakeView before_view;
            BubbleWakeView after_view;
            wake.view(before_view);
            const double START_X = before_view.bubbles.front().centre.x;
            wake.advance(TIME_STEP);
            wake.view(after_view);
            if (after_view.bubble_count != 0U) {
                moved = after_view.bubbles.front().centre.x - START_X;
            }
        }
        CHECK(moved > 0 && moved < 600 * TIME_STEP);
        // Film thins and goes, oldest first and never all at once, and the wake ends.
        BubbleWakeView mid;
        for (int i = 0; i < 30; ++i) {
            wake.advance(TIME_STEP);
        }
        wake.view(mid);
        CHECK(mid.node_count > 0 && std::get<0>(mid.nodes).fade > 0);
        for (int i = 0; i < 300 && !wake.done(); ++i) {
            wake.advance(TIME_STEP);
        }
        CHECK(wake.done() && wake.bounds().w == 0);
        // A wake holds a bounded ribbon.
        BubbleWake full(1);
        std::size_t accepted = 0;
        for (int i = 0; i < 100; ++i) {
            accepted += static_cast<std::size_t>(full.extend(
                {
                    .position = {.x = 0, .y = 0},
                    .direction = {.x = 1, .y = 0},
                    .half_width = 40,
                    .density = 1,
                    .start = false,
                },
                {.x = static_cast<double>(i), .y = 0}));
        }
        CHECK(accepted == K_BUBBLE_WAKE_NODES && full.full());
    }

    constexpr BubbleMotionParams ROUND{
        .strength = .65,
        .microbubbles = .5,
        .trail = .5,
        .roundness = 1,
        .shape = BubbleShape::Circle,
        .shape_speed = 1200,
    };
    double roundness(const Bubble &bubble) {
        return bubble.frame(SIZE).window.motion.round;
    }
    // Where contraction carries a point of the membrane's outline.
    Vec2 shaped(const BubbleDeformation &motion, Vec2 point) {
        return bubble_round_point({.x = SIZE.x / 2, .y = SIZE.y / 2}, motion, point);
    }
    // Distance from the centre of a rounded point of the membrane's outline.
    double reach(const BubbleDeformation &motion, Vec2 point) {
        const auto POINT = shaped(motion, point);
        return std::hypot(POINT.x, POINT.y);
    }

    void rounds_up_while_moving_and_spreads_back() {
        Bubble bubble;
        bubble.seed_motion(8);
        double time = 0;
        double jump = 0;
        double previous = 0;
        const auto SMOOTH = [&](const Bubble &current) {
            jump = std::max(jump, std::abs(roundness(current) - previous));
            previous = roundness(current);
        };
        // A quick flick already rounds the window visibly.
        travel(bubble, time, .15, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND, SMOOTH);
        CHECK(roundness(bubble) > .25);
        travel(bubble, time, 1, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND, SMOOTH);
        const auto MOTION = bubble.frame(SIZE).window.motion;
        CHECK(MOTION.round > .85 && MOTION.round < 1);
        // Nearly circular: the corners have pulled in to the edges' distance, wide as it is.
        const double SIDE = reach(MOTION, {.x = SIZE.x / 2, .y = 0});
        const double TOP = reach(MOTION, {.x = 0, .y = SIZE.y / 2});
        const double CORNER = reach(MOTION, {.x = SIZE.x / 2, .y = SIZE.y / 2});
        CHECK(std::abs(SIDE - TOP) < .08 * SIDE && std::abs(CORNER - SIDE) < .12 * SIDE);
        CHECK(SIDE < SIZE.x / 2);
        const auto BOUNDS = bubble.bounds(SIZE);
        CHECK(BOUNDS.h >= 2 * TOP && BOUNDS.w >= 2 * SIDE);
        // Stopping spreads it back out, past the rectangle a little, then exactly to it.
        double least = 1;
        travel(bubble, time, 3, still, ROUND, [&](const Bubble &current) {
            SMOOTH(current);
            least = std::min(least, roundness(current));
        });
        CHECK(least < 0 && least > -.12);
        CHECK(jump < .08);
        CHECK(!bubble.stirred() && bubble.frame(SIZE).window.motion.still());
    }

    void pillow_and_ellipse_keep_the_windows_proportions() {
        for (const auto SHAPE : {BubbleShape::Pillow, BubbleShape::Ellipse}) {
            Bubble bubble;
            double time = 0;
            travel(bubble,
                time,
                1.2,
                [](double) { return Vec2{.x = 1500, .y = 0}; },
                {.strength = .65, .microbubbles = .5, .trail = .5, .roundness = 1, .shape = SHAPE});
            const auto MOTION = bubble.frame(SIZE).window.motion;
            const auto SIDE = shaped(MOTION, {.x = SIZE.x / 2, .y = 0});
            const auto TOP = shaped(MOTION, {.x = 0, .y = SIZE.y / 2});
            const auto CORNER = shaped(MOTION, {.x = SIZE.x / 2, .y = SIZE.y / 2});
            // As wide for its height as the window, and its sides bow gently outward.
            CHECK(std::abs((SIDE.x / TOP.y) - (SIZE.x / SIZE.y)) < .02);
            CHECK(SIDE.x > SIZE.x / 2 && SIDE.x < SIZE.x / 2 * 1.12);
            // The corners pull in: much more for the ellipse than for the pillow.
            const double PULLED = 1 - (CORNER.x / (SIZE.x / 2));
            if (SHAPE == BubbleShape::Pillow) {
                CHECK(PULLED > .08 && PULLED < .25);
            } else {
                CHECK(PULLED > .18);
            }
            // A pillow's sides stay nearly straight well toward its corners.
            if (SHAPE == BubbleShape::Pillow) {
                CHECK(shaped(MOTION, {SIZE.x / 2, SIZE.y / 4}).x > .98 * SIZE.x / 2);
            }
            const auto BOUNDS = bubble.bounds(SIZE);
            CHECK(BOUNDS.w >= 2 * SIDE.x && BOUNDS.h >= 2 * TOP.y);
        }
    }

    void contraction_follows_speed() {
        const auto SETTLE = [](double speed) {
            Bubble settling_bubble;
            double elapsed = 0;
            travel(
                settling_bubble,
                elapsed,
                1.2,
                [=](double) { return Vec2{.x = speed, .y = 0}; },
                ROUND);
            return roundness(settling_bubble);
        };
        const double SLOW = SETTLE(300);
        const double BRISK = SETTLE(700);
        const double FAST = SETTLE(1500);
        CHECK(SLOW > 0 && SLOW < BRISK && BRISK < FAST);
        CHECK(SLOW < .3 * FAST);
        // Slowing down without stopping relaxes it as it goes.
        Bubble bubble;
        double time = 0;
        travel(bubble, time, 1, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND);
        travel(bubble, time, 1.2, [](double) { return Vec2{.x = 300, .y = 0}; }, ROUND);
        CHECK(roundness(bubble) < .5 * FAST && roundness(bubble) > .5 * SLOW);
        // The speed of full contraction is the setting's.
        Bubble comparison;
        time = 0;
        auto lazy = ROUND;
        lazy.shape_speed = 4000;
        travel(comparison, time, 1.2, [](double) { return Vec2{.x = 1500, .y = 0}; }, lazy);
        CHECK(roundness(comparison) < .6 * FAST);
    }

    void pauses_and_resumptions_do_not_snap() {
        Bubble moving_bubble;
        double time = 0;
        double jump = 0;
        double previous = 0;
        double lowest = 1;
        const auto WATCH = [&](const Bubble &current) {
            jump = std::max(jump, std::abs(roundness(current) - previous));
            previous = roundness(current);
        };
        travel(
            moving_bubble, time, .8, [](double) { return Vec2{.x = 0, .y = 1500}; }, ROUND, WATCH);
        // A brief pause keeps the bubble round.
        travel(moving_bubble, time, .12, still, ROUND, [&](const Bubble &current) {
            WATCH(current);
            lowest = std::min(lowest, roundness(current));
        });
        CHECK(lowest > .8);
        // A longer one lets it spread; moving again pulls it back from wherever it got to.
        travel(moving_bubble, time, .45, still, ROUND, WATCH);
        const double SPREAD = roundness(moving_bubble);
        CHECK(SPREAD < .6);
        travel(
            moving_bubble, time, .5, [](double) { return Vec2{.x = -1500, .y = 0}; }, ROUND, WATCH);
        CHECK(roundness(moving_bubble) > SPREAD + .2);
        CHECK(jump < .08);
    }

    void roundness_zero_keeps_the_rectangle() {
        Bubble bubble;
        double time = 0;
        travel(
            bubble,
            time,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            PARAMS,
            [](const Bubble &current) { CHECK(roundness(current) == 0); });
        Bubble comparison;
        travel(
            comparison,
            time,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1, .roundness = 1},
            [](const Bubble &current) { CHECK(roundness(current) == 0); });
    }

    void microbubbles_ride_the_rounded_film() {
        Bubble bubble;
        bubble.seed_motion(13);
        double time = 0;
        const BubbleMotionParams PARAMS{
            .strength = .65,
            .microbubbles = 1,
            .trail = 1,
            .roundness = 1,
            .shape = BubbleShape::Circle,
            .shape_speed = 1200,
        };
        travel(bubble, time, .8, [](double) { return Vec2{.x = 1400, .y = 0}; }, PARAMS);
        bool seen = false;
        travel(
            bubble,
            time,
            1.5,
            [](double elapsed) { return Vec2{.x = 1400 * std::cos(elapsed), .y = 600}; },
            PARAMS,
            [&](const Bubble &current) {
                const auto FRAME = current.frame(SIZE);
                if (FRAME.window.motion.round < .8) {
                    return;
                }
                const double FILM_RADIUS = FRAME.window.motion.extent.x;
                for (const auto &microbubble : std::span(FRAME.micro).first(FRAME.micro_count)) {
                    const double ANCHOR_RADIUS =
                        std::hypot(microbubble.anchor.x, microbubble.anchor.y);
                    CHECK(ANCHOR_RADIUS > .8 * FILM_RADIUS && ANCHOR_RADIUS < 1.2 * FILM_RADIUS);
                    seen = true;
                }
                // Film is shed from the rounded outline, not from the rectangle's.
                for (const auto &point : current.motion().trail()) {
                    CHECK(std::hypot(point.position.x, point.position.y) < 1.2 * FILM_RADIUS);
                }
            });
        CHECK(seen);
    }

    void layout_moves_carry_the_film() {
        Bubble bubble;
        bubble.seed_motion(21);
        const BubbleMotionParams PARAMS{
            .strength = .65,
            .microbubbles = .5,
            .trail = .5,
            .roundness = 1,
        };
        // The layout swaps the window 640 px to the right within a tenth of a second.
        double drawn = 0;
        double previous = -1e9;
        double longest = 0;
        double arrived = -1;
        bool monotonic = true;
        bool contracted = false;
        bool covered = true;
        for (int frame = 0; frame < 240; ++frame) {
            if (frame < 6) {
                bubble.drive({.x = 640.0 / 6, .y = 0}, true);
            }
            bubble.advance(TIME_STEP, SIZE, BODY, PARAMS);
            const auto MOTION = bubble.frame(SIZE).window.motion;
            // Where the window's centre is drawn, relative to its real place.
            drawn = MOTION.carry.x;
            if (frame == 0) {
                CHECK(drawn < -640.0 / 6 * .9);
            }
            // Once the layout has placed the window, the film only closes in on it.
            if (frame >= 6) {
                monotonic = monotonic && drawn >= previous - 1e-9 && drawn <= 1e-9;
            }
            previous = drawn;
            contracted = contracted || MOTION.round > .1;
            const auto BOUNDS = bubble.bounds(SIZE);
            covered = covered && BOUNDS.x <= (SIZE.x / 2) + drawn - (BODY.w / 2) &&
                      BOUNDS.x + BOUNDS.w >= (SIZE.x / 2) + drawn + (BODY.w / 2);
            longest = std::max(longest, -drawn);
            if (arrived < 0 && drawn > -.05 * 640) {
                arrived = frame * TIME_STEP;
            }
            bubble.clear_emissions();
        }
        // It sets off from where the window was, travels over without overshooting, contracting
        // as it goes, and arrives within about half a second.
        CHECK(longest > 500 && monotonic && contracted && covered);
        CHECK(arrived > .3 && arrived < .7);
        CHECK(!bubble.stirred() && bubble.frame(SIZE).window.motion.still());
    }

    void deterministic_motion() {
        Bubble first_bubble;
        Bubble bubble;
        first_bubble.seed_motion(42);
        bubble.seed_motion(42);
        double first_time = 0;
        double second_time = 0;
        const auto VELOCITY = [](double elapsed) {
            return Vec2{.x = 1300 * std::cos(elapsed * 2), .y = 400};
        };
        travel(first_bubble,
            first_time,
            1.5,
            VELOCITY,
            {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(
            bubble, second_time, 1.5, VELOCITY, {.strength = .65, .microbubbles = 1, .trail = 1});
        const auto FIRST_FRAME = first_bubble.frame(SIZE);
        const auto SECOND_FRAME = bubble.frame(SIZE);
        CHECK(FIRST_FRAME.window.motion.drag.x == SECOND_FRAME.window.motion.drag.x &&
              FIRST_FRAME.micro_count == SECOND_FRAME.micro_count);
        first_bubble.drive({.x = std::nan(""), .y = 0});
        first_bubble.advance(TIME_STEP, SIZE, BODY, PARAMS);
        CHECK(std::isfinite(first_bubble.frame(SIZE).window.motion.drag.x));
        first_bubble.stop_motion();
        CHECK(!first_bubble.stirred() && first_bubble.frame(SIZE).window.motion.still());
    }
}  // namespace

int main() {
    resting_window_is_untouched();
    acceleration_lags_steady_travel_holds_and_stopping_overshoots();
    reversal_keeps_momentum();
    zero_strength_moves_undeformed();
    offset_keeps_the_leading_edge();
    microbubbles_form_migrate_and_detach();
    slow_movement_leaves_no_trail();
    close_inherits_the_deformation();
    opening_hands_over_to_movement();
    wake_drains_and_ends();
    deterministic_motion();
    rounds_up_while_moving_and_spreads_back();
    pillow_and_ellipse_keep_the_windows_proportions();
    contraction_follows_speed();
    pauses_and_resumptions_do_not_snap();
    roundness_zero_keeps_the_rectangle();
    microbubbles_ride_the_rounded_film();
    layout_moves_carry_the_film();
    return g_failures == 0 ? 0 : 1;
}
