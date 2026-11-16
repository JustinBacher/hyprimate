#include <algorithm>
#include <cmath>
#include <functional>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleWake.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{900, 600};
    constexpr Box BODY{-440, -290, 880, 580};
    constexpr BubbleMotionParams PARAMS{.65, .5, .5};
    constexpr double DT = 1.0 / 60.0;

    // Moves the window at velocity(t) px/s for the given time, one frame at a time.
    void travel(Bubble &b,
        double &t,
        double seconds,
        const std::function<Vec2(double)> &velocity,
        BubbleMotionParams params = PARAMS,
        const std::function<void(const Bubble &)> &each = {}) {
        for (double end = t + seconds; t < end - 1e-9; t += DT) {
            const Vec2 v = velocity(t);
            b.drive({v.x * DT, v.y * DT});
            b.advance(DT, SIZE, BODY, params);
            if (each) each(b);
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
            [](double) { return Vec2{1200, 0}; },
            PARAMS,
            [&](const Bubble &x) { peak = std::min(peak, drag(x)); });
        CHECK(peak < -8);
        CHECK(drag(b) < 0);
        // Sustained travel settles into a steady stretch, smaller than the acceleration's.
        travel(b, t, .6, [](double) { return Vec2{1200, 0}; });
        const double steady = drag(b);
        double spread = 0;
        travel(
            b,
            t,
            .3,
            [](double) { return Vec2{1200, 0}; },
            PARAMS,
            [&](const Bubble &x) { spread = std::max(spread, std::abs(drag(x) - steady)); });
        CHECK(steady < -2 && steady > peak);
        CHECK(spread < 1.5);
        CHECK(b.stirred() && b.frame(SIZE).window.motion.sheen > .05);
        // Stopping: the fluid carries on past the window once, then settles without snapping.
        double overshoot = 0, jump = 0, previous = drag(b);
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
        travel(b, t, .8, [](double) { return Vec2{1000, 0}; });
        const double before = drag(b);
        CHECK(before < 0);
        // The window turns back; the fluid keeps going right for a moment.
        double first = 0;
        bool sampled = false, flipped = false;
        double jump = 0, previous = before;
        travel(
            b,
            t,
            .6,
            [](double) { return Vec2{-1000, 0}; },
            PARAMS,
            [&](const Bubble &x) {
                const double d = drag(x);
                if (!sampled) first = d, sampled = true;
                flipped |= d > 4;
                jump = std::max(jump, std::abs(d - previous));
                previous = d;
            });
        CHECK(std::abs(first - before) < 6);  // no reset at the turn
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
            [](double) { return Vec2{1500, 0}; },
            {0, 1, 1},
            [&](const Bubble &x) {
                const auto f = x.frame(SIZE);
                CHECK(
                    f.window.motion.drag.x == 0 && f.window.motion.sheen == 0 && f.microCount == 0);
                CHECK(x.motion().trail().empty() && x.motion().releases().empty());
            });
    }

    void offsetKeepsTheLeadingEdge() {
        BubbleDeformation m;
        m.drag = {-20, 0};
        const Vec2 half{SIZE.x / 2, SIZE.y / 2};
        const auto leading = bubbleMotionOffset(half, m, {half.x, 0});
        const auto trailing = bubbleMotionOffset(half, m, {-half.x, 0});
        CHECK(std::abs(leading.x) < 1e-9 && std::abs(leading.y) < 1e-9);
        CHECK(std::abs(trailing.x + 20) < 1e-9);
        // The trailing film narrows across the motion.
        const auto corner = bubbleMotionOffset(half, m, {-half.x, half.y});
        CHECK(corner.y < 0);
        const auto none = bubbleMotionOffset(half, {}, {123, -45});
        CHECK(none.x == 0 && none.y == 0);
        // Capillary modes move the outline, not the centre.
        BubbleDeformation wave;
        wave.waveCos[0] = 3;
        const auto centre = bubbleMotionOffset(half, wave, {0, 0});
        const auto edge = bubbleMotionOffset(half, wave, {half.x, 0});
        CHECK(centre.x == 0 && centre.y == 0);
        CHECK(std::abs(edge.x - 3) < 1e-9);
    }

    void microbubblesFormMigrateAndDetach() {
        Bubble b;
        b.seedMotion(11);
        double t = 0;
        std::size_t most = 0, releases = 0, trail = 0;
        bool grew = false, rodeOut = false, trailedBehind = true;
        travel(
            b,
            t,
            4,
            [](double s) { return Vec2{1500 * std::cos(s * 1.6), 500 * std::sin(s * 2.3)}; },
            {.65, 1, 1},
            [&](const Bubble &x) {
                const auto f = x.frame(SIZE);
                most = std::max(most, f.microCount);
                for (std::size_t i = 0; i < f.microCount; ++i) {
                    const auto &m = f.micro[i];
                    grew |= m.growth > 0 && m.growth < 1;
                    // A formed bubble sits outside the film it grew from, joined to it by its neck.
                    if (m.growth >= 1 && m.collapse == 0)
                        rodeOut |=
                            std::abs(m.centre.x) > BODY.w / 2 || std::abs(m.centre.y) > BODY.h / 2;
                    CHECK(m.radius > 0 && m.radius < 20 && m.attach > 0);
                }
                for (const auto &r : x.motion().releases()) {
                    ++releases;
                    CHECK(r.micro.attach == 0 && r.life > 0);
                    // Released bubbles inherit the window's motion but fall behind it.
                    const double speed = x.motion().speed();
                    CHECK(std::hypot(r.velocity.x, r.velocity.y) < speed * 1.2 + 80);
                }
                for (const auto &p : x.motion().trail()) {
                    ++trail;
                    CHECK(p.density > 0 && p.density <= 1 && p.halfWidth > 0);
                    // The film comes off the trailing side.
                    trailedBehind &=
                        p.position.x * p.direction.x + p.position.y * p.direction.y < 0;
                }
            });
        CHECK(most > 0 && most <= kBubbleMicroCapacity);
        CHECK(grew && rodeOut);
        CHECK(releases > 0);
        CHECK(trail > 20 && trailedBehind);
        // Without movement nothing new forms once the window has come to a stop, and every bubble
        // ends.
        travel(b, t, .3, still, {.65, 1, 1});
        travel(b, t, 4, still, {.65, 1, 1}, [&](const Bubble &x) {
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
            [](double) { return Vec2{90, 0}; },
            PARAMS,
            [&](const Bubble &x) {
                points += x.motion().trail().size() + x.motion().releases().size();
            });
        CHECK(points == 0);
    }

    void closeInheritsTheDeformation() {
        Bubble b;
        double t = 0;
        travel(b, t, .3, [](double) { return Vec2{1400, 0}; });
        const auto before = b.frame(SIZE);
        CHECK(before.window.motion.drag.x < 0);
        b.begin(Transition::Close, .42, 9, SIZE);
        const auto after = b.frame(SIZE);
        CHECK(after.window.motion.drag.x == before.window.motion.drag.x);
        CHECK(after.window.core.x == before.window.core.x);
        CHECK(after.microCount == before.microCount);
        b.advance(.42 * .55);
        const auto relaxed = b.frame(SIZE);
        CHECK(relaxed.window.motion.drag.x == 0 && relaxed.window.motion.sheen == 0);
        const auto bounds = b.bounds(SIZE);
        CHECK(bounds.w > 0);
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
        const auto landed = b.frame(SIZE);
        // Movement adds to the settling core instead of replacing it.
        travel(b, t, .1, [](double) { return Vec2{1200, 0}; });
        const auto moving = b.frame(SIZE);
        CHECK(moving.window.motion.drag.x < 0);
        CHECK(moving.window.halfSize.x > 0 && landed.window.thickness == moving.window.thickness);
        travel(b, t, 3, still);
        CHECK(!b.active() && !b.stirred());
        const auto rest = b.frame(SIZE);
        CHECK(rest.window.motion.still() && rest.window.core.x == 0 && rest.window.film == 0);
    }

    void wakeDrainsAndEnds() {
        BubbleWake wake(7);
        CHECK(wake.done() && wake.bounds().w == 0);
        for (int i = 0; i < 20; ++i)
            CHECK(wake.extend({{-450, 0}, {1, 0}, 80, .8, i == 0}, {100.0 + i * 20, 300}));
        BubbleRelease r;
        r.micro.radius = 5;
        r.micro.thickness = 500;
        r.velocity = {600, 0};
        r.life = .4;
        CHECK(wake.release(r, {500, 300}));
        const auto first = wake.bounds();
        CHECK(first.w > 380 && first.h >= 160);
        BubbleWakeView view;
        wake.view(view);
        CHECK(view.nodeCount == 20 && view.bubbleCount == 1);
        CHECK(std::abs(view.size.x - first.w) < 1e-9);
        CHECK(view.nodes[19].arc > view.nodes[0].arc);
        // A free bubble drifts on a little, then slows.
        double moved = 0;
        for (int i = 0; i < 12; ++i) {
            BubbleWakeView a, b;
            wake.view(a);
            const double x0 = a.bubbles[0].centre.x;
            wake.advance(DT);
            wake.view(b);
            if (b.bubbleCount) moved = b.bubbles[0].centre.x - x0;
        }
        CHECK(moved > 0 && moved < 600 * DT);
        // Film thins and goes, oldest first and never all at once, and the wake ends.
        BubbleWakeView mid;
        for (int i = 0; i < 30; ++i) wake.advance(DT);
        wake.view(mid);
        CHECK(mid.nodeCount > 0 && mid.nodes[0].fade > 0);
        for (int i = 0; i < 300 && !wake.done(); ++i) wake.advance(DT);
        CHECK(wake.done() && wake.bounds().w == 0);
        // A wake holds a bounded ribbon.
        BubbleWake full(1);
        std::size_t accepted = 0;
        for (int i = 0; i < 100; ++i)
            accepted += full.extend({{0, 0}, {1, 0}, 40, 1, false}, {double(i), 0});
        CHECK(accepted == kBubbleWakeNodes && full.full());
    }

    void deterministicMotion() {
        Bubble a, b;
        a.seedMotion(42);
        b.seedMotion(42);
        double ta = 0, tb = 0;
        const auto v = [](double s) { return Vec2{1300 * std::cos(s * 2), 400}; };
        travel(a, ta, 1.5, v, {.65, 1, 1});
        travel(b, tb, 1.5, v, {.65, 1, 1});
        const auto x = a.frame(SIZE), y = b.frame(SIZE);
        CHECK(x.window.motion.drag.x == y.window.motion.drag.x && x.microCount == y.microCount);
        a.drive({std::nan(""), 0});
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
    return g_failures == 0 ? 0 : 1;
}
