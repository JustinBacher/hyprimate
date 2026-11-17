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
            const Vec2 v = velocity(t);
            b.drive({.x = v.x * DT, .y = v.y * DT});
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
        Bubble b;
        double t = 0;
        travel(b, t, 1, still);
        CHECK(!b.stirred());
        const auto f = b.frame(SIZE);
        CHECK(f.window.motion.still() && f.microCount == 0);
        CHECK(f.window.core.x == 0 && f.window.core.y == 0 && f.window.film == 0);
        CHECK(b.bounds(SIZE).w == SIZE.x + 16);
    }

    void accelerationLagsSteadyTravelHoldsAndStoppingOvershoots() {
        Bubble b;
        b.seedMotion(3);
        double t = 0;
        double peak = 0;
        // Setting off to the right: the trailing (left) film lags behind the leading edge.
        travel(
            b,
            t,
            .25,
            [](double) { return Vec2{.x = 1200, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) { peak = std::min(peak, drag(x)); });
        CHECK(peak < -8);
        CHECK(drag(b) < 0);
        // Sustained travel settles into a steady stretch, smaller than the acceleration's.
        travel(b, t, .6, [](double) { return Vec2{.x = 1200, .y = 0}; });
        const double STEADY = drag(b);
        double spread = 0;
        travel(
            b,
            t,
            .3,
            [](double) { return Vec2{.x = 1200, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) { spread = std::max(spread, std::abs(drag(x) - STEADY)); });
        CHECK(STEADY < -2 && STEADY > peak);
        CHECK(spread < 1.5);
        CHECK(b.stirred() && b.frame(SIZE).window.motion.sheen > .05);
        // Stopping: the fluid carries on past the window once, then settles without snapping.
        double overshoot = 0;
        double jump = 0;
        double previous = drag(b);
        travel(b, t, 3, still, PARAMS, [&](const Bubble &x) {
            const double d = drag(x);
            overshoot = std::max(overshoot, d);
            jump = std::max(jump, std::abs(d - previous));
            previous = d;
        });
        CHECK(overshoot > 2);
        CHECK(overshoot < 2 * std::abs(peak));
        CHECK(jump < 6);
        // At rest the window is exactly its undeformed self again.
        CHECK(!b.stirred());
        const auto f = b.frame(SIZE);
        CHECK(f.window.motion.still() && f.window.core.x == 0 && f.microCount == 0);
    }

    void reversalKeepsMomentum() {
        Bubble b;
        b.seedMotion(5);
        double t = 0;
        travel(b, t, .8, [](double) { return Vec2{.x = 1000, .y = 0}; });
        const double BEFORE = drag(b);
        CHECK(BEFORE < 0);
        // The window turns back; the fluid keeps going right for a moment.
        double first = 0;
        bool sampled = false;
        bool flipped = false;
        double jump = 0;
        double previous = BEFORE;
        travel(
            b,
            t,
            .6,
            [](double) { return Vec2{.x = -1000, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) {
                const double d = drag(x);
                if (!sampled) {
                    first = d, sampled = true;
                }
                flipped |= d > 4;
                jump = std::max(jump, std::abs(d - previous));
                previous = d;
            });
        CHECK(std::abs(first - BEFORE) < 6);  // no reset at the turn
        CHECK(flipped);  // trailing film is now on the right
        CHECK(jump < 6);
        double waves = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto &m = b.frame(SIZE).window.motion;
            waves += std::hypot(m.waveCos[i], m.waveSin[i]);
        }
        CHECK(waves > 0);
    }

    void zeroStrengthMovesUndeformed() {
        Bubble b;
        double t = 0;
        travel(
            b,
            t,
            .5,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1},
            [&](const Bubble &x) {
                const auto f = x.frame(SIZE);
                CHECK(
                    f.window.motion.drag.x == 0 && f.window.motion.sheen == 0 && f.microCount == 0);
                CHECK(x.motion().trail().empty() && x.motion().releases().empty());
            });
    }

    void offsetKeepsTheLeadingEdge() {
        BubbleDeformation m;
        m.drag = {.x = -20, .y = 0};
        const Vec2 HALF{.x = SIZE.x / 2, .y = SIZE.y / 2};
        const auto LEADING = bubbleMotionOffset(HALF, m, {.x = HALF.x, .y = 0});
        const auto TRAILING = bubbleMotionOffset(HALF, m, {.x = -HALF.x, .y = 0});
        CHECK(std::abs(LEADING.x) < 1e-9 && std::abs(LEADING.y) < 1e-9);
        CHECK(std::abs(TRAILING.x + 20) < 1e-9);
        // The trailing film narrows across the motion.
        const auto CORNER = bubbleMotionOffset(HALF, m, {.x = -HALF.x, .y = HALF.y});
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
        Bubble b;
        b.seedMotion(11);
        double t = 0;
        std::size_t most = 0;
        std::size_t releases = 0;
        std::size_t trail = 0;
        bool grew = false;
        bool rodeOut = false;
        bool trailedBehind = true;
        travel(
            b,
            t,
            4,
            [](double s) {
                return Vec2{.x = 1500 * std::cos(s * 1.6), .y = 500 * std::sin(s * 2.3)};
            },
            {.strength = .65, .microbubbles = 1, .trail = 1},
            [&](const Bubble &x) {
                const auto f = x.frame(SIZE);
                most = std::max(most, f.microCount);
                for (std::size_t i = 0; i < f.microCount; ++i) {
                    const auto &m = f.micro[i];
                    grew |= m.growth > 0 && m.growth < 1;
                    // A formed bubble sits outside the film it grew from, joined to it by its neck.
                    if (m.growth >= 1 && m.collapse == 0) {
                        rodeOut |=
                            std::abs(m.centre.x) > BODY.w / 2 || std::abs(m.centre.y) > BODY.h / 2;
                    }
                    CHECK(m.radius > 0 && m.radius < 20 && m.attach > 0);
                }
                for (const auto &r : x.motion().releases()) {
                    ++releases;
                    CHECK(r.micro.attach == 0 && r.life > 0);
                    // Released bubbles inherit the window's motion but fall behind it.
                    const double SPEED = x.motion().speed();
                    CHECK(std::hypot(r.velocity.x, r.velocity.y) < SPEED * 1.2 + 80);
                }
                for (const auto &p : x.motion().trail()) {
                    ++trail;
                    CHECK(p.density > 0 && p.density <= 1 && p.halfWidth > 0);
                    // The film comes off the trailing side.
                    trailedBehind &=
                        (p.position.x * p.direction.x) + (p.position.y * p.direction.y) < 0;
                }
            });
        CHECK(most > 0 && most <= kBubbleMicroCapacity);
        CHECK(grew && rodeOut);
        CHECK(releases > 0);
        CHECK(trail > 20 && trailedBehind);
        // Without movement nothing new forms once the window has come to a stop, and every bubble
        // ends.
        travel(b, t, .3, still, {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(
            b, t, 4, still, {.strength = .65, .microbubbles = 1, .trail = 1}, [&](const Bubble &x) {
                CHECK(x.motion().trail().empty() && x.motion().releases().empty());
            });
        CHECK(!b.stirred() && b.frame(SIZE).microCount == 0);
    }

    void slowMovementLeavesNoTrail() {
        Bubble b;
        double t = 0;
        std::size_t points = 0;
        travel(
            b,
            t,
            1,
            [](double) { return Vec2{.x = 90, .y = 0}; },
            PARAMS,
            [&](const Bubble &x) {
                points += x.motion().trail().size() + x.motion().releases().size();
            });
        CHECK(points == 0);
    }

    void closeInheritsTheDeformation() {
        Bubble b;
        double t = 0;
        travel(b, t, .3, [](double) { return Vec2{.x = 1400, .y = 0}; });
        const auto BEFORE = b.frame(SIZE);
        CHECK(BEFORE.window.motion.drag.x < 0);
        b.begin(Transition::Close, .42, 9, SIZE);
        const auto AFTER = b.frame(SIZE);
        CHECK(AFTER.window.motion.drag.x == BEFORE.window.motion.drag.x);
        CHECK(AFTER.window.core.x == BEFORE.window.core.x);
        CHECK(AFTER.microCount == BEFORE.microCount);
        b.advance(.42 * .55);
        const auto RELAXED = b.frame(SIZE);
        CHECK(RELAXED.window.motion.drag.x == 0 && RELAXED.window.motion.sheen == 0);
        const auto BOUNDS = b.bounds(SIZE);
        CHECK(BOUNDS.w > 0);
    }

    void openingHandsOverToMovement() {
        Bubble b;
        b.seedMotion(2);
        b.begin(Transition::Open, .62, 4, SIZE);
        double t = 0;
        while (!b.landed()) {
            b.advance(DT, SIZE, BODY, PARAMS);
            t += DT;
        }
        CHECK(b.active() && b.opening());
        const auto LANDED = b.frame(SIZE);
        // Movement adds to the settling core instead of replacing it.
        travel(b, t, .1, [](double) { return Vec2{.x = 1200, .y = 0}; });
        const auto MOVING = b.frame(SIZE);
        CHECK(MOVING.window.motion.drag.x < 0);
        CHECK(MOVING.window.halfSize.x > 0 && LANDED.window.thickness == MOVING.window.thickness);
        travel(b, t, 3, still);
        CHECK(!b.active() && !b.stirred());
        const auto REST = b.frame(SIZE);
        CHECK(REST.window.motion.still() && REST.window.core.x == 0 && REST.window.film == 0);
    }

    void wakeDrainsAndEnds() {
        BubbleWake wake(7);
        CHECK(wake.done() && wake.bounds().w == 0);
        for (int i = 0; i < 20; ++i) {
            CHECK(wake.extend({{-450, 0}, {1, 0}, 80, .8, i == 0}, {100.0 + i * 20, 300}));
        }
        BubbleRelease r;
        r.micro.radius = 5;
        r.micro.thickness = 500;
        r.velocity = {.x = 600, .y = 0};
        r.life = .4;
        CHECK(wake.release(r, {500, 300}));
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
            BubbleWakeView a;
            BubbleWakeView b;
            wake.view(a);
            const double x0 = a.bubbles[0].centre.x;
            wake.advance(DT);
            wake.view(b);
            if (b.bubbleCount != 0u) {
                moved = b.bubbles[0].centre.x - x0;
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
            accepted += static_cast<std::size_t>(full.extend({.position = {.x = 0, .y = 0},
                                                                 .direction = {.x = 1, .y = 0},
                                                                 .halfWidth = 40,
                                                                 .density = 1,
                                                                 .start = false},
                {.x = static_cast<double>(i), .y = 0}));
        }
        CHECK(accepted == kBubbleWakeNodes && full.full());
    }

    constexpr BubbleMotionParams ROUND{.strength = .65,
        .microbubbles = .5,
        .trail = .5,
        .roundness = 1,
        .shape = BubbleShape::Circle,
        .shapeSpeed = 1200};
    double roundness(const Bubble &b) {
        return b.frame(SIZE).window.motion.round;
    }
    // Where contraction carries a point of the membrane's outline.
    Vec2 shaped(const BubbleDeformation &m, Vec2 q) {
        return bubbleRoundPoint({.x = SIZE.x / 2, .y = SIZE.y / 2}, m, q);
    }
    // Distance from the centre of a rounded point of the membrane's outline.
    double reach(const BubbleDeformation &m, Vec2 q) {
        const auto p = shaped(m, q);
        return std::hypot(p.x, p.y);
    }

    void roundsUpWhileMovingAndSpreadsBack() {
        Bubble b;
        b.seedMotion(8);
        double t = 0;
        double jump = 0;
        double previous = 0;
        const auto SMOOTH = [&](const Bubble &x) {
            jump = std::max(jump, std::abs(roundness(x) - previous));
            previous = roundness(x);
        };
        // A quick flick already rounds the window visibly.
        travel(b, t, .15, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND, SMOOTH);
        CHECK(roundness(b) > .25);
        travel(b, t, 1, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND, SMOOTH);
        const auto m = b.frame(SIZE).window.motion;
        CHECK(m.round > .85 && m.round < 1);
        // Nearly circular: the corners have pulled in to the edges' distance, wide as it is.
        const double SIDE = reach(m, {.x = SIZE.x / 2, .y = 0});
        const double TOP = reach(m, {.x = 0, .y = SIZE.y / 2});
        const double CORNER = reach(m, {.x = SIZE.x / 2, .y = SIZE.y / 2});
        CHECK(std::abs(SIDE - TOP) < .08 * SIDE && std::abs(CORNER - SIDE) < .12 * SIDE);
        CHECK(SIDE < SIZE.x / 2);
        const auto BOUNDS = b.bounds(SIZE);
        CHECK(BOUNDS.h >= 2 * TOP && BOUNDS.w >= 2 * SIDE);
        // Stopping spreads it back out, past the rectangle a little, then exactly to it.
        double least = 1;
        travel(b, t, 3, still, ROUND, [&](const Bubble &x) {
            SMOOTH(x);
            least = std::min(least, roundness(x));
        });
        CHECK(least < 0 && least > -.12);
        CHECK(jump < .08);
        CHECK(!b.stirred() && b.frame(SIZE).window.motion.still());
    }

    void pillowAndEllipseKeepTheWindowsProportions() {
        for (const auto SHAPE : {BubbleShape::Pillow, BubbleShape::Ellipse}) {
            Bubble b;
            double t = 0;
            travel(b,
                t,
                1.2,
                [](double) { return Vec2{.x = 1500, .y = 0}; },
                {.strength = .65, .microbubbles = .5, .trail = .5, .roundness = 1, .shape = SHAPE});
            const auto m = b.frame(SIZE).window.motion;
            const auto SIDE = shaped(m, {.x = SIZE.x / 2, .y = 0});
            const auto TOP = shaped(m, {.x = 0, .y = SIZE.y / 2});
            const auto CORNER = shaped(m, {.x = SIZE.x / 2, .y = SIZE.y / 2});
            // As wide for its height as the window, and its sides bow gently outward.
            CHECK(std::abs(SIDE.x / TOP.y - SIZE.x / SIZE.y) < .02);
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
                CHECK(shaped(m, {SIZE.x / 2, SIZE.y / 4}).x > .98 * SIZE.x / 2);
            }
            const auto BOUNDS = b.bounds(SIZE);
            CHECK(BOUNDS.w >= 2 * SIDE.x && BOUNDS.h >= 2 * TOP.y);
        }
    }

    void contractionFollowsSpeed() {
        const auto SETTLE = [](double speed) {
            Bubble b;
            double t = 0;
            travel(b, t, 1.2, [=](double) { return Vec2{.x = speed, .y = 0}; }, ROUND);
            return roundness(b);
        };
        const double SLOW = SETTLE(300);
        const double BRISK = SETTLE(700);
        const double FAST = SETTLE(1500);
        CHECK(SLOW > 0 && SLOW < BRISK && BRISK < FAST);
        CHECK(SLOW < .3 * FAST);
        // Slowing down without stopping relaxes it as it goes.
        Bubble b;
        double t = 0;
        travel(b, t, 1, [](double) { return Vec2{.x = 1500, .y = 0}; }, ROUND);
        travel(b, t, 1.2, [](double) { return Vec2{.x = 300, .y = 0}; }, ROUND);
        CHECK(roundness(b) < .5 * FAST && roundness(b) > .5 * SLOW);
        // The speed of full contraction is the setting's.
        Bubble c;
        t = 0;
        auto lazy = ROUND;
        lazy.shapeSpeed = 4000;
        travel(c, t, 1.2, [](double) { return Vec2{.x = 1500, .y = 0}; }, lazy);
        CHECK(roundness(c) < .6 * FAST);
    }

    void pausesAndResumptionsDoNotSnap() {
        Bubble b;
        double t = 0;
        double jump = 0;
        double previous = 0;
        double lowest = 1;
        const auto WATCH = [&](const Bubble &x) {
            jump = std::max(jump, std::abs(roundness(x) - previous));
            previous = roundness(x);
        };
        travel(b, t, .8, [](double) { return Vec2{.x = 0, .y = 1500}; }, ROUND, WATCH);
        // A brief pause keeps the bubble round.
        travel(b, t, .12, still, ROUND, [&](const Bubble &x) {
            WATCH(x);
            lowest = std::min(lowest, roundness(x));
        });
        CHECK(lowest > .8);
        // A longer one lets it spread; moving again pulls it back from wherever it got to.
        travel(b, t, .45, still, ROUND, WATCH);
        const double SPREAD = roundness(b);
        CHECK(SPREAD < .6);
        travel(b, t, .5, [](double) { return Vec2{.x = -1500, .y = 0}; }, ROUND, WATCH);
        CHECK(roundness(b) > SPREAD + .2);
        CHECK(jump < .08);
    }

    void roundnessZeroKeepsTheRectangle() {
        Bubble b;
        double t = 0;
        travel(
            b,
            t,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            PARAMS,
            [](const Bubble &x) { CHECK(roundness(x) == 0); });
        Bubble c;
        travel(
            c,
            t,
            .8,
            [](double) { return Vec2{.x = 1500, .y = 0}; },
            {.strength = 0, .microbubbles = 1, .trail = 1, .roundness = 1},
            [](const Bubble &x) { CHECK(roundness(x) == 0); });
    }

    void microbubblesRideTheRoundedFilm() {
        Bubble b;
        b.seedMotion(13);
        double t = 0;
        const BubbleMotionParams PARAMS{.strength = .65,
            .microbubbles = 1,
            .trail = 1,
            .roundness = 1,
            .shape = BubbleShape::Circle,
            .shapeSpeed = 1200};
        travel(b, t, .8, [](double) { return Vec2{.x = 1400, .y = 0}; }, PARAMS);
        bool seen = false;
        travel(
            b,
            t,
            1.5,
            [](double s) { return Vec2{.x = 1400 * std::cos(s), .y = 600}; },
            PARAMS,
            [&](const Bubble &x) {
                const auto f = x.frame(SIZE);
                if (f.window.motion.round < .8) {
                    return;
                }
                const double R = f.window.motion.extent.x;
                for (std::size_t i = 0; i < f.microCount; ++i) {
                    const double r = std::hypot(f.micro[i].anchor.x, f.micro[i].anchor.y);
                    CHECK(r > .8 * R && r < 1.2 * R);
                    seen = true;
                }
                // Film is shed from the rounded outline, not from the rectangle's.
                for (const auto &p : x.motion().trail()) {
                    CHECK(std::hypot(p.position.x, p.position.y) < 1.2 * R);
                }
            });
        CHECK(seen);
    }

    void layoutMovesCarryTheFilm() {
        Bubble b;
        b.seedMotion(21);
        const BubbleMotionParams PARAMS{
            .strength = .65, .microbubbles = .5, .trail = .5, .roundness = 1};
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
                b.drive({.x = 640.0 / 6, .y = 0}, true);
            }
            b.advance(DT, SIZE, BODY, PARAMS);
            const auto m = b.frame(SIZE).window.motion;
            // Where the window's centre is drawn, relative to its real place.
            drawn = m.carry.x;
            if (frame == 0) {
                CHECK(drawn < -640.0 / 6 * .9);
            }
            // Once the layout has placed the window, the film only closes in on it.
            if (frame >= 6) {
                monotonic = monotonic && drawn >= previous - 1e-9 && drawn <= 1e-9;
            }
            previous = drawn;
            contracted = contracted || m.round > .1;
            const auto BOUNDS = b.bounds(SIZE);
            covered = covered && BOUNDS.x <= (SIZE.x / 2) + drawn - (BODY.w / 2) &&
                      BOUNDS.x + BOUNDS.w >= (SIZE.x / 2) + drawn + (BODY.w / 2);
            longest = std::max(longest, -drawn);
            if (arrived < 0 && drawn > -.05 * 640) {
                arrived = frame * DT;
            }
            b.clearEmissions();
        }
        // It sets off from where the window was, travels over without overshooting, contracting
        // as it goes, and arrives within about half a second.
        CHECK(longest > 500 && monotonic && contracted && covered);
        CHECK(arrived > .3 && arrived < .7);
        CHECK(!b.stirred() && b.frame(SIZE).window.motion.still());
    }

    void deterministicMotion() {
        Bubble a;
        Bubble b;
        a.seedMotion(42);
        b.seedMotion(42);
        double ta = 0;
        double tb = 0;
        const auto v = [](double s) { return Vec2{.x = 1300 * std::cos(s * 2), .y = 400}; };
        travel(a, ta, 1.5, v, {.strength = .65, .microbubbles = 1, .trail = 1});
        travel(b, tb, 1.5, v, {.strength = .65, .microbubbles = 1, .trail = 1});
        const auto x = a.frame(SIZE);
        const auto y = b.frame(SIZE);
        CHECK(x.window.motion.drag.x == y.window.motion.drag.x && x.microCount == y.microCount);
        a.drive({.x = std::nan(""), .y = 0});
        a.advance(DT, SIZE, BODY, PARAMS);
        CHECK(std::isfinite(a.frame(SIZE).window.motion.drag.x));
        a.stopMotion();
        CHECK(!a.stirred() && a.frame(SIZE).window.motion.still());
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
