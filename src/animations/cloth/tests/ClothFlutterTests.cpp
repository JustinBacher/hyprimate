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
        const auto FINITE = [](const Vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        CHECK(std::all_of(p.p.begin(), p.p.end(), FINITE));
        CHECK(std::all_of(p.v.begin(), p.v.end(), FINITE));
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
        const auto START = pose(m);
        double contour = 0;
        double near = 1e9;
        double far = -1e9;
        const int ROW = PAPER_CELLS / 2;
        for (int i = 0; i < GRID; ++i) {
            const auto &v = START.p[(static_cast<std::size_t>(ROW) * GRID) + i];
            near = std::min(near, v.z);
            far = std::max(far, v.z);
            if (i != 0) {
                const auto &a = START.p[(static_cast<std::size_t>(ROW) * GRID) + i - 1];
                contour += std::hypot(v.x - a.x, v.y - a.y, v.z - a.z);
            }
        }
        CHECK(far - near > .03);
        CHECK(
            START.p[std::size_t(ROW) * GRID + PAPER_CELLS].x - START.p[std::size_t(ROW) * GRID].x <
            .95);
        CHECK(std::abs(contour - 1) < .01);
        // It hangs predominantly downward, rather than presenting one flat horizontal flap.
        CHECK(START.p[PAPER_CELLS / 2].y -
                  START.p[std::size_t(PAPER_CELLS) * GRID + PAPER_CELLS / 2].y >
              .65);
    }

    // Folds must spread and settle without a one-frame jolt at the free edge or weighted hem.
    void softLanding() {
        const double L = std::sqrt(WINDOW.w * WINDOW.h);
        for (const double FLUTTER : {0.0, 0.5}) {
            auto p = params();
            p.flutter = FLUTTER;
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
        const auto START = pose(falling);
        advance(falling, 0.25);
        const auto DROP = pose(falling);
        double averageYVelocity = 0;
        for (int i = 0; i < GRID; ++i) {
            CHECK(DROP.p[i].y < START.p[i].y - 0.1);
            averageYVelocity += DROP.v[i].y;
        }
        CHECK(averageYVelocity < 0);
        CHECK(falling.opacity() == 1);  // visible sheet cannot fade prematurely
        advance(falling, 0.45);
        const auto LATE = pose(falling);
        for (const auto &v : LATE.p) {
            CHECK(WINDOW.y + WINDOW.h / 2 + (-v.y * WINDOW.h - WINDOW.h / 2) > SCREEN.y + SCREEN.h);
        }
        advance(falling, 1);
        CHECK(falling.closed() && falling.opacity() == 0);
        for (const Edge EDGE : {Edge::Top, Edge::Left, Edge::Right}) {
            auto pulling = close(EDGE, p);
            const auto INITIAL = pose(pulling);
            advance(pulling, 0.4);
            const auto MID = pose(pulling);
            for (int i = 0; i < GRID; ++i) {
                const std::size_t k = i;
                if (EDGE == Edge::Top) {
                    CHECK(MID.p[k].y > INITIAL.p[k].y + 0.5);
                }
                if (EDGE == Edge::Left) {
                    CHECK(MID.p[k].x < INITIAL.p[k].x - 0.5);
                }
                if (EDGE == Edge::Right) {
                    CHECK(MID.p[k].x > INITIAL.p[k].x + 0.5);
                }
            }
            advance(pulling, 0.35);
            const auto END = pose(pulling);
            for (const auto &v : END.p) {
                if (EDGE == Edge::Top) {
                    CHECK(WINDOW.y - v.y * WINDOW.h < SCREEN.y);
                }
                if (EDGE == Edge::Left) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w < SCREEN.x);
                }
                if (EDGE == Edge::Right) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w > SCREEN.x + SCREEN.w);
                }
            }
            advance(pulling, 1);
            CHECK(pulling.closed());
        }
    }
    void exitCorridor() {
        const Box WINDOW{.x = 100, .y = 50, .w = 1000, .h = 400};
        const Box SCREEN{.x = 0, .y = 0, .w = 1920, .h = 1080};
        const Box BELOW{.x = 100, .y = 460, .w = 1000, .h = 500};
        const Box RIGHT{.x = 1110, .y = 50, .w = 600, .h = 400};
        auto p = params();
        p.flutter = p.variation = 0;
        p.duration = .9;
        for (const auto EDGE : {Edge::Bottom, Edge::Right}) {
            const Box OBSTACLE = EDGE == Edge::Bottom ? BELOW : RIGHT;
            ClothFlutter clear;
            ClothFlutter blocked;
            clear.begin(Transition::Close,
                p,
                1,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = EDGE, .screen = SCREEN});
            blocked.begin(Transition::Close,
                p,
                1,
                WINDOW,
                std::span<const Box>(&OBSTACLE, 1),
                ClothCloseGeometry{.edge = EDGE, .screen = SCREEN});
            CHECK(clear.waitForRecording());
            CHECK(blocked.waitForRecording());
            advance(clear, .7);
            advance(blocked, .7);
            CHECK(delta(pose(clear).p, pose(blocked).p) == 0);
            const auto FINAL = pose(blocked);
            for (const auto &v : FINAL.p) {
                CHECK(std::abs(v.x) < 20 && std::abs(v.y) < 20 && std::abs(v.z) < 20);
                if (EDGE == Edge::Bottom) {
                    CHECK(WINDOW.y - v.y * WINDOW.h > SCREEN.y + SCREEN.h);
                } else {
                    CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w > SCREEN.x + SCREEN.w);
                }
            }
        }
    }
    void handoff() {
        for (const double WHEN : {0.015, 0.25, 0.73}) {
            auto m = open();
            advance(m, WHEN);
            const auto BEFORE = pose(m);
            const double ALPHA = m.opacity();
            auto p = params();
            p.duration = 1.6;
            m.begin(Transition::Close,
                p,
                12,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
            const auto AFTER = pose(m);
            CHECK(delta(BEFORE.p, AFTER.p) < 1e-12);
            CHECK(delta(BEFORE.v, AFTER.v) < 1e-12);
            CHECK(m.opacity() == ALPHA);
            CHECK(m.waitForRecording());
            const auto READY = pose(m);
            CHECK(delta(BEFORE.p, READY.p) < 2e-6);
            CHECK(delta(BEFORE.v, READY.v) < 2e-5);
        }
    }
    void extremeDurationHandoff() {
        auto p = params();
        p.duration = .1;
        p.flutter = 1;
        p.variation = 0;
        auto m = open(p);
        advance(m, .03, .001);
        const auto BEFORE = pose(m);
        const double ALPHA = m.opacity();
        p.duration = 3.430429;
        m.begin(Transition::Close,
            p,
            2,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
        CHECK(m.waitForRecording());
        const auto AFTER = pose(m);
        CHECK(delta(BEFORE.p, AFTER.p) < 2e-6);
        CHECK(delta(BEFORE.v, AFTER.v) < 2e-4);
        CHECK(m.opacity() == ALPHA);
    }
    void cadence() {
        const auto SOURCE = open();
        auto reference = SOURCE;
        advance(reference, 0.63, 0.001);
        const auto EXPECTED = pose(reference);
        for (const double hz : {60., 120., 144., 240.}) {
            auto m = SOURCE;
            advance(m, 0.63, 1 / hz);
            CHECK(delta(EXPECTED.p, pose(m).p) < 1e-10);
        }
        auto irregular = SOURCE;
        double time = 0;
        const double STEPS[] = {.003, .027, .011, .007, .045, .016};
        int i = 0;
        while (time < .63 - 1e-12) {
            const double dt = std::min(STEPS[i++ % 6], .63 - time);
            irregular.advance(dt);
            time += dt;
        }
        CHECK(delta(EXPECTED.p, pose(irregular).p) < 1e-10);
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
        for (const auto KIND : {Transition::Open, Transition::Close}) {
            ClothPhysics::pauseWorkersForTesting(true, 4);
            ClothFlutter m;
            m.begin(KIND,
                params(),
                7,
                WINDOW,
                {},
                KIND == Transition::Close ? std::optional<ClothCloseGeometry>{ClothCloseGeometry{
                                                .edge = Edge::Bottom, .screen = SCREEN}}
                                          : std::optional<ClothCloseGeometry>{});
            CHECK(m.waitForSnapshots(4));
            const ClothFlutter WITNESS = m;
            advance(m, .1, dt);
            const auto HELD = pose(m);
            const double ALPHA = m.opacity();
            advance(m, .1, dt);
            CHECK(m.active());
            CHECK(delta(pose(m).p, HELD.p) < 1e-9);
            CHECK(std::abs(m.opacity() - ALPHA) < 1e-9);
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(WITNESS.waitForRecording());
            const auto BEFORE = pose(m);
            advance(m, .1, dt);
            CHECK(delta(pose(m).p, BEFORE.p) > 1e-9);
            advance(m, 2, dt);
            CHECK(KIND == Transition::Open ? !m.active() && !m.closed() : m.closed());
        }
    }

    void movementPublicationRecovery() {
        // A gap shorter than the stall bound must recover even when the compositor posts targets
        // faster than Jolt.
        for (const int REFRESH_RATE : {120, 360, 1000}) {
            const double dt = 1.0 / REFRESH_RATE;
            ClothFlutter m;
            m.beginMovement({}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            const ClothFlutter WITNESS = m;
            ClothPhysics::pauseWorkersForTesting(true, 2);
            Box box = WINDOW;
            for (int i = 0; i < REFRESH_RATE / 5; ++i) {
                box.x += 1;
                m.updateMovement(box, dt);
                m.advance(dt);
            }
            CHECK(m.moving());
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(WITNESS.waitForSnapshots(26, 2));
            m.advance(0);
            CHECK(m.moving());
            CHECK(delta(pose(m).p, pose(WITNESS).p) > 1e-7);
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
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) {
            ClothFlutter m;
            m.begin(Transition::Open, params(), i, WINDOW, {});
            m.reset();
        }
        CHECK(std::chrono::steady_clock::now() - START < std::chrono::seconds(1));
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
        const auto HORIZONTAL = [](const ClothFlutter &sheet) {
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
        CHECK(HORIZONTAL(m));
        m.beginMovement({}, 7, WINDOW);
        CHECK(HORIZONTAL(m));
        CHECK(m.waitForSnapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int k = 0; k < 24; ++k) {
            box.x += 4;
            movementStep(m, box, 1.0 / 120, time);
            CHECK(HORIZONTAL(m));
        }
    }

    // The displayed sleeve must stay in the real window bounds, including reversal, resize and
    // worker recovery.
    void pocketStaysWithinWindowBounds() {
        constexpr double dt = 1.0 / 120;
        for (const double SPRINGINESS : {0., .5, 1.}) {
            for (const double DIRECTION : {-1., 1.}) {
                auto material = params();
                material.flutter = material.variation = 0;
                auto m = open(material);
                m.beginMovement({.springiness = SPRINGINESS}, 7, WINDOW);
                CHECK(m.waitForSnapshots(2));
                Box box = WINDOW;
                double time = 0;
                double overhang = 0;
                bool ordered = true;
                bool level = true;
                const auto check = [&] {
                    const auto SAMPLED = pose(m);
                    SheetFrame frame;
                    m.frame({.x = box.w, .y = box.h}, frame);
                    for (int i = 0; i < GRID; ++i) {
                        overhang = std::max(overhang, (std::abs(frame.position[i].x) - .5) * box.w);
                        ordered &= i == 0 || SAMPLED.p[i].x >= SAMPLED.p[i - 1].x;
                        level &= std::abs(SAMPLED.p[i].y) < 1e-6 && std::abs(SAMPLED.p[i].z) < 1e-6;
                    }
                };
                check();
                for (int k = 0; k < 64; ++k) {
                    box.x += (k < 32 ? DIRECTION : -DIRECTION) * 12;
                    if (k == 32) {
                        box.w *= .7;
                        box.h *= 1.1;
                    }
                    movementStep(m, box, dt, time);
                    check();
                }
                ClothPhysics::pauseWorkersForTesting(true);
                for (int k = 0; k < 72; ++k) {
                    box.x -= DIRECTION * 12;
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
        for (const double DIRECTION : {-1., 1.}) {
            auto material = params();
            material.flutter = material.variation = 0;
            auto m = open(material);
            m.beginMovement({.springiness = 1}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            const auto START = pose(m);
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 24; ++k) {
                box.x += DIRECTION * .25;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto SPREAD = pose(m);
            const int TRAILING = DIRECTION > 0 ? 0 : PAPER_CELLS;
            const int LEADING = DIRECTION > 0 ? PAPER_CELLS : 0;
            const double TRAILING_SPREAD =
                -DIRECTION * (SPREAD.p[TRAILING].x - START.p[TRAILING].x);
            const double LEADING_SPREAD = DIRECTION * (SPREAD.p[LEADING].x - START.p[LEADING].x);
            CHECK(LEADING_SPREAD > .005);
            CHECK(TRAILING_SPREAD > LEADING_SPREAD * 1.35);
        }
    }

    void pocketSpringinessControl() {
        std::array<double, 3> peaks{};
        int mode = 0;
        for (const double SPRINGINESS : {0., .25, 1.}) {
            ClothMovementParams material;
            if (SPRINGINESS > 0) {
                material.springiness = SPRINGINESS;
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
                const auto MOVED = pose(m);
                for (int i = 0; i < GRID; ++i) {
                    const double REST = (static_cast<double>(i) / PAPER_CELLS) - .5;
                    peaks[mode] = std::max(peaks[mode], std::abs(MOVED.p[i].x - REST) * box.w);
                    lowerDrift = std::max(lowerDrift,
                        std::abs(
                            MOVED.p[(static_cast<std::size_t>(PAPER_CELLS) * GRID) + i].x - REST) *
                            box.w);
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
        for (const double DIRECTION : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter m;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 30; ++k) {
                box.x += DIRECTION * 5;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(m);
            bool onRod = true;
            for (int i = 0; i < GRID; ++i) {
                onRod &= MOVED.p[i].x >= -.50001 && MOVED.p[i].x <= .50001 &&
                         (i == 0 || MOVED.p[i].x >= MOVED.p[i - 1].x);
            }
            CHECK(onRod);
            CHECK(DIRECTION > 0 ? MOVED.p[PAPER_CELLS].x < .495 : MOVED.p[0].x > -.495);
        }
    }

    // The lengthwise spring must restore the pocket even while the window keeps travelling.
    void pocketSpringRestoresDuringTravel() {
        for (const double DIRECTION : {-1., 1.}) {
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
                box.x += DIRECTION * 5;
                movementStep(m, box, 1.0 / 120, time);
                const auto MOVED = pose(m);
                offset = 0;
                for (int i = 0; i < GRID; ++i) {
                    offset += MOVED.p[i].x - ((static_cast<double>(i) / PAPER_CELLS) - .5);
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
        for (const double DIRECTION : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter m;
            m.beginMovement(material, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            const auto STEP = [&](double dx) {
                box.x += dx;
                movementStep(m, box, 1.0 / 120, time);
            };
            const auto CENTER = [&] {
                const auto p = pose(m);
                double sum = 0;
                for (int i = 0; i < GRID; ++i) {
                    sum += p.p[i].x;
                }
                return sum / GRID;
            };
            const double START = CENTER();
            for (int i = 0; i < 8; ++i) {
                STEP(DIRECTION * 5);
            }
            const double EARLY = CENTER();
            for (int i = 0; i < 40; ++i) {
                STEP(DIRECTION * 5);
            }
            const double LATER = CENTER();
            CHECK(DIRECTION * (EARLY - START) < -1e-4);

            for (int i = 0; i < 30; ++i) {
                STEP(-DIRECTION * 5);
            }
            const auto REVERSED = pose(m);
            double reversedCenter = 0;
            bool onRod = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &p = REVERSED.p[i];
                reversedCenter += p.x;
                onRod &= p.x >= -.50001 && p.x <= .50001 && (i == 0 || p.x >= REVERSED.p[i - 1].x);
            }
            reversedCenter /= GRID;
            CHECK(onRod);
            CHECK(DIRECTION * (reversedCenter - LATER) > 1e-4);

            box.w *= .9;
            box.h *= 1.1;
            movementStep(m, box, 1.0 / 120, time);
            const auto RESIZED = pose(m);
            for (int i = 0; i < GRID; ++i) {
                CHECK(RESIZED.p[i].x >= -.50001 && RESIZED.p[i].x <= .50001);
                CHECK(i == 0 || RESIZED.p[i].x >= RESIZED.p[i - 1].x);
                CHECK(std::abs(RESIZED.p[i].y) < 1e-5 && std::abs(RESIZED.p[i].z) < 1e-5);
            }
        }
    }

    void curtainRodMovement() {
        for (const Vec2 DIRECTION : {Vec2{.x = 1, .y = 0},
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
                box.x += 4 * DIRECTION.x;
                box.y += 4 * DIRECTION.y;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(m);
            bool topAttached = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &p = MOVED.p[i];
                topAttached &= p.x >= -.50001 && p.x <= .50001 &&
                               (i == 0 || p.x >= MOVED.p[i - 1].x) && std::abs(p.y) < 1e-5 &&
                               std::abs(p.z) < 1e-5;
            }
            CHECK(topAttached);
            if (DIRECTION.x != 0) {
                const auto MOTION_AT = [&](int row) {
                    double sum = 0;
                    for (int i = 0; i < GRID; ++i) {
                        const auto &p = MOVED.p[(static_cast<std::size_t>(row) * GRID) + i];
                        sum +=
                            std::pow(
                                (p.x - (static_cast<double>(i) / PAPER_CELLS) + .5) * box.w, 2) +
                            std::pow((p.y + (static_cast<double>(row) / PAPER_CELLS)) * box.h, 2) +
                            (p.z * p.z * box.w * box.h);
                    }
                    return std::sqrt(sum / GRID);
                };
                CHECK(MOTION_AT(PAPER_CELLS) > MOTION_AT(PAPER_CELLS / 4) + 1);
            }
        }
    }

    void curtainRodClose() {
        for (const Edge EDGE : {Edge::Top, Edge::Bottom, Edge::Left, Edge::Right}) {
            auto m = close(EDGE);
            advance(m, .2);
            const auto MOVED = pose(m);
            bool straightRod = true;
            for (int i = 1; i < GRID; ++i) {
                straightRod &= MOVED.p[i].x >= MOVED.p[i - 1].x &&
                               MOVED.p[i].x - MOVED.p[0].x <= 1.00001 &&
                               std::abs(MOVED.p[i].y - MOVED.p[0].y) < 1e-5 &&
                               std::abs(MOVED.p[i].z - MOVED.p[0].z) < 1e-5;
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
        const double ALPHA = m.opacity();
        closing.begin(Transition::Close,
            ::params(),
            4,
            box,
            {},
            ClothCloseGeometry{.edge = Edge::Right, .screen = SCREEN});
        CHECK(closing.waitForSnapshots(2));
        CHECK(delta(pose(closing).p, moved.p) < 1e-6);
        CHECK(delta(pose(closing).v, moved.v) < 1e-5);
        CHECK(std::abs(closing.opacity() - ALPHA) < 1e-8);
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
        const auto BEFORE = pose(m);
        const double ALPHA = m.opacity();
        m.beginMovement({}, 2, WINDOW);
        CHECK(delta(pose(m).p, BEFORE.p) < 1e-6);
        CHECK(delta(pose(m).v, BEFORE.v) < 1e-6);
        CHECK(std::abs(m.opacity() - ALPHA) < 1e-8);
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
        const auto FALLBACK = pose(m);
        box.y += 20;
        m.updateMovement(box, .01);
        CHECK(m.moving());
        CHECK(delta(pose(m).p, FALLBACK.p) < 1e-6);
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
                std::min(cadence * (irregular ? (((frame++ % 2) != 0) ? 1.6 : .4) : 1), .2 - time);
            Box box = WINDOW;
            box.x += 300 * (time + dt);
            movementStep(m, box, dt, time);
        }
        return pose(m);
    }

    void movementCadenceAndInvalid() {
        const auto REFERENCE = movementTrajectory(1.0 / 120, 7);
        for (const double hz : {60., 144., 240.}) {
            const auto SAMPLED = movementTrajectory(1 / hz, 100);
            CHECK(delta(REFERENCE.p, SAMPLED.p) < .015);
        }
        const auto SAME = movementTrajectory(1.0 / 120, 900);
        CHECK(delta(REFERENCE.p, SAME.p) == 0);
        CHECK(delta(REFERENCE.p, movementTrajectory(1.0 / 120, 100, true).p) < .015);
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
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            m.beginMovement({}, i, WINDOW);
            m.reset();
        }
        CHECK(std::chrono::steady_clock::now() - START < std::chrono::seconds(1));
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
            worst = std::max(worst, delta(answered[static_cast<std::size_t>(k - 1)], pose(m).p));
        }
        ClothPhysics::pauseWorkersForTesting(false);
        CHECK(worst < 1e-9);
    }

    void movementGravityControl() {
        const auto HEM_AFTER_DRAG = [](double gravity) {
            ClothFlutter m;
            m.beginMovement({.variation = 0, .gravity = gravity}, 7, WINDOW);
            CHECK(m.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 36; ++frame) {
                box.y += 2800.0 / 120;
                movementStep(m, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(m);
            double hem = 0;
            for (int i = 0; i < GRID; ++i) {
                hem += MOVED.p[(static_cast<std::size_t>(PAPER_CELLS) * GRID) + i].y;
            }
            return hem / GRID;
        };
        const double ZERO = HEM_AFTER_DRAG(0);
        const double NORMAL = HEM_AFTER_DRAG(1);
        const double HEAVY = HEM_AFTER_DRAG(3);
        CHECK(ZERO > NORMAL + .01);
        CHECK(NORMAL > HEAVY + .01);
        CHECK(HEM_AFTER_DRAG(-1) == ZERO);
        CHECK(HEM_AFTER_DRAG(4) == HEAVY);
        for (const double INVALID :
            {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            ClothFlutter m;
            m.beginMovement({.gravity = INVALID}, 7, WINDOW);
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
            const auto LIFTED = pose(m);
            double hem = 0;
            for (int i = 0; i < GRID; ++i) {
                hem += LIFTED.p[(static_cast<std::size_t>(PAPER_CELLS) * GRID) + i].y + 1;
            }
            CHECK(hem / GRID > .3);
            for (int i = 0; i < GRID; ++i) {
                CHECK(std::abs(LIFTED.p[i].y) < 1e-5);
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
        const auto MOVED = pose(m);
        const auto ANCHOR = static_cast<std::size_t>(PAPER_CELLS);
        CHECK(MOVED.p[ANCHOR].x >= -.50001 && MOVED.p[ANCHOR].x <= .50001 &&
              std::abs(MOVED.p[ANCHOR].y) < 1e-5);
        for (int i = 0; i < 48; ++i) {
            movementStep(m, box, 1.0 / 120, time);
        }
        m.advance(.12);  // begin the bounded final flattening transition
        const auto SETTLING = pose(m);
        box.y += 10;
        m.updateMovement(box, .01);
        CHECK(m.moving());
        CHECK(delta(pose(m).p, SETTLING.p) < 1e-6);
        CHECK(delta(pose(m).v, SETTLING.v) < 1e-6);
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
        const auto ONSET = pose(m);
        CHECK(std::abs(ONSET.p[PAPER_CELLS].x - .5) < 1e-7);
        CHECK(std::abs(ONSET.v[PAPER_CELLS].x - 1.2) < 1e-6);
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
        const auto HELD = pose(m);
        advance(m, .02);
        CHECK(delta(pose(m).p, HELD.p) < 1e-9);
        ClothPhysics::pauseWorkersForTesting(false);
    }
}  // namespace
int main() {
    needsPhysics();
    {
        const ClothPhysics PHYSICS;
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
        const ClothPhysics PHYSICS;
        ClothPhysics::pauseWorkersForTesting(true);
        survivor.begin(Transition::Open, params(), 1, WINDOW, {});
    }
    survivor.reset();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d cloth checks failed\n", g_failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
}
