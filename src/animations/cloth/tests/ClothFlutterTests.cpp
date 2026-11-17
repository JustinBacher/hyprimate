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
        Pose result;
        m.sampleVertices(result.p, result.v);
        const auto FINITE = [](const Vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        CHECK(std::all_of(result.p.begin(), result.p.end(), FINITE));
        CHECK(std::all_of(result.v.begin(), result.v.end(), FINITE));
        return result;
    }
    double delta(const SheetGrid &a, const SheetGrid &b) {
        double maxDelta = 0;
        for (std::size_t k = 0; k < a.size(); ++k) {
            maxDelta = std::max({maxDelta,
                std::abs(a[k].x - b[k].x),
                std::abs(a[k].y - b[k].y),
                std::abs(a[k].z - b[k].z)});
        }
        return maxDelta;
    }
    void advance(ClothFlutter &m, double seconds, double step = 1.0 / 144) {
        while (seconds > 1e-12 && m.active()) {
            const double STEP_SECONDS = std::min(step, seconds);
            m.advance(STEP_SECONDS);
            seconds -= STEP_SECONDS;
        }
    }
    ClothFlutter open(ClothParams p = params(), std::uint64_t seed = 7) {
        ClothFlutter cloth;
        cloth.begin(Transition::Open, p, seed, WINDOW, {});
        CHECK(cloth.waitForRecording());
        return cloth;
    }
    ClothFlutter close(Edge edge, ClothParams p = params()) {
        ClothFlutter cloth;
        cloth.begin(Transition::Close,
            p,
            3,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = edge, .screen = SCREEN});
        CHECK(cloth.waitForRecording());
        return cloth;
    }
    void needsPhysics() {
        ClothFlutter cloth;
        cloth.begin(Transition::Open, params(), 1, WINDOW, {});
        CHECK(!cloth.active());
    }
    void openingSettlesFlat() {
        auto material = params();
        material.flutter = 0;
        auto cloth = open(material);
        CHECK(cloth.opacity() == 0);
        advance(cloth, .85);
        CHECK(!cloth.active() && !cloth.closed() && cloth.opacity() == 1);
        hyprimate::SheetFrame frame;
        cloth.frame({.x = WINDOW.w, .y = WINDOW.h}, frame);
        for (std::size_t vertex = 0; vertex < hyprimate::PAPER_GRID_VERTICES; ++vertex) {
            const auto TEX_COORD = hyprimate::sheetUv(vertex);
            CHECK(frame.position[vertex].x == TEX_COORD.x - 0.5 &&
                  frame.position[vertex].y == TEX_COORD.y - 0.5);
        }
        for (const auto &shade : frame.shade) {
            for (const float SHADE : shade) {
                CHECK(SHADE == 0.0F);
            }
        }
    }
    // Gathering a curtain makes folds by spending its width in depth, not by stretching or
    // shrinking its material.
    void curtainDrape() {
        auto material = params();
        material.flutter = material.variation = 0;
        const auto CLOTH = open(material);
        const auto START = pose(CLOTH);
        double contour = 0;
        double near = 1e9;
        double far = -1e9;
        const int ROW = PAPER_CELLS / 2;
        for (int i = 0; i < GRID; ++i) {
            const auto &vertex = START.p[(static_cast<std::size_t>(ROW) * GRID) + i];
            near = std::min(near, vertex.z);
            far = std::max(far, vertex.z);
            if (i != 0) {
                const auto &previousVertex =
                    START.p[(static_cast<std::size_t>(ROW) * GRID) + i - 1];
                contour += std::hypot(vertex.x - previousVertex.x,
                    vertex.y - previousVertex.y,
                    vertex.z - previousVertex.z);
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
        const double LENGTH_SCALE = std::sqrt(WINDOW.w * WINDOW.h);
        for (const double FLUTTER : {0.0, 0.5}) {
            auto material = params();
            material.flutter = FLUTTER;
            auto cloth = open(material);
            std::vector<SheetGrid> frames;
            while (cloth.active()) {
                cloth.advance(1.0 / 120);
                frames.push_back(pose(cloth).p);
            }
            double worst = 0;
            for (std::size_t frameIndex = 2; frameIndex < frames.size(); ++frameIndex) {
                for (std::size_t k = 0; k < frames[frameIndex].size(); ++k) {
                    const Vec3 EARLIER = frames[frameIndex - 2][k];
                    const Vec3 PREVIOUS = frames[frameIndex - 1][k];
                    const Vec3 CURRENT = frames[frameIndex][k];
                    worst = std::max(worst,
                        std::hypot((CURRENT.x - (2 * PREVIOUS.x) + EARLIER.x) * WINDOW.w,
                            (CURRENT.y - (2 * PREVIOUS.y) + EARLIER.y) * WINDOW.h,
                            (CURRENT.z - (2 * PREVIOUS.z) + EARLIER.z) * LENGTH_SCALE));
                }
            }
            // Per-frame velocity change in pixels at 120 Hz; the undamped swing peaked near 60
            // here.
            CHECK(worst < 30);
        }
    }
    void exits() {
        auto material = params();
        material.flutter = 0;
        auto falling = close(Edge::Bottom, material);
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
        for (const auto &vertexPos : LATE.p) {
            CHECK(WINDOW.y + WINDOW.h / 2 + (-vertexPos.y * WINDOW.h - WINDOW.h / 2) > SCREEN.y + SCREEN.h);
        }
        advance(falling, 1);
        CHECK(falling.closed() && falling.opacity() == 0);
        for (const Edge EDGE : {Edge::Top, Edge::Left, Edge::Right}) {
            auto pulling = close(EDGE, material);
            const auto INITIAL = pose(pulling);
            advance(pulling, 0.4);
            const auto MID = pose(pulling);
            for (int i = 0; i < GRID; ++i) {
                const std::size_t INDEX = i;
                if (EDGE == Edge::Top) {
                    CHECK(MID.p[INDEX].y > INITIAL.p[INDEX].y + 0.5);
                }
                if (EDGE == Edge::Left) {
                    CHECK(MID.p[INDEX].x < INITIAL.p[INDEX].x - 0.5);
                }
                if (EDGE == Edge::Right) {
                    CHECK(MID.p[INDEX].x > INITIAL.p[INDEX].x + 0.5);
                }
            }
            advance(pulling, 0.35);
            const auto END = pose(pulling);
            for (const auto &vertex : END.p) {
                if (EDGE == Edge::Top) {
                    CHECK(WINDOW.y - vertex.y * WINDOW.h < SCREEN.y);
                }
                if (EDGE == Edge::Left) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + vertex.x * WINDOW.w < SCREEN.x);
                }
                if (EDGE == Edge::Right) {
                    CHECK(WINDOW.x + WINDOW.w / 2 + vertex.x * WINDOW.w > SCREEN.x + SCREEN.w);
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
        auto material = params();
        material.flutter = material.variation = 0;
        material.duration = .9;
        for (const auto EDGE : {Edge::Bottom, Edge::Right}) {
            const Box OBSTACLE = EDGE == Edge::Bottom ? BELOW : RIGHT;
            ClothFlutter clear;
            ClothFlutter blocked;
            clear.begin(Transition::Close,
                material,
                1,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = EDGE, .screen = SCREEN});
            blocked.begin(Transition::Close,
                material,
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
            for (const auto &vertex : FINAL.p) {
                CHECK(
                    std::abs(vertex.x) < 20 && std::abs(vertex.y) < 20 && std::abs(vertex.z) < 20);
                if (EDGE == Edge::Bottom) {
                    CHECK(WINDOW.y - vertex.y * WINDOW.h > SCREEN.y + SCREEN.h);
                } else {
                    CHECK(WINDOW.x + WINDOW.w / 2 + vertex.x * WINDOW.w > SCREEN.x + SCREEN.w);
                }
            }
        }
    }
    void handoff() {
        for (const double WHEN : {0.015, 0.25, 0.73}) {
            auto cloth = open();
            advance(cloth, WHEN);
            const auto BEFORE = pose(cloth);
            const double ALPHA = cloth.opacity();
            auto material = params();
            material.duration = 1.6;
            cloth.begin(Transition::Close,
                material,
                12,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
            const auto AFTER = pose(cloth);
            CHECK(delta(BEFORE.p, AFTER.p) < 1e-12);
            CHECK(delta(BEFORE.v, AFTER.v) < 1e-12);
            CHECK(cloth.opacity() == ALPHA);
            CHECK(cloth.waitForRecording());
            const auto READY = pose(cloth);
            CHECK(delta(BEFORE.p, READY.p) < 2e-6);
            CHECK(delta(BEFORE.v, READY.v) < 2e-5);
        }
    }
    void extremeDurationHandoff() {
        auto material = params();
        material.duration = .1;
        material.flutter = 1;
        material.variation = 0;
        auto cloth = open(material);
        advance(cloth, .03, .001);
        const auto BEFORE = pose(cloth);
        const double ALPHA = cloth.opacity();
        material.duration = 3.430429;
        cloth.begin(Transition::Close,
            material,
            2,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
        CHECK(cloth.waitForRecording());
        const auto AFTER = pose(cloth);
        CHECK(delta(BEFORE.p, AFTER.p) < 2e-6);
        CHECK(delta(BEFORE.v, AFTER.v) < 2e-4);
        CHECK(cloth.opacity() == ALPHA);
    }
    void cadence() {
        const auto SOURCE = open();
        auto reference = SOURCE;
        advance(reference, 0.63, 0.001);
        const auto EXPECTED = pose(reference);
        for (const double REFRESH_HZ : {60., 120., 144., 240.}) {
            auto cloth = SOURCE;
            advance(cloth, 0.63, 1 / REFRESH_HZ);
            CHECK(delta(EXPECTED.p, pose(cloth).p) < 1e-10);
        }
        auto irregular = SOURCE;
        double time = 0;
        const double STEPS[] = {.003, .027, .011, .007, .045, .016};
        int stepIndex = 0;
        while (time < .63 - 1e-12) {
            const double STEP_SECONDS = std::min(STEPS[stepIndex++ % 6], .63 - time);
            irregular.advance(STEP_SECONDS);
            time += STEP_SECONDS;
        }
        CHECK(delta(EXPECTED.p, pose(irregular).p) < 1e-10);
    }
    void deterministic() {
        auto material = params();
        material.variation = 0;
        auto first = open(material, 5);
        auto second = open(material, 900);
        advance(first, .43);
        advance(second, .43);
        CHECK(delta(pose(first).p, pose(second).p) == 0);
    }
    void invalid() {
        auto material = params();
        material.duration = std::numeric_limits<double>::quiet_NaN();
        ClothFlutter cloth;
        cloth.begin(Transition::Open, material, 1, WINDOW, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Open, params(), 1, Box{.x = 0, .y = 0, .w = 0, .h = 500}, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Open,
            params(),
            1,
            Box{.x = 0, .y = 0, .w = std::numeric_limits<double>::max(), .h = 500},
            {});
        CHECK(!cloth.active());
        cloth.begin(static_cast<Transition>(99), params(), 1, WINDOW, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Close,
            params(),
            1,
            WINDOW,
            {},
            ClothCloseGeometry{
                .edge = Edge::Left, .screen = Box{.x = 0, .y = 0, .w = -1, .h = 500}});
        CHECK(!cloth.active());
        cloth = open();
        cloth.advance(-1);
        CHECK(!cloth.active() && !cloth.closed());
        cloth = close(Edge::Top);
        cloth.advance(std::numeric_limits<double>::infinity());
        CHECK(cloth.closed());
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
        constexpr double STEP_SECONDS = 1.0 / 120;
        for (const auto KIND : {Transition::Open, Transition::Close}) {
            ClothPhysics::pauseWorkersForTesting(true, 4);
            ClothFlutter cloth;
            cloth.begin(KIND,
                params(),
                7,
                WINDOW,
                {},
                KIND == Transition::Close ? std::optional<ClothCloseGeometry>{ClothCloseGeometry{
                                                .edge = Edge::Bottom, .screen = SCREEN}}
                                          : std::optional<ClothCloseGeometry>{});
            CHECK(cloth.waitForSnapshots(4));
            const ClothFlutter WITNESS = cloth;
            advance(cloth, .1, STEP_SECONDS);
            const auto HELD = pose(cloth);
            const double ALPHA = cloth.opacity();
            advance(cloth, .1, STEP_SECONDS);
            CHECK(cloth.active());
            CHECK(delta(pose(cloth).p, HELD.p) < 1e-9);
            CHECK(std::abs(cloth.opacity() - ALPHA) < 1e-9);
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(WITNESS.waitForRecording());
            const auto BEFORE = pose(cloth);
            advance(cloth, .1, STEP_SECONDS);
            CHECK(delta(pose(cloth).p, BEFORE.p) > 1e-9);
            advance(cloth, 2, STEP_SECONDS);
            CHECK(KIND == Transition::Open ? !cloth.active() && !cloth.closed() : cloth.closed());
        }
    }

    void movementPublicationRecovery() {
        // A gap shorter than the stall bound must recover even when the compositor posts targets
        // faster than Jolt.
        for (const int REFRESH_RATE : {120, 360, 1000}) {
            const double STEP_SECONDS = 1.0 / REFRESH_RATE;
            ClothFlutter cloth;
            cloth.beginMovement({}, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            const ClothFlutter WITNESS = cloth;
            ClothPhysics::pauseWorkersForTesting(true, 2);
            Box box = WINDOW;
            for (int i = 0; i < REFRESH_RATE / 5; ++i) {
                box.x += 1;
                cloth.updateMovement(box, STEP_SECONDS);
                cloth.advance(STEP_SECONDS);
            }
            CHECK(cloth.moving());
            ClothPhysics::pauseWorkersForTesting(false);
            CHECK(WITNESS.waitForSnapshots(26, 2));
            cloth.advance(0);
            CHECK(cloth.moving());
            CHECK(delta(pose(cloth).p, pose(WITNESS).p) > 1e-7);
        }
    }

    void clothShine() {
        ClothFlutter cloth;
        auto material = params();
        material.shine = std::numeric_limits<double>::quiet_NaN();
        cloth.begin(Transition::Open, material, 1, WINDOW, {});
        CHECK(!cloth.active());
        ClothMovementParams movement;
        movement.shine = std::numeric_limits<double>::infinity();
        cloth.beginMovement(movement, 1, WINDOW);
        CHECK(!cloth.active());

        auto source = open();
        advance(source, .25);
        auto dark = source;
        auto bright = source;
        movement.shine = 0;
        dark.beginMovement(movement, 1, WINDOW);
        movement.shine = 1;
        bright.beginMovement(movement, 1, WINDOW);
        SheetFrame first;
        SheetFrame second;
        dark.frame({.x = WINDOW.w, .y = WINDOW.h}, first);
        bright.frame({.x = WINDOW.w, .y = WINDOW.h}, second);
        bool highlight = false;
        for (std::size_t triangle = 0; triangle < PAPER_TRIANGLES; ++triangle) {
            for (int corner = 0; corner < 3; ++corner) {
                CHECK(first.shade[triangle][corner] <= 0);
                if (second.shade[triangle][corner] > 0) {
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
            ClothFlutter cloth;
            cloth.begin(Transition::Open, params(), i, WINDOW, {});
            cloth.reset();
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
        auto cloth = open();
        advance(cloth, .2);
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
        CHECK(HORIZONTAL(cloth));
        cloth.beginMovement({}, 7, WINDOW);
        CHECK(HORIZONTAL(cloth));
        CHECK(cloth.waitForSnapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int k = 0; k < 24; ++k) {
            box.x += 4;
            movementStep(cloth, box, 1.0 / 120, time);
            CHECK(HORIZONTAL(cloth));
        }
    }

    // The displayed sleeve must stay in the real window bounds, including reversal, resize and
    // worker recovery.
    void pocketStaysWithinWindowBounds() {
        constexpr double STEP_SECONDS = 1.0 / 120;
        for (const double SPRINGINESS : {0., .5, 1.}) {
            for (const double DIRECTION : {-1., 1.}) {
                auto material = params();
                material.flutter = material.variation = 0;
                auto cloth = open(material);
                cloth.beginMovement({.springiness = SPRINGINESS}, 7, WINDOW);
                CHECK(cloth.waitForSnapshots(2));
                Box box = WINDOW;
                double time = 0;
                double overhang = 0;
                bool ordered = true;
                bool level = true;
                const auto check = [&] {
                    const auto SAMPLED = pose(cloth);
                    SheetFrame frame;
                    cloth.frame({.x = box.w, .y = box.h}, frame);
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
                    movementStep(cloth, box, STEP_SECONDS, time);
                    check();
                }
                ClothPhysics::pauseWorkersForTesting(true);
                for (int k = 0; k < 72; ++k) {
                    box.x -= DIRECTION * 12;
                    cloth.updateMovement(box, STEP_SECONDS);
                    cloth.advance(STEP_SECONDS);
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
            auto cloth = open(material);
            cloth.beginMovement({.springiness = 1}, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            const auto START = pose(cloth);
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 24; ++k) {
                box.x += DIRECTION * .25;
                movementStep(cloth, box, 1.0 / 120, time);
            }
            const auto SPREAD = pose(cloth);
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
            ClothFlutter cloth;
            cloth.beginMovement(material, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            double lowerDrift = 0;
            for (int k = 0; k < 48; ++k) {
                box.x += k < 24 ? 5 : -5;
                movementStep(cloth, box, 1.0 / 120, time);
                const auto MOVED = pose(cloth);
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
            ClothFlutter cloth;
            cloth.beginMovement(material, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 30; ++k) {
                box.x += DIRECTION * 5;
                movementStep(cloth, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(cloth);
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
            ClothFlutter cloth;
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            cloth.beginMovement(material, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            double peak = 0;
            double offset = 0;
            for (int frame = 0; frame < 120; ++frame) {
                box.x += DIRECTION * 5;
                movementStep(cloth, box, 1.0 / 120, time);
                const auto MOVED = pose(cloth);
                offset = 0;
                for (int i = 0; i < GRID; ++i) {
                    offset += MOVED.p[i].x - ((static_cast<double>(i) / PAPER_CELLS) - .5);
                }
                offset /= GRID;
                peak = std::max(peak, std::abs(offset));
            }
            CHECK(cloth.moving());
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
            ClothFlutter cloth;
            cloth.beginMovement(material, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            const auto STEP = [&](double dx) {
                box.x += dx;
                movementStep(cloth, box, 1.0 / 120, time);
            };
            const auto CENTER = [&] {
                const auto POSE = pose(cloth);
                double sum = 0;
                for (int i = 0; i < GRID; ++i) {
                    sum += POSE.p[i].x;
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
            const auto REVERSED = pose(cloth);
            double reversedCenter = 0;
            bool onRod = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &vertex = REVERSED.p[i];
                reversedCenter += vertex.x;
                onRod &= vertex.x >= -.50001 && vertex.x <= .50001 &&
                         (i == 0 || vertex.x >= REVERSED.p[i - 1].x);
            }
            reversedCenter /= GRID;
            CHECK(onRod);
            CHECK(DIRECTION * (reversedCenter - LATER) > 1e-4);

            box.w *= .9;
            box.h *= 1.1;
            movementStep(cloth, box, 1.0 / 120, time);
            const auto RESIZED = pose(cloth);
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
            ClothFlutter cloth;
            ClothMovementParams material;
            material.variation = 0;
            cloth.beginMovement(material, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 24; ++frame) {
                box.x += 4 * DIRECTION.x;
                box.y += 4 * DIRECTION.y;
                movementStep(cloth, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(cloth);
            bool topAttached = true;
            for (int i = 0; i < GRID; ++i) {
                const auto &vertexPos = MOVED.p[i];
                topAttached &= vertexPos.x >= -.50001 && vertexPos.x <= .50001 &&
                               (i == 0 || vertexPos.x >= MOVED.p[i - 1].x) && std::abs(vertexPos.y) < 1e-5 &&
                               std::abs(vertexPos.z) < 1e-5;
            }
            CHECK(topAttached);
            if (DIRECTION.x != 0) {
                const auto MOTION_AT = [&](int row) {
                    double sum = 0;
                    for (int i = 0; i < GRID; ++i) {
                        const auto &vertex = MOVED.p[(static_cast<std::size_t>(row) * GRID) + i];
                        sum +=
                            std::pow(
                                (vertex.x - (static_cast<double>(i) / PAPER_CELLS) + .5) * box.w,
                                2) +
                            std::pow(
                                (vertex.y + (static_cast<double>(row) / PAPER_CELLS)) * box.h, 2) +
                            (vertex.z * vertex.z * box.w * box.h);
                    }
                    return std::sqrt(sum / GRID);
                };
                CHECK(MOTION_AT(PAPER_CELLS) > MOTION_AT(PAPER_CELLS / 4) + 1);
            }
        }
    }

    void curtainRodClose() {
        for (const Edge EDGE : {Edge::Top, Edge::Bottom, Edge::Left, Edge::Right}) {
            auto cloth = close(EDGE);
            advance(cloth, .2);
            const auto MOVED = pose(cloth);
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
        ClothFlutter cloth;
        cloth.beginMovement(params, 1, WINDOW);
        CHECK(cloth.moving() && cloth.waitForSnapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int i = 0; i < 24; ++i) {
            box.x += 4;
            movementStep(cloth, box, 1.0 / 120, time);
        }
        auto moved = pose(cloth);
        CHECK(
            std::any_of(moved.p.begin(), moved.p.end(), [](const Vec3 &v) { return v.z > .001; }));
        for (int i = 0; i < 12; ++i) {
            box.x -= 4;
            movementStep(cloth, box, 1.0 / 120, time);
        }
        moved = pose(cloth);
        // A retained copy freezes the displayed pose; closing consumes it without sharing mutable
        // playback state.
        ClothFlutter closing = cloth;
        const double ALPHA = cloth.opacity();
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
        movementStep(cloth, box, 1.0 / 120, time);
        for (int i = 0; i < 55 && cloth.moving(); ++i) {
            movementStep(cloth, box, 1.0 / 120, time);
        }
        advance(cloth, .25);
        CHECK(!cloth.active() && cloth.opacity() == 1);
        hyprimate::SheetFrame frame;
        cloth.frame({.x = box.w, .y = box.h}, frame);
        for (std::size_t vertex = 0; vertex < hyprimate::PAPER_GRID_VERTICES; ++vertex) {
            const auto TEX_COORD = hyprimate::sheetUv(vertex);
            CHECK(frame.position[vertex].x == TEX_COORD.x - .5 &&
                  frame.position[vertex].y == TEX_COORD.y - .5);
        }
    }

    void movementHandoff() {
        auto cloth = open();
        advance(cloth, .3);
        const auto BEFORE = pose(cloth);
        const double ALPHA = cloth.opacity();
        cloth.beginMovement({}, 2, WINDOW);
        CHECK(delta(pose(cloth).p, BEFORE.p) < 1e-6);
        CHECK(delta(pose(cloth).v, BEFORE.v) < 1e-6);
        CHECK(std::abs(cloth.opacity() - ALPHA) < 1e-8);
        CHECK(cloth.waitForSnapshots(2));
        Box box = WINDOW;
        box.x += 10;
        double time = 0;
        movementStep(cloth, box, 1.0 / 120, time);
        CHECK(cloth.moving());
        // Bounded starvation flattens, and a new move during that flattening continues from its
        // visible pose.
        ClothPhysics::pauseWorkersForTesting(true, 2);
        advance(cloth, .12);
        const auto FALLBACK = pose(cloth);
        box.y += 20;
        cloth.updateMovement(box, .01);
        CHECK(cloth.moving());
        CHECK(delta(pose(cloth).p, FALLBACK.p) < 1e-6);
        ClothPhysics::pauseWorkersForTesting(false);
        cloth.reset();
    }

    Pose movementTrajectory(double cadence, std::uint64_t seed, bool irregular = false) {
        ClothMovementParams params;
        params.variation = 0;
        ClothFlutter cloth;
        cloth.beginMovement(params, seed, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        double time = 0;
        int frame = 0;
        while (time < .2 - 1e-9) {
            const double STEP_SECONDS =
                std::min(cadence * (irregular ? (((frame++ % 2) != 0) ? 1.6 : .4) : 1), .2 - time);
            Box box = WINDOW;
            box.x += 300 * (time + STEP_SECONDS);
            movementStep(cloth, box, STEP_SECONDS, time);
        }
        return pose(cloth);
    }

    void movementCadenceAndInvalid() {
        const auto REFERENCE = movementTrajectory(1.0 / 120, 7);
        for (const double REFRESH_HZ : {60., 144., 240.}) {
            const auto SAMPLED = movementTrajectory(1 / REFRESH_HZ, 100);
            CHECK(delta(REFERENCE.p, SAMPLED.p) < .015);
        }
        const auto SAME = movementTrajectory(1.0 / 120, 900);
        CHECK(delta(REFERENCE.p, SAME.p) == 0);
        CHECK(delta(REFERENCE.p, movementTrajectory(1.0 / 120, 100, true).p) < .015);
        ClothFlutter cloth;
        auto params = ClothMovementParams{};
        params.strength = 0;
        cloth.beginMovement(params, 1, WINDOW);
        CHECK(!cloth.active());
        params.strength = std::numeric_limits<double>::quiet_NaN();
        cloth.beginMovement(params, 1, WINDOW);
        CHECK(!cloth.active());
        cloth.beginMovement({}, 1, WINDOW);
        cloth.updateMovement(Box{.x = 0, .y = 0, .w = -1, .h = 200}, .01);
        CHECK(!cloth.active());
        ClothPhysics::pauseWorkersForTesting(true, 2);
        cloth.beginMovement({}, 1, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        advance(cloth, .5);
        CHECK(!cloth.active());
        ClothPhysics::pauseWorkersForTesting(false);
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            cloth.beginMovement({}, i, WINDOW);
            cloth.reset();
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
            ClothFlutter animation;
            animation.beginMovement(params, 3, WINDOW);
            CHECK(animation.waitForSnapshots(2));
            Box box = WINDOW;
            for (int k = 1; k <= FRAMES; ++k) {
                box.x += 5;
                animation.updateMovement(box, FRAME);
                animation.advance(FRAME);
                CHECK(animation.waitForSnapshots(2 + 2 * k));
                animation.advance(0);
                answered.push_back(pose(animation).p);
            }
        }
        ClothFlutter cloth;
        cloth.beginMovement(params, 3, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        Box box = WINDOW;
        double worst = 0;
        for (int k = 1; k <= FRAMES; ++k) {
            // Let the worker answer every earlier target, then hold it before the one this frame
            // posts.
            ClothPhysics::pauseWorkersForTesting(true, 2 + (2 * (k - 1)));
            CHECK(cloth.waitForSnapshots(2 + 2 * (k - 1)));
            box.x += 5;
            cloth.updateMovement(box, FRAME);
            cloth.advance(FRAME);
            worst =
                std::max(worst, delta(answered[static_cast<std::size_t>(k - 1)], pose(cloth).p));
        }
        ClothPhysics::pauseWorkersForTesting(false);
        CHECK(worst < 1e-9);
    }

    void movementGravityControl() {
        const auto HEM_AFTER_DRAG = [](double gravity) {
            ClothFlutter animation;
            animation.beginMovement({.variation = 0, .gravity = gravity}, 7, WINDOW);
            CHECK(animation.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 36; ++frame) {
                box.y += 2800.0 / 120;
                movementStep(animation, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(animation);
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
            ClothFlutter cloth;
            cloth.beginMovement({.gravity = INVALID}, 7, WINDOW);
            CHECK(!cloth.active());
        }
    }

    // A quick downward pull leaves the free hem behind instead of dragging a rigid panel.
    void downwardDragLiftsHem() {
        for (const double REFRESH_HZ : {60., 120., 240.}) {
            ClothFlutter cloth;
            cloth.beginMovement({}, 7, WINDOW);
            CHECK(cloth.waitForSnapshots(2));
            Box box = WINDOW;
            double time = 0;
            while (time < .3 - 1e-9) {
                box.y = WINDOW.y + (2800 * (time + (1 / REFRESH_HZ)));
                movementStep(cloth, box, 1 / REFRESH_HZ, time);
            }
            const auto LIFTED = pose(cloth);
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
        ClothFlutter cloth;
        cloth.beginMovement({}, 1, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        double time = 0;
        Box box = WINDOW;
        box.x += 300;
        movementStep(cloth, box, 1.0 / 120, time);
        movementStep(cloth, box, 1.0 / 120, time);
        const auto MOVED = pose(cloth);
        const auto ANCHOR = static_cast<std::size_t>(PAPER_CELLS);
        CHECK(MOVED.p[ANCHOR].x >= -.50001 && MOVED.p[ANCHOR].x <= .50001 &&
              std::abs(MOVED.p[ANCHOR].y) < 1e-5);
        for (int i = 0; i < 48; ++i) {
            movementStep(cloth, box, 1.0 / 120, time);
        }
        cloth.advance(.12);  // begin the bounded final flattening transition
        const auto SETTLING = pose(cloth);
        box.y += 10;
        cloth.updateMovement(box, .01);
        CHECK(cloth.moving());
        CHECK(delta(pose(cloth).p, SETTLING.p) < 1e-6);
        CHECK(delta(pose(cloth).v, SETTLING.v) < 1e-6);
    }

    void movementRodBeforePublication() {
        ClothFlutter cloth;
        cloth.beginMovement({}, 1, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        ClothPhysics::pauseWorkersForTesting(true, 2);
        Box box = WINDOW;
        box.x += 20;
        cloth.updateMovement(box, 1.0 / 60);
        cloth.advance(1.0 / 60);
        const auto ONSET = pose(cloth);
        CHECK(std::abs(ONSET.p[PAPER_CELLS].x - .5) < 1e-7);
        CHECK(std::abs(ONSET.v[PAPER_CELLS].x - 1.2) < 1e-6);
        ClothPhysics::pauseWorkersForTesting(false);
        cloth.reset();

        cloth.beginMovement({}, 1, WINDOW);
        CHECK(cloth.waitForSnapshots(2));
        box = WINDOW;
        double time = 0;
        for (int i = 0; i < 4; ++i) {
            box.x += 10;
            movementStep(cloth, box, 1.0 / 120, time);
        }
        ClothPhysics::pauseWorkersForTesting(true, 6);
        cloth.updateMovement(box, 1.0 / 60);
        // Already-published samples may still advance the sliding pocket; hold only once those are
        // consumed.
        advance(cloth, .1);
        const auto HELD = pose(cloth);
        advance(cloth, .02);
        CHECK(delta(pose(cloth).p, HELD.p) < 1e-9);
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
