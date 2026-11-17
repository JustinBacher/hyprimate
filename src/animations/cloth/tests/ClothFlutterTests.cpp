#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "Check.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {
    using namespace hyprimate;
    constexpr Box WINDOW{.x = 100, .y = 50, .w = 1000, .h = 700};
    constexpr Box SCREEN{.x = -300, .y = -200, .w = 1920, .h = 1200};
    constexpr int GRID = PAPER_CELLS + 1;
    ClothParams params() {
        return {.duration = 0.8, .flutter = 0.5, .shading = 0.7, .variation = 0.5};
    }
    struct Pose {
        SheetGrid p{}, v{};
    };
    Pose pose(const ClothFlutter &m) {
        Pose p;
        m.sampleVertices(p.p, p.v);
        const auto finite = [](const Vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        CHECK(std::all_of(p.p.begin(), p.p.end(), finite));
        CHECK(std::all_of(p.v.begin(), p.v.end(), finite));
        return p;
    }
    double delta(const SheetGrid &a, const SheetGrid &b) {
        double d = 0;
        for (std::size_t k = 0; k < a.size(); ++k) {
            d = std::max({d,
                std::abs(a[k].x - b[k].x),
                std::abs(a[k].y - b[k].y),
                std::abs(a[k].z - b[k].z)});
        }
        return d;
    }
    void advance(ClothFlutter &m, double seconds, double step = 1.0 / 144) {
        while (seconds > 1e-12 && m.active()) {
            const double dt = std::min(step, seconds);
            m.advance(dt);
            seconds -= dt;
        }
    }
    ClothFlutter open(ClothParams p = params(), std::uint64_t seed = 7) {
        ClothFlutter m;
        m.begin(Transition::Open, p, seed, WINDOW, {});
        CHECK(m.waitForRecording());
        return m;
    }
    ClothFlutter close(Edge edge, ClothParams p = params()) {
        ClothFlutter m;
        m.begin(Transition::Close,
            p,
            3,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = edge, .screen = SCREEN});
        CHECK(m.waitForRecording());
        return m;
    }
    void needsPhysics() {
        ClothFlutter m;
        m.begin(Transition::Open, params(), 1, WINDOW, {});
        CHECK(!m.active());
    }
    void openingSettlesFlat() {
        auto p = params();
        p.flutter = 0;
        auto m = open(p);
        CHECK(m.opacity() == 0);
        advance(m, .85);
        CHECK(!m.active() && !m.closed() && m.opacity() == 1);
        hyprimate::SheetFrame frame;
        m.frame({.x = WINDOW.w, .y = WINDOW.h}, frame);
        for (std::size_t v = 0; v < hyprimate::PAPER_GRID_VERTICES; ++v) {
            const auto uv = hyprimate::sheetUv(v);
            CHECK(frame.position[v].x == uv.x - 0.5 && frame.position[v].y == uv.y - 0.5);
        }
        for (const auto &shade : frame.shade) {
            for (const float s : shade) {
                CHECK(s == 0.0F);
            }
        }
    }
    // Gathering a curtain makes folds by spending its width in depth, not by stretching or
    // shrinking its material.
    void curtainDrape() {
        auto p = params();
        p.flutter = p.variation = 0;
        const auto m = open(p);
        const auto start = pose(m);
        double contour = 0;
        double near = 1e9;
        double far = -1e9;
        const int row = PAPER_CELLS / 2;
        for (int i = 0; i < GRID; ++i) {
            const auto &v = start.p[(std::size_t(row) * GRID) + i];
            near = std::min(near, v.z);
            far = std::max(far, v.z);
            if (i) {
                const auto &a = start.p[(std::size_t(row) * GRID) + i - 1];
                contour += std::hypot(v.x - a.x, v.y - a.y, v.z - a.z);
            }
        }
        CHECK(far - near > .03);
        CHECK(
            start.p[std::size_t(row) * GRID + PAPER_CELLS].x - start.p[std::size_t(row) * GRID].x <
            .95);
        CHECK(std::abs(contour - 1) < .01);
        // It hangs predominantly downward, rather than presenting one flat horizontal flap.
        CHECK(start.p[PAPER_CELLS / 2].y -
                  start.p[std::size_t(PAPER_CELLS) * GRID + PAPER_CELLS / 2].y >
              .65);
    }

    // Folds must spread and settle without a one-frame jolt at the free edge or weighted hem.
    void softLanding() {
        const double L = std::sqrt(WINDOW.w * WINDOW.h);
        for (const double flutter : {0.0, 0.5}) {
            auto p = params();
            p.flutter = flutter;
            auto m = open(p);
            std::vector<SheetGrid> frames;
            while (m.active()) {
                m.advance(1.0 / 120);
                frames.push_back(pose(m).p);
            }
            double worst = 0;
            for (std::size_t f = 2; f < frames.size(); ++f) {
                for (std::size_t k = 0; k < frames[f].size(); ++k) {
                    const Vec3 a = frames[f - 2][k];
                    const Vec3 b = frames[f - 1][k];
                    const Vec3 c = frames[f][k];
                    worst = std::max(worst,
                        std::hypot((c.x - (2 * b.x) + a.x) * WINDOW.w,
                            (c.y - (2 * b.y) + a.y) * WINDOW.h,
                            (c.z - (2 * b.z) + a.z) * L));
                }
            }
            // Per-frame velocity change in pixels at 120 Hz; the undamped swing peaked near 60
            // here.
            CHECK(worst < 30);
        }
    }
    void exits() {
        auto p = params();
        p.flutter = 0;
        auto falling = close(Edge::Bottom, p);
        const auto start = pose(falling);
        advance(falling, 0.25);
        const auto drop = pose(falling);
        double averageYVelocity = 0;
        for (int i = 0; i < GRID; ++i) {
            CHECK(drop.p[i].y < start.p[i].y - 0.1);
            averageYVelocity += drop.v[i].y;
        }
        CHECK(averageYVelocity < 0);
        CHECK(falling.opacity() == 1);  // visible sheet cannot fade prematurely
        advance(falling, 0.45);
        const auto late = pose(falling);
        for (const auto &v : late.p) {
            CHECK(WINDOW.y + WINDOW.h / 2 + (-v.y * WINDOW.h - WINDOW.h / 2) > SCREEN.y + SCREEN.h);
        }
        advance(falling, 1);
        CHECK(falling.closed() && falling.opacity() == 0);
        for (const Edge edge : {Edge::Top, Edge::Left, Edge::Right}) {
            auto pulling = close(edge, p);
            const auto initial = pose(pulling);
            advance(pulling, 0.4);
            const auto mid = pose(pulling);
            for (int i = 0; i < GRID; ++i) {
                const std::size_t k = i;
                if (edge == Edge::Top) {
                    CHECK(mid.p[k].y > initial.p[k].y + 0.5);
                }
                if (edge == Edge::Left) {
                    CHECK(mid.p[k].x < initial.p[k].x - 0.5);
                }
                if (edge == Edge::Right) {
                    CHECK(mid.p[k].x > initial.p[k].x + 0.5);
                }
            }
            advance(pulling, 0.35);
            const auto end = pose(pulling);
            for (const auto &v : end.p) {
                if (edge == Edge::Top) {
                    CHECK(WINDOW.y - v.y * WINDOW.h < SCREEN.y);
                }
                if (edge == Edge::Left) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w < SCREEN.x);
                }
                if (edge == Edge::Right) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w > SCREEN.x + SCREEN.w);
                }
            }
            advance(pulling, 1);
            CHECK(pulling.closed());
        }
    }
    void exitCorridor() {
        const Box window{100, 50, 1000, 400};
        const Box screen{0, 0, 1920, 1080};
        const Box below{100, 460, 1000, 500};
        const Box right{1110, 50, 600, 400};
        auto p = params();
        p.flutter = p.variation = 0;
        p.duration = .9;
        for (const auto edge : {Edge::Bottom, Edge::Right}) {
            const Box obstacle = edge == Edge::Bottom ? below : right;
            ClothFlutter clear;
            ClothFlutter blocked;
            clear.begin(Transition::Close,
                p,
                1,
                window,
                {},
                ClothCloseGeometry{.edge = edge, .screen = screen});
            blocked.begin(Transition::Close,
                p,
                1,
                window,
                std::span<const Box>(&obstacle, 1),
                ClothCloseGeometry{.edge = edge, .screen = screen});
            CHECK(clear.waitForRecording());
            CHECK(blocked.waitForRecording());
            advance(clear, .7);
            advance(blocked, .7);
            CHECK(delta(pose(clear).p, pose(blocked).p) == 0);
            const auto final = pose(blocked);
            for (const auto &v : final.p) {
                CHECK(std::abs(v.x) < 20 && std::abs(v.y) < 20 && std::abs(v.z) < 20);
                if (edge == Edge::Bottom) {
                    CHECK(window.y - v.y * window.h > screen.y + screen.h);
                } else {
                    CHECK(window.x + window.w / 2 + v.x * window.w > screen.x + screen.w);
                }
            }
        }
    }
    void handoff() {
        for (const double when : {0.015, 0.25, 0.73}) {
            auto m = open();
            advance(m, when);
            const auto before = pose(m);
            const double alpha = m.opacity();
            auto p = params();
            p.duration = 1.6;
            m.begin(Transition::Close,
                p,
                12,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
            const auto after = pose(m);
            CHECK(delta(before.p, after.p) < 1e-12);
            CHECK(delta(before.v, after.v) < 1e-12);
            CHECK(m.opacity() == alpha);
            CHECK(m.waitForRecording());
            const auto ready = pose(m);
            CHECK(delta(before.p, ready.p) < 2e-6);
            CHECK(delta(before.v, ready.v) < 2e-5);
        }
    }
    void extremeDurationHandoff() {
        auto p = params();
        p.duration = .1;
        p.flutter = 1;
        p.variation = 0;
        auto m = open(p);
        advance(m, .03, .001);
        const auto before = pose(m);
        const double alpha = m.opacity();
        p.duration = 3.430429;
        m.begin(Transition::Close,
            p,
            2,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
        CHECK(m.waitForRecording());
        const auto after = pose(m);
        CHECK(delta(before.p, after.p) < 2e-6);
        CHECK(delta(before.v, after.v) < 2e-4);
        CHECK(m.opacity() == alpha);
    }
    void cadence() {
        const auto source = open();
        auto reference = source;
        advance(reference, 0.63, 0.001);
        const auto expected = pose(reference);
        for (const double hz : {60., 120., 144., 240.}) {
            auto m = source;
            advance(m, 0.63, 1 / hz);
            CHECK(delta(expected.p, pose(m).p) < 1e-10);
        }
        auto irregular = source;
        double time = 0;
        const double steps[] = {.003, .027, .011, .007, .045, .016};
        int i = 0;
        while (time < .63 - 1e-12) {
            const double dt = std::min(steps[i++ % 6], .63 - time);
            irregular.advance(dt);
            time += dt;
        }
        CHECK(delta(expected.p, pose(irregular).p) < 1e-10);
    }
    void deterministic() {
        auto p = params();
        p.variation = 0;
        auto a = open(p, 5);
        auto b = open(p, 900);
        advance(a, .43);
        advance(b, .43);
        CHECK(delta(pose(a).p, pose(b).p) == 0);
    }
    void invalid() {
        auto p = params();
        p.duration = std::numeric_limits<double>::quiet_NaN();
        ClothFlutter m;
        m.begin(Transition::Open, p, 1, WINDOW, {});
        CHECK(!m.active());
        m.begin(Transition::Open, params(), 1, Box{.x = 0, .y = 0, .w = 0, .h = 500}, {});
        CHECK(!m.active());
        m.begin(Transition::Open,
            params(),
            1,
            Box{.x = 0, .y = 0, .w = std::numeric_limits<double>::max(), .h = 500},
            {});
        CHECK(!m.active());
        m.begin(static_cast<Transition>(99), params(), 1, WINDOW, {});
        CHECK(!m.active());
        m.begin(Transition::Close,
            params(),
            1,
            WINDOW,
            {},
            ClothCloseGeometry{
                .edge = Edge::Left, .screen = Box{.x = 0, .y = 0, .w = -1, .h = 500}});
        CHECK(!m.active());
        m = open();
        m.advance(-1);
        CHECK(!m.active() && !m.closed());
        m = close(Edge::Top);
        m.advance(std::numeric_limits<double>::infinity());
        CHECK(m.closed());
    }
    void starvation() {
        ClothPhysics::pauseWorkersForTesting(true);
        ClothFlutter opening;
        opening.begin(Transition::Open, params(), 1, WINDOW, {});
        advance(opening, 0.55);
        CHECK(!opening.active() && opening.opacity() == 1);
        ClothFlutter closing;
        closing.begin(Transition::Close,
            params(),
            1,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Bottom, .screen = SCREEN});
        advance(closing, 0.55);
        CHECK(closing.closed() && closing.opacity() == 0);
        ClothPhysics::pauseWorkersForTesting(false);
        // Exhaust a partial recording by pausing publication after the worker has started.
        ClothPhysics::pauseWorkersForTesting(true, 2);
        ClothFlutter partial;
        partial.begin(Transition::Open, params(), 7, WINDOW, {});
        CHECK(partial.waitForSnapshots(2));
        advance(partial, 1);
        CHECK(!partial.active());
        ClothPhysics::pauseWorkersForTesting(false);
    }
    // A recoverable publication gap must retain the same recording and resume it.
    void publicationRecovery() {
        constexpr double dt = 1.0 / 120;
        for (const auto kind : {Transition::Open, Transition::Close}) {
            ClothPhysics::pauseWorkersForTesting(true, 4);
            ClothFlutter m;
            m.begin(kind,
                params(),
                7,
                WINDOW,
                {},
                kind == Transition::Close ? std::optional<ClothCloseGeometry>{ClothCloseGeometry{
                                                .edge = Edge::Bottom, .screen = SCREEN}}
                                          : std::optional<ClothCloseGeometry>{});
            CHECK(m.waitForSnapshots(4));
            const ClothFlutter witness = m;
            advance(m, .1, dt);
            const auto held = pose(m);
            const double alpha = m.opacity();
            advance(m, .1, dt);
            CHECK(m.active());
            CHECK(delta(pose(m).p, held.p) < 1e-9);
            CHECK(std::abs(m.opacity() - alpha) < 1e-9);
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(witness.waitForRecording());
            const auto before = pose(m);
            advance(m, .1, dt);
            CHECK(delta(pose(m).p, before.p) > 1e-9);
            advance(m, 2, dt);
            CHECK(kind == Transition::Open ? !m.active() && !m.closed() : m.closed());
        }
    }

    void movementPublicationRecovery() {
        // A gap shorter than the stall bound must recover even when the compositor posts targets
        // faster than Jolt.
        for (const int refreshRate : {120, 360, 1000}) {
            const double dt = 1.0 / refreshRate;
            ClothFlutter m;
            m.beginMovement({}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            const ClothFlutter witness = m;
            ClothPhysics::pauseWorkersForTesting(true, 2);
            Box box = WINDOW;
            for (int i = 0; i < refreshRate / 5; ++i) {
                box.x += 1;
                m.updateMovement(box, dt);
                m.advance(dt);
            }
            CHECK(m.moving());
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(witness.waitForSnapshots(26, 2));
            m.advance(0);
            CHECK(m.moving());
            CHECK(delta(pose(m).p, pose(witness).p) > 1e-7);
        }
    }

    void clothShine() {
        ClothFlutter m;
        auto p = params();
        p.shine = std::numeric_limits<double>::quiet_NaN();
        m.begin(Transition::Open, p, 1, WINDOW, {});
        CHECK(!m.active());
        ClothMovementParams movement;
        movement.shine = std::numeric_limits<double>::infinity();
        m.beginMovement(movement, 1, WINDOW);
        CHECK(!m.active());

        auto source = open();
        advance(source, .25);
        auto dark = source;
        auto bright = source;
        movement.shine = 0;
        dark.beginMovement(movement, 1, WINDOW);
        movement.shine = 1;
        bright.beginMovement(movement, 1, WINDOW);
        SheetFrame a;
        SheetFrame b;
        dark.frame({.x = WINDOW.w, .y = WINDOW.h}, a);
        bright.frame({.x = WINDOW.w, .y = WINDOW.h}, b);
        bool highlight = false;
        for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
            for (int c = 0; c < 3; ++c) {
                CHECK(a.shade[t][c] <= 0);
                if (b.shade[t][c] > 0) {
                    highlight = true;
                }
            }
        }
        CHECK(highlight);
    }

    void cancelAndUnload() {
        ClothPhysics::pauseWorkersForTesting(true);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) {
            ClothFlutter m;
            m.begin(Transition::Open, params(), i, WINDOW, {});
            m.reset();
        }
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
        ClothPhysics::pauseWorkersForTesting(false);
    }

    void movementStep(ClothFlutter &m, const Box &box, double dt, double &time) {
        m.updateMovement(box, dt);
        m.advance(dt);
        time += dt;
        if (m.moving()) {
            CHECK(m.waitForSnapshots(2 + int(std::floor(time * 120 + 1e-7))));
            m.advance(0);
        }
    }

    // A curtain's rod is its entire top edge, irrespective of where it travels or where a drag
    // began.
    void pocketHeaderStaysHorizontal() {
        auto m = open();
        advance(m, .2);
        const auto horizontal = [](const ClothFlutter &sheet) {
            SheetFrame frame;
            sheet.frame({.x = WINDOW.w, .y = WINDOW.h}, frame);
            double low = frame.position[0].y;
            double high = low;
            for (int i = 1; i < GRID; ++i) {
                low = std::min(low, frame.position[i].y);
                high = std::max(high, frame.position[i].y);
            }
            return high - low < 1e-6;
        };
        CHECK(horizontal(m));
        m.beginMovement({}, 7, WINDOW);
        CHECK(horizontal(m));
        CHECK(m.waitForSnapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int k = 0; k < 24; ++k) {
            box.x += 4;
            movementStep(m, box, 1.0 / 120, time);
            CHECK(horizontal(m));
        }
    }

    // The displayed sleeve must stay in the real window bounds, including reversal, resize and
    // worker recovery.
    void pocketStaysWithinWindowBounds() {
        constexpr double dt = 1.0 / 120;
        for (const double springiness : {0., .5, 1.}) {
            for (const double direction : {-1., 1.}) {
                auto material = params();
                material.flutter = material.variation = 0;
                auto m = open(material);
                m.beginMovement({.springiness = springiness}, 7, WINDOW);
                CHECK(m.waitForSnapshots(2));
                Box box = WINDOW;
                double time = 0;
                double overhang = 0;
                bool ordered = true;
                bool level = true;
                const auto check = [&] {
                    const auto sampled = pose(m);
                    SheetFrame frame;
                    m.frame({.x = box.w, .y = box.h}, frame);
                    for (int i = 0; i < GRID; ++i) {
                        overhang = std::max(overhang, (std::abs(frame.position[i].x) - .5) * box.w);
                        ordered &= i == 0 || sampled.p[i].x >= sampled.p[i - 1].x;
                        level &= std::abs(sampled.p[i].y) < 1e-6 && std::abs(sampled.p[i].z) < 1e-6;
                    }
                };
                check();
                for (int k = 0; k < 64; ++k) {
                    box.x += (k < 32 ? direction : -direction) * 12;
                    if (k == 32) {
                        box.w *= .7;
                        box.h *= 1.1;
                    }
                    movementStep(m, box, dt, time);
                    check();
                }
                ClothPhysics::pauseWorkersForTesting(true);
                for (int k = 0; k < 72; ++k) {
                    box.x -= direction * 12;
                    m.updateMovement(box, dt);
                    m.advance(dt);
                    check();
                }
                ClothPhysics::pauseWorkersForTesting(false);
                CHECK(overhang <= 1e-6);
                CHECK(ordered);
                CHECK(level);
            }
        }
    }

    // A gently travelling gathered sleeve spreads faster at the end opposite its travel direction.
    void pocketSpringIsStifferBehindTravel() {
        for (const double direction : {-1., 1.}) {
            auto material = params();
            material.flutter = material.variation = 0;
            auto m = open(material);
            m.beginMovement({.springiness = 1}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            const auto start = pose(m);
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 24; ++k) {
                box.x += direction * .25;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto spread = pose(m);
            const int trailing = direction > 0 ? 0 : PAPER_CELLS;
            const int leading = direction > 0 ? PAPER_CELLS : 0;
            const double trailingSpread = -direction * (spread.p[trailing].x - start.p[trailing].x);
            const double leadingSpread = direction * (spread.p[leading].x - start.p[leading].x);
            CHECK(leadingSpread > .005);
            CHECK(trailingSpread > leadingSpread * 1.35);
        }
    }

    void pocketSpringinessControl() {
        std::array<double, 3> peaks{};
        int mode = 0;
        for (const double springiness : {0., .25, 1.}) {
            ClothMovementParams material;
            if (springiness > 0) {
                material.springiness = springiness;
            }
            ClothFlutter m;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            double lowerDrift = 0;
            for (int k = 0; k < 48; ++k) {
                box.x += k < 24 ? 5 : -5;
                movementStep(m, box, 1.0 / 120, time);
                const auto moved = pose(m);
                for (int i = 0; i < GRID; ++i) {
                    const double rest = (double(i) / PAPER_CELLS) - .5;
                    peaks[mode] = std::max(peaks[mode], std::abs(moved.p[i].x - rest) * box.w);
                    lowerDrift = std::max(lowerDrift,
                        std::abs(moved.p[(std::size_t(PAPER_CELLS) * GRID) + i].x - rest) * box.w);
                }
            }
            CHECK(lowerDrift > 1);
            ++mode;
        }
        CHECK(peaks[0] < .001);
        CHECK(peaks[1] > .25);
        CHECK(peaks[2] > peaks[1] * 1.5);
        ClothFlutter invalid;
        invalid.beginMovement({.springiness = std::numeric_limits<double>::quiet_NaN()}, 7, WINDOW);
        CHECK(!invalid.active());
    }

    void pocketSlidesInward() {
        for (const double direction : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter m;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 30; ++k) {
                box.x += direction * 5;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto moved = pose(m);
            bool onRod = true;
            for (int i = 0; i < GRID; ++i) {
                onRod &= moved.p[i].x >= -.50001 && moved.p[i].x <= .50001 &&
                         (i == 0 || moved.p[i].x >= moved.p[i - 1].x);
            }
            CHECK(onRod);
            CHECK(direction > 0 ? moved.p[PAPER_CELLS].x < .495 : moved.p[0].x > -.495);
        }
    }

    // The lengthwise spring must restore the pocket even while the window keeps travelling.
    void pocketSpringRestoresDuringTravel() {
        for (const double direction : {-1., 1.}) {
            ClothFlutter m;
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            double peak = 0;
            double offset = 0;
            for (int frame = 0; frame < 120; ++frame) {
                box.x += direction * 5;
                movementStep(m, box, 1.0 / 120, time);
                const auto moved = pose(m);
                offset = 0;
                for (int i = 0; i < GRID; ++i) {
                    offset += moved.p[i].x - ((double(i) / PAPER_CELLS) - .5);
                }
                offset /= GRID;
                peak = std::max(peak, std::abs(offset));
            }
            CHECK(m.moving());
            CHECK(peak > .005);
            CHECK(std::abs(offset) < peak * .5);
        }
    }

    // Direction changes and capture resizing must preserve the bounded horizontal sleeve.
    void pocketSpringReversalAndResize() {
        for (const double direction : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter m;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            const auto step = [&](double dx) {
                box.x += dx;
                movementStep(m, box, 1.0 / 120, time);
            };
            const auto center = [&] {
                const auto p = pose(m);
                double sum = 0;
                for (int i = 0; i < GRID; ++i) {
                    sum += p.p[i].x;
                }
                return sum / GRID;
            };
            const double start = center();
            for (int i = 0; i < 8; ++i) {
                step(direction * 5);
            }
            const double early = center();
            for (int i = 0; i < 40; ++i) {
                step(direction * 5);
            }
            const double later = center();
            CHECK(direction * (early - start) < -1e-4);

            for (int i = 0; i < 30; ++i) {
                step(-direction * 5);
            }
            const auto reversed = pose(m);
            double reversedCenter = 0;
            bool onRod = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &p = reversed.p[i];
                reversedCenter += p.x;
                onRod &= p.x >= -.50001 && p.x <= .50001 && (i == 0 || p.x >= reversed.p[i - 1].x);
            }
            reversedCenter /= GRID;
            CHECK(onRod);
            CHECK(direction * (reversedCenter - later) > 1e-4);

            box.w *= .9;
            box.h *= 1.1;
            movementStep(m, box, 1.0 / 120, time);
            const auto resized = pose(m);
            for (int i = 0; i < GRID; ++i) {
                CHECK(resized.p[i].x >= -.50001 && resized.p[i].x <= .50001);
                CHECK(i == 0 || resized.p[i].x >= resized.p[i - 1].x);
                CHECK(std::abs(resized.p[i].y) < 1e-5 && std::abs(resized.p[i].z) < 1e-5);
            }
        }
    }

    void curtainRodMovement() {
        for (const Vec2 direction : {Vec2{.x = 1, .y = 0},
                 Vec2{.x = -1, .y = 0},
                 Vec2{.x = 0, .y = 1},
                 Vec2{.x = 0, .y = -1}}) {
            ClothFlutter m;
            ClothMovementParams material;
            material.variation = 0;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 24; ++frame) {
                box.x += 4 * direction.x;
                box.y += 4 * direction.y;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto moved = pose(m);
            bool topAttached = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &p = moved.p[i];
                topAttached &= p.x >= -.50001 && p.x <= .50001 &&
                               (i == 0 || p.x >= moved.p[i - 1].x) && std::abs(p.y) < 1e-5 &&
                               std::abs(p.z) < 1e-5;
            }
            CHECK(topAttached);
            if (direction.x != 0) {
                const auto motionAt = [&](int row) {
                    double sum = 0;
                    for (int i = 0; i < GRID; ++i) {
                        const auto &p = moved.p[(std::size_t(row) * GRID) + i];
                        sum += std::pow((p.x - (double(i) / PAPER_CELLS) + .5) * box.w, 2) +
                               std::pow((p.y + (double(row) / PAPER_CELLS)) * box.h, 2) +
                               (p.z * p.z * box.w * box.h);
                    }
                    return std::sqrt(sum / GRID);
                };
                CHECK(motionAt(PAPER_CELLS) > motionAt(PAPER_CELLS / 4) + 1);
            }
        }
    }

    void curtainRodClose() {
        for (const Edge edge : {Edge::Top, Edge::Bottom, Edge::Left, Edge::Right}) {
            auto m = close(edge);
            advance(m, .2);
            const auto moved = pose(m);
            bool straightRod = true;
            for (int i = 1; i < GRID; ++i) {
                straightRod &= moved.p[i].x >= moved.p[i - 1].x &&
                               moved.p[i].x - moved.p[0].x <= 1.00001 &&
                               std::abs(moved.p[i].y - moved.p[0].y) < 1e-5 &&
                               std::abs(moved.p[i].z - moved.p[0].z) < 1e-5;
            }
            CHECK(straightRod);
        }
    }

    void movement() {
        ClothMovementParams params;
        params.flutter = 0;
        params.variation = 0;
        params.springiness = 1;
        ClothFlutter m;
        m.beginMovement(params, 1, WINDOW);
        CHECK(m.moving() && m.waitForSnapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int i = 0; i < 24; ++i) {
            box.x += 4;
            movementStep(m, box, 1.0 / 120, time);
        }
        auto moved = pose(m);
        CHECK(
            std::any_of(moved.p.begin(), moved.p.end(), [](const Vec3 &v) { return v.z > .001; }));
        for (int i = 0; i < 12; ++i) {
            box.x -= 4;
            movementStep(m, box, 1.0 / 120, time);
        }
        moved = pose(m);
        // A retained copy freezes the displayed pose; closing consumes it without sharing mutable
        // playback state.
        ClothFlutter closing = m;
        const double alpha = m.opacity();
        closing.begin(Transition::Close,
            ::params(),
            4,
            box,
            {},
            ClothCloseGeometry{.edge = Edge::Right, .screen = SCREEN});
        CHECK(closing.waitForSnapshots(2));
        CHECK(delta(pose(closing).p, moved.p) < 1e-6);
        CHECK(delta(pose(closing).v, moved.v) < 1e-5);
        CHECK(std::abs(closing.opacity() - alpha) < 1e-8);
        box.w *= .9;
        box.h *= 1.1;
        movementStep(m, box, 1.0 / 120, time);
        for (int i = 0; i < 55 && m.moving(); ++i) {
            movementStep(m, box, 1.0 / 120, time);
        }
        advance(m, .25);
        CHECK(!m.active() && m.opacity() == 1);
        hyprimate::SheetFrame frame;
        m.frame({.x = box.w, .y = box.h}, frame);
        for (std::size_t v = 0; v < hyprimate::PAPER_GRID_VERTICES; ++v) {
            const auto uv = hyprimate::sheetUv(v);
            CHECK(frame.position[v].x == uv.x - .5 && frame.position[v].y == uv.y - .5);
        }
    }

    void movementHandoff() {
        auto m = open();
        advance(m, .3);
        const auto before = pose(m);
        const double alpha = m.opacity();
        m.beginMovement({}, 2, WINDOW);
        CHECK(delta(pose(m).p, before.p) < 1e-6);
        CHECK(delta(pose(m).v, before.v) < 1e-6);
        CHECK(std::abs(m.opacity() - alpha) < 1e-8);
        CHECK(m.waitForSnapshots(2));
        Box box = WINDOW;
        box.x += 10;
        double time = 0;
        movementStep(m, box, 1.0 / 120, time);
        CHECK(m.moving());
        // Bounded starvation flattens, and a new move during that flattening continues from its
        // visible pose.
        ClothPhysics::pauseWorkersForTesting(true, 2);
        advance(m, .12);
        const auto fallback = pose(m);
        box.y += 20;
        m.updateMovement(box, .01);
        CHECK(m.moving());
        CHECK(delta(pose(m).p, fallback.p) < 1e-6);
        ClothPhysics::pauseWorkersForTesting(false);
        m.reset();
    }

    Pose movementTrajectory(double cadence, std::uint64_t seed, bool irregular = false) {
        ClothMovementParams params;
        params.variation = 0;
        ClothFlutter m;
        m.beginMovement(params, seed, WINDOW);
        CHECK(m.waitForSnapshots(2));
        double time = 0;
        int frame = 0;
        while (time < .2 - 1e-9) {
            const double dt =
                std::min(cadence * (irregular ? (frame++ % 2 ? 1.6 : .4) : 1), .2 - time);
            Box box = WINDOW;
            box.x += 300 * (time + dt);
            movementStep(m, box, dt, time);
        }
        return pose(m);
    }

    void movementCadenceAndInvalid() {
        const auto reference = movementTrajectory(1.0 / 120, 7);
        for (const double hz : {60., 144., 240.}) {
            const auto sampled = movementTrajectory(1 / hz, 100);
            CHECK(delta(reference.p, sampled.p) < .015);
        }
        const auto same = movementTrajectory(1.0 / 120, 900);
        CHECK(delta(reference.p, same.p) == 0);
        CHECK(delta(reference.p, movementTrajectory(1.0 / 120, 100, true).p) < .015);
        ClothFlutter m;
        auto params = ClothMovementParams{};
        params.strength = 0;
        m.beginMovement(params, 1, WINDOW);
        CHECK(!m.active());
        params.strength = std::numeric_limits<double>::quiet_NaN();
        m.beginMovement(params, 1, WINDOW);
        CHECK(!m.active());
        m.beginMovement({}, 1, WINDOW);
        m.updateMovement(Box{.x = 0, .y = 0, .w = -1, .h = 200}, .01);
        CHECK(!m.active());
        ClothPhysics::pauseWorkersForTesting(true, 2);
        m.beginMovement({}, 1, WINDOW);
        CHECK(m.waitForSnapshots(2));
        advance(m, .5);
        CHECK(!m.active());
        ClothPhysics::pauseWorkersForTesting(false);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            m.beginMovement({}, i, WINDOW);
            m.reset();
        }
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    }
    // Each frame posts a target the worker has not answered yet. The displayed sheet must already
    // be fully simulated, so it cannot depend on whether the worker happened to answer before the
    // frame was drawn.
    void movementIndependentOfWorkerTiming() {
        ClothMovementParams params;
        params.variation = 0;
        constexpr int FRAMES = 30;
        constexpr double FRAME = 1.0 / 60;
        std::vector<SheetGrid> answered;
        {
            ClothFlutter m;
            m.beginMovement(params, 3, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            for (int k = 1; k <= FRAMES; ++k) {
                box.x += 5;
                m.updateMovement(box, FRAME);
                m.advance(FRAME);
                CHECK(m.waitForSnapshots(2 + 2 * k));
                m.advance(0);
                answered.push_back(pose(m).p);
            }
        }
        ClothFlutter m;
        m.beginMovement(params, 3, WINDOW);
        CHECK(m.waitForSnapshots(2));
        Box box = WINDOW;
        double worst = 0;
        for (int k = 1; k <= FRAMES; ++k) {
            // Let the worker answer every earlier target, then hold it before the one this frame
            // posts.
            ClothPhysics::pauseWorkersForTesting(true, 2 + (2 * (k - 1)));
            CHECK(m.waitForSnapshots(2 + 2 * (k - 1)));
            box.x += 5;
            m.updateMovement(box, FRAME);
            m.advance(FRAME);
            worst = std::max(worst, delta(answered[std::size_t(k - 1)], pose(m).p));
        }
        ClothPhysics::pauseWorkersForTesting(false);
        CHECK(worst < 1e-9);
    }

    void movementGravityControl() {
        const auto hemAfterDrag = [](double gravity) {
            ClothFlutter m;
            m.beginMovement({.variation = 0, .gravity = gravity}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 36; ++frame) {
                box.y += 2800.0 / 120;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto moved = pose(m);
            double hem = 0;
            for (int i = 0; i < GRID; ++i) {
                hem += moved.p[(std::size_t(PAPER_CELLS) * GRID) + i].y;
            }
            return hem / GRID;
        };
        const double zero = hemAfterDrag(0);
        const double normal = hemAfterDrag(1);
        const double heavy = hemAfterDrag(3);
        CHECK(zero > normal + .01);
        CHECK(normal > heavy + .01);
        CHECK(hemAfterDrag(-1) == zero);
        CHECK(hemAfterDrag(4) == heavy);
        for (const double invalid :
            {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            ClothFlutter m;
            m.beginMovement({.gravity = invalid}, 7, WINDOW);
            CHECK(!m.active());
        }
    }

    // A quick downward pull leaves the free hem behind instead of dragging a rigid panel.
    void downwardDragLiftsHem() {
        for (const double hz : {60., 120., 240.}) {
            ClothFlutter m;
            m.beginMovement({}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            while (time < .3 - 1e-9) {
                box.y = WINDOW.y + (2800 * (time + (1 / hz)));
                movementStep(m, box, 1 / hz, time);
            }
            const auto lifted = pose(m);
            double hem = 0;
            for (int i = 0; i < GRID; ++i) {
                hem += lifted.p[(std::size_t(PAPER_CELLS) * GRID) + i].y + 1;
            }
            CHECK(hem / GRID > .3);
            for (int i = 0; i < GRID; ++i) {
                CHECK(std::abs(lifted.p[i].y) < 1e-5);
            }
        }
    }

    void movementBoundsAndRestart() {
        ClothFlutter m;
        m.beginMovement({}, 1, WINDOW);
        CHECK(m.waitForSnapshots(2));
        double time = 0;
        Box box = WINDOW;
        box.x += 300;
        movementStep(m, box, 1.0 / 120, time);
        movementStep(m, box, 1.0 / 120, time);
        const auto moved = pose(m);
        const auto anchor = std::size_t(PAPER_CELLS);
        CHECK(moved.p[anchor].x >= -.50001 && moved.p[anchor].x <= .50001 &&
              std::abs(moved.p[anchor].y) < 1e-5);
        for (int i = 0; i < 48; ++i) {
            movementStep(m, box, 1.0 / 120, time);
        }
        m.advance(.12);  // begin the bounded final flattening transition
        const auto settling = pose(m);
        box.y += 10;
        m.updateMovement(box, .01);
        CHECK(m.moving());
        CHECK(delta(pose(m).p, settling.p) < 1e-6);
        CHECK(delta(pose(m).v, settling.v) < 1e-6);
    }

    void movementRodBeforePublication() {
        ClothFlutter m;
        m.beginMovement({}, 1, WINDOW);
        CHECK(m.waitForSnapshots(2));
        ClothPhysics::pauseWorkersForTesting(true, 2);
        Box box = WINDOW;
        box.x += 20;
        m.updateMovement(box, 1.0 / 60);
        m.advance(1.0 / 60);
        const auto onset = pose(m);
        CHECK(std::abs(onset.p[PAPER_CELLS].x - .5) < 1e-7);
        CHECK(std::abs(onset.v[PAPER_CELLS].x - 1.2) < 1e-6);
        ClothPhysics::pauseWorkersForTesting(false);
        m.reset();

        m.beginMovement({}, 1, WINDOW);
        CHECK(m.waitForSnapshots(2));
        box = WINDOW;
        double time = 0;
        for (int i = 0; i < 4; ++i) {
            box.x += 10;
            movementStep(m, box, 1.0 / 120, time);
        }
        ClothPhysics::pauseWorkersForTesting(true, 6);
        m.updateMovement(box, 1.0 / 60);
        // Already-published samples may still advance the sliding pocket; hold only once those are
        // consumed.
        advance(m, .1);
        const auto held = pose(m);
        advance(m, .02);
        CHECK(delta(pose(m).p, held.p) < 1e-9);
        ClothPhysics::pauseWorkersForTesting(false);
    }
}  // namespace
int main() {
    needsPhysics();
    {
        const ClothPhysics physics;
        openingSettlesFlat();
        curtainDrape();
        softLanding();
        exits();
        exitCorridor();
        handoff();
        extremeDurationHandoff();
        cadence();
        deterministic();
        invalid();
        starvation();
        publicationRecovery();
        movementPublicationRecovery();
        clothShine();
        cancelAndUnload();
        pocketHeaderStaysHorizontal();
        pocketSlidesInward();
        pocketStaysWithinWindowBounds();
        pocketSpringIsStifferBehindTravel();
        pocketSpringinessControl();
        pocketSpringReversalAndResize();
        pocketSpringRestoresDuringTravel();
        curtainRodMovement();
        curtainRodClose();
        movement();
        movementHandoff();
        movementCadenceAndInvalid();
        movementIndependentOfWorkerTiming();
        movementBoundsAndRestart();
        movementRodBeforePublication();
        downwardDragLiftsHem();
        movementGravityControl();
    }
    // Shutdown must join a currently paused worker before unregistering Jolt, even with a model
    // reference retained.
    ClothFlutter survivor;
    {
        const ClothPhysics physics;
        ClothPhysics::pauseWorkersForTesting(true);
        survivor.begin(Transition::Open, params(), 1, WINDOW, {});
    }
    survivor.reset();
    if (g_failures) {
        std::fprintf(stderr, "%d cloth checks failed\n", g_failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
}
