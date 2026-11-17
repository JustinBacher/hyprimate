#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>

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
    constexpr double DT = 1.0 / 60.0;

    // Moves the window at velocity(t) px/s for the given time, one frame at a time.
    void travel(Bubble &b,
        double &t,
        double seconds,
        const std::function<Vec2(double)> &velocity,
        BubbleMotionParams params = PARAMS,
        const std::function<void(const Bubble &)> &each = {}) {
        for (const double END = t + seconds; t < END - 1e-9; t += DT) {
            const Vec2 VELOCITY = velocity(t);
            b.drive({.x = VELOCITY.x * DT, .y = VELOCITY.y * DT});
            b.advance(DT, SIZE, BODY, params);
            if (each) {
                each(b);
            }
            b.clearEmissions();
        }
    }
    Vec2 still(double) {
        return {};
    }
    double drag(const Bubble &b) {
        return b.frame(SIZE).window.motion.drag.x;
    }

    void restingWindowIsUntouched() {
        Bubble bubble;
        double time = 0;
        travel(bubble, time, 1, still);
        CHECK(!bubble.stirred());
        const auto FRAME = bubble.frame(SIZE);
        CHECK(FRAME.window.motion.still() && FRAME.microCount == 0);
        CHECK(FRAME.window.core.x == 0 && FRAME.window.core.y == 0 && FRAME.window.film == 0);
        CHECK(bubble.bounds(SIZE).w == SIZE.x + 16);
    }

    void accelerationLagsSteadyTravelHoldsAndStoppingOvershoots() {
        Bubble bubble;
        bubble.seedMotion(3);
        double time = 0;
        double peak = 0;
        // Setting off to the right: the trailing (left) film lags behind the leading edge.
        travel(
            bubble,
            time,
            .25,
            [](double) { return Vec2{.x = 1200, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) { peak = std::min(peak, drag(x)); });
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
            [&](const Bubble &x) { spread = std::max(spread, std::abs(drag(x) - STEADY)); });
        CHECK(STEADY < -2 && STEADY > peak);
        CHECK(spread < 1.5);
        CHECK(bubble.stirred() && bubble.frame(SIZE).window.motion.sheen > .05);
        // Stopping: the fluid carries on past the window once, then settles without snapping.
        double overshoot = 0;
        double jump = 0;
        double previous = drag(bubble);
        travel(bubble, time, 3, still, PARAMS, [&](const Bubble &x) {
            const double DRAG = drag(x);
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
        CHECK(FRAME.window.motion.still() && FRAME.window.core.x == 0 && FRAME.microCount == 0);
    }

    void reversalKeepsMomentum() {
        Bubble bubble;
        bubble.seedMotion(5);
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
            [&](const Bubble &x) {
                const double DRAG = drag(x);
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
        for (std::size_t i = 0; i < 4; ++i) {
            const auto &deformation = bubble.frame(SIZE).window.motion;
            waves += std::hypot(deformation.waveCos[i], deformation.waveSin[i]);
        }
        CHECK(waves > 0);
    }

    void zeroStrengthMovesUndeformed() {
        Bubble bubble;
        double time = 0;
        travel(
            bubble,
            time,
            .5,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1},
            [&](const Bubble &x) {
                const auto FRAME = x.frame(SIZE);
                CHECK(FRAME.window.motion.drag.x == 0 && FRAME.window.motion.sheen == 0 &&
                      FRAME.microCount == 0);
                CHECK(x.motion().trail().empty() && x.motion().releases().empty());
            });
    }

    void offsetKeepsTheLeadingEdge() {
        BubbleDeformation deformation;
        deformation.drag = {.x = -20, .y = 0};
        const Vec2 HALF{.x = SIZE.x / 2, .y = SIZE.y / 2};
        const auto LEADING = bubbleMotionOffset(HALF, deformation, {.x = HALF.x, .y = 0});
        const auto TRAILING = bubbleMotionOffset(HALF, deformation, {.x = -HALF.x, .y = 0});
        CHECK(std::abs(LEADING.x) < 1e-9 && std::abs(LEADING.y) < 1e-9);
        CHECK(std::abs(TRAILING.x + 20) < 1e-9);
        // The trailing film narrows across the motion.
        const auto CORNER = bubbleMotionOffset(HALF, deformation, {.x = -HALF.x, .y = HALF.y});
        CHECK(CORNER.y < 0);
        const auto NONE = bubbleMotionOffset(HALF, {}, {.x = 123, .y = -45});
        CHECK(NONE.x == 0 && NONE.y == 0);
        // Capillary modes move the outline, not the centre.
        BubbleDeformation wave;
        wave.waveCos[0] = 3;
        const auto CENTRE = bubbleMotionOffset(HALF, wave, {.x = 0, .y = 0});
        const auto EDGE = bubbleMotionOffset(HALF, wave, {.x = HALF.x, .y = 0});
        CHECK(CENTRE.x == 0 && CENTRE.y == 0);
        CHECK(std::abs(EDGE.x - 3) < 1e-9);
    }

    void microbubblesFormMigrateAndDetach() {
        Bubble movingBubble;
        movingBubble.seedMotion(11);
        double time = 0;
        std::size_t most = 0;
        std::size_t releases = 0;
        std::size_t trail = 0;
        bool grew = false;
        bool rodeOut = false;
        bool trailedBehind = true;
        travel(
            movingBubble,
            time,
            4,
            [](double s) {
                return Vec2{.x = 1500 * std::cos(s * 1.6), .y = 500 * std::sin(s * 2.3)};
            },
            {.strength = .65, .microbubbles = 1, .trail = 1},
            [&](const Bubble &x) {
                const auto FRAME = x.frame(SIZE);
                most = std::max(most, FRAME.microCount);
                for (std::size_t i = 0; i < FRAME.microCount; ++i) {
                    const auto &microbubble = FRAME.micro[i];
                    grew |= microbubble.growth > 0 && microbubble.growth < 1;
                    // A formed bubble sits outside the film it grew from, joined to it by its neck.
                    if (microbubble.growth >= 1 && microbubble.collapse == 0) {
                        rodeOut |= std::abs(microbubble.centre.x) > BODY.w / 2 ||
                                   std::abs(microbubble.centre.y) > BODY.h / 2;
                    }
                    CHECK(microbubble.radius > 0 && microbubble.radius < 20 &&
                          microbubble.attach > 0);
                }
                for (const auto &release : x.motion().releases()) {
                    ++releases;
                    CHECK(release.micro.attach == 0 && release.life > 0);
                    // Released bubbles inherit the window's motion but fall behind it.
                    const double SPEED = x.motion().speed();
                    CHECK(std::hypot(release.velocity.x, release.velocity.y) < (SPEED * 1.2) + 80);
                }
                for (const auto &point : x.motion().trail()) {
                    ++trail;
                    CHECK(point.density > 0 && point.density <= 1 && point.halfWidth > 0);
                    // The film comes off the trailing side.
                    trailedBehind &= (point.position.x * point.direction.x) +
                                         (point.position.y * point.direction.y) <
                                     0;
                }
            });
        CHECK(most > 0 && most <= kBubbleMicroCapacity);
        CHECK(grew && rodeOut);
        CHECK(releases > 0);
        CHECK(trail > 20 && trailedBehind);
        // Without movement nothing new forms once the window has come to a stop, and every bubble
        // ends.
        travel(movingBubble, time, .3, still, {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(movingBubble,
            time,
            4,
            still,
            {.strength = .65, .microbubbles = 1, .trail = 1},
            [&](const Bubble &x) {
                CHECK(x.motion().trail().empty() && x.motion().releases().empty());
            });
        CHECK(!movingBubble.stirred() && movingBubble.frame(SIZE).microCount == 0);
    }

    void slowMovementLeavesNoTrail() {
        Bubble bubble;
        double time = 0;
        std::size_t points = 0;
        travel(
            bubble,
            time,
            1,
            [](double) { return Vec2{.x = 90, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) {
                points += x.motion().trail().size() + x.motion().releases().size();
            });
        CHECK(points == 0);
    }

    void closeInheritsTheDeformation() {
        Bubble bubble;
        double time = 0;
        travel(bubble, time, .3, [](double) { return Vec2{.x = 1400, .y = 0}; });
        const auto BEFORE = bubble.frame(SIZE);
        CHECK(BEFORE.window.motion.drag.x < 0);
        bubble.begin(Transition::Close, .42, 9, SIZE);
        const auto AFTER = bubble.frame(SIZE);
        CHECK(AFTER.window.motion.drag.x == BEFORE.window.motion.drag.x);
        CHECK(AFTER.window.core.x == BEFORE.window.core.x);
        CHECK(AFTER.microCount == BEFORE.microCount);
        bubble.advance(.42 * .55);
        const auto RELAXED = bubble.frame(SIZE);
        CHECK(RELAXED.window.motion.drag.x == 0 && RELAXED.window.motion.sheen == 0);
        const auto BOUNDS = bubble.bounds(SIZE);
        CHECK(BOUNDS.w > 0);
    }

    void openingHandsOverToMovement() {
        Bubble bubble;
        bubble.seedMotion(2);
        bubble.begin(Transition::Open, .62, 4, SIZE);
        double time = 0;
        while (!bubble.landed()) {
            bubble.advance(DT, SIZE, BODY, PARAMS);
            time += DT;
        }
        CHECK(bubble.active() && bubble.opening());
        const auto LANDED = bubble.frame(SIZE);
        // Movement adds to the settling core instead of replacing it.
        travel(bubble, time, .1, [](double) { return Vec2{.x = 1200, .y = 0}; });
        const auto MOVING = bubble.frame(SIZE);
        CHECK(MOVING.window.motion.drag.x < 0);
        CHECK(MOVING.window.halfSize.x > 0 && LANDED.window.thickness == MOVING.window.thickness);
        travel(bubble, time, 3, still);
        CHECK(!bubble.active() && !bubble.stirred());
        const auto REST = bubble.frame(SIZE);
        CHECK(REST.window.motion.still() && REST.window.core.x == 0 && REST.window.film == 0);
    }

    void wakeDrainsAndEnds() {
        BubbleWake wake(7);
        CHECK(wake.done() && wake.bounds().w == 0);
        for (int i = 0; i < 20; ++i) {
            CHECK(wake.extend({{-450, 0}, {1, 0}, 80, .8, i == 0}, {100.0 + (i * 20), 300}));
        }
        BubbleRelease bubbleRelease;
        bubbleRelease.micro.radius = 5;
        bubbleRelease.micro.thickness = 500;
        bubbleRelease.velocity = {.x = 600, .y = 0};
        bubbleRelease.life = .4;
        CHECK(wake.release(bubbleRelease, {500, 300}));
        const auto FIRST = wake.bounds();
        CHECK(FIRST.w > 380 && FIRST.h >= 160);
        BubbleWakeView view;
        wake.view(view);
        CHECK(view.nodeCount == 20 && view.bubbleCount == 1);
        CHECK(std::abs(view.size.x - FIRST.w) < 1e-9);
        CHECK(view.nodes[19].arc > view.nodes[0].arc);
        // A free bubble drifts on a little, then slows.
        double moved = 0;
        for (int i = 0; i < 12; ++i) {
            BubbleWakeView beforeView;
            BubbleWakeView afterView;
            wake.view(beforeView);
            const double START_X = beforeView.bubbles[0].centre.x;
            wake.advance(DT);
            wake.view(afterView);
            if (afterView.bubbleCount != 0U) {
                moved = afterView.bubbles[0].centre.x - START_X;
            }
        }
        CHECK(moved > 0 && moved < 600 * DT);
        // Film thins and goes, oldest first and never all at once, and the wake ends.
        BubbleWakeView mid;
        for (int i = 0; i < 30; ++i) {
            wake.advance(DT);
        }
        wake.view(mid);
        CHECK(mid.nodeCount > 0 && mid.nodes[0].fade > 0);
        for (int i = 0; i < 300 && !wake.done(); ++i) {
            wake.advance(DT);
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
                    .halfWidth = 40,
                    .density = 1,
                    .start = false,
                },
                {.x = static_cast<double>(i), .y = 0}));
        }
        CHECK(accepted == kBubbleWakeNodes && full.full());
    }

    constexpr BubbleMotionParams ROUND{
        .strength = .65,
        .microbubbles = .5,
        .trail = .5,
        .roundness = 1,
        .shape = BubbleShape::Circle,
        .shapeSpeed = 1200,
    };
    double roundness(const Bubble &b) {
        return b.frame(SIZE).window.motion.round;
    }
    // Where contraction carries a point of the membrane's outline.
    Vec2 shaped(const BubbleDeformation &m, Vec2 q) {
        return bubbleRoundPoint({.x = SIZE.x / 2, .y = SIZE.y / 2}, m, q);
    }
    // Distance from the centre of a rounded point of the membrane's outline.
    double reach(const BubbleDeformation &m, Vec2 q) {
        const auto POINT = shaped(m, q);
        return std::hypot(POINT.x, POINT.y);
    }

    void roundsUpWhileMovingAndSpreadsBack() {
        Bubble bubble;
        bubble.seedMotion(8);
        double time = 0;
        double jump = 0;
        double previous = 0;
        const auto SMOOTH = [&](const Bubble &x) {
            jump = std::max(jump, std::abs(roundness(x) - previous));
            previous = roundness(x);
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
        travel(bubble, time, 3, still, ROUND, [&](const Bubble &x) {
            SMOOTH(x);
            least = std::min(least, roundness(x));
        });
        CHECK(least < 0 && least > -.12);
        CHECK(jump < .08);
        CHECK(!bubble.stirred() && bubble.frame(SIZE).window.motion.still());
    }

    void pillowAndEllipseKeepTheWindowsProportions() {
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

    void contractionFollowsSpeed() {
        const auto SETTLE = [](double speed) {
            Bubble settlingBubble;
            double elapsed = 0;
            travel(
                settlingBubble,
                elapsed,
                1.2,
                [=](double) { return Vec2{.x = speed, .y = 0}; },
                ROUND);
            return roundness(settlingBubble);
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
        lazy.shapeSpeed = 4000;
        travel(comparison, time, 1.2, [](double) { return Vec2{.x = 1500, .y = 0}; }, lazy);
        CHECK(roundness(comparison) < .6 * FAST);
    }

    void pausesAndResumptionsDoNotSnap() {
        Bubble movingBubble;
        double time = 0;
        double jump = 0;
        double previous = 0;
        double lowest = 1;
        const auto WATCH = [&](const Bubble &x) {
            jump = std::max(jump, std::abs(roundness(x) - previous));
            previous = roundness(x);
        };
        travel(
            movingBubble, time, .8, [](double) { return Vec2{.x = 0, .y = 1500}; }, ROUND, WATCH);
        // A brief pause keeps the bubble round.
        travel(movingBubble, time, .12, still, ROUND, [&](const Bubble &x) {
            WATCH(x);
            lowest = std::min(lowest, roundness(x));
        });
        CHECK(lowest > .8);
        // A longer one lets it spread; moving again pulls it back from wherever it got to.
        travel(movingBubble, time, .45, still, ROUND, WATCH);
        const double SPREAD = roundness(movingBubble);
        CHECK(SPREAD < .6);
        travel(
            movingBubble, time, .5, [](double) { return Vec2{.x = -1500, .y = 0}; }, ROUND, WATCH);
        CHECK(roundness(movingBubble) > SPREAD + .2);
        CHECK(jump < .08);
    }

    void roundnessZeroKeepsTheRectangle() {
        Bubble bubble;
        double time = 0;
        travel(
            bubble,
            time,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            PARAMS,
            [](const Bubble &x) { CHECK(roundness(x) == 0); });
        Bubble comparison;
        travel(
            comparison,
            time,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1, .roundness = 1},
            [](const Bubble &x) { CHECK(roundness(x) == 0); });
    }

    void microbubblesRideTheRoundedFilm() {
        Bubble bubble;
        bubble.seedMotion(13);
        double time = 0;
        const BubbleMotionParams PARAMS{
            .strength = .65,
            .microbubbles = 1,
            .trail = 1,
            .roundness = 1,
            .shape = BubbleShape::Circle,
            .shapeSpeed = 1200,
        };
        travel(bubble, time, .8, [](double) { return Vec2{.x = 1400, .y = 0}; }, PARAMS);
        bool seen = false;
        travel(
            bubble,
            time,
            1.5,
            [](double s) { return Vec2{.x = 1400 * std::cos(s), .y = 600}; },
            PARAMS,
            [&](const Bubble &x) {
                const auto FRAME = x.frame(SIZE);
                if (FRAME.window.motion.round < .8) {
                    return;
                }
                const double FILM_RADIUS = FRAME.window.motion.extent.x;
                for (std::size_t i = 0; i < FRAME.microCount; ++i) {
                    const double ANCHOR_RADIUS =
                        std::hypot(FRAME.micro[i].anchor.x, FRAME.micro[i].anchor.y);
                    CHECK(ANCHOR_RADIUS > .8 * FILM_RADIUS && ANCHOR_RADIUS < 1.2 * FILM_RADIUS);
                    seen = true;
                }
                // Film is shed from the rounded outline, not from the rectangle's.
                for (const auto &point : x.motion().trail()) {
                    CHECK(std::hypot(point.position.x, point.position.y) < 1.2 * FILM_RADIUS);
                }
            });
        CHECK(seen);
    }

    void layoutMovesCarryTheFilm() {
        Bubble bubble;
        bubble.seedMotion(21);
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
            bubble.advance(DT, SIZE, BODY, PARAMS);
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
                arrived = frame * DT;
            }
            bubble.clearEmissions();
        }
        // It sets off from where the window was, travels over without overshooting, contracting
        // as it goes, and arrives within about half a second.
        CHECK(longest > 500 && monotonic && contracted && covered);
        CHECK(arrived > .3 && arrived < .7);
        CHECK(!bubble.stirred() && bubble.frame(SIZE).window.motion.still());
    }

    void deterministicMotion() {
        Bubble firstBubble;
        Bubble bubble;
        firstBubble.seedMotion(42);
        bubble.seedMotion(42);
        double firstTime = 0;
        double secondTime = 0;
        const auto VELOCITY = [](double s) { return Vec2{.x = 1300 * std::cos(s * 2), .y = 400}; };
        travel(firstBubble,
            firstTime,
            1.5,
            VELOCITY,
            {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(bubble, secondTime, 1.5, VELOCITY, {.strength = .65, .microbubbles = 1, .trail = 1});
        const auto FIRST_FRAME = firstBubble.frame(SIZE);
        const auto SECOND_FRAME = bubble.frame(SIZE);
        CHECK(FIRST_FRAME.window.motion.drag.x == SECOND_FRAME.window.motion.drag.x &&
              FIRST_FRAME.microCount == SECOND_FRAME.microCount);
        firstBubble.drive({.x = std::nan(""), .y = 0});
        firstBubble.advance(DT, SIZE, BODY, PARAMS);
        CHECK(std::isfinite(firstBubble.frame(SIZE).window.motion.drag.x));
        firstBubble.stopMotion();
        CHECK(!firstBubble.stirred() && firstBubble.frame(SIZE).window.motion.still());
    }
}  // namespace

int main() {
    restingWindowIsUntouched();
    accelerationLagsSteadyTravelHoldsAndStoppingOvershoots();
    reversalKeepsMomentum();
    zeroStrengthMovesUndeformed();
    offsetKeepsTheLeadingEdge();
    microbubblesFormMigrateAndDetach();
    slowMovementLeavesNoTrail();
    closeInheritsTheDeformation();
    openingHandsOverToMovement();
    wakeDrainsAndEnds();
    deterministicMotion();
    roundsUpWhileMovingAndSpreadsBack();
    pillowAndEllipseKeepTheWindowsProportions();
    contractionFollowsSpeed();
    pausesAndResumptionsDoNotSnap();
    roundnessZeroKeepsTheRectangle();
    microbubblesRideTheRoundedFilm();
    layoutMovesCarryTheFilm();
    return g_failures == 0 ? 0 : 1;
}
