#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleCurl.hpp"
#include "animations/bubble/BubbleRupture.hpp"
#include "animations/bubble/BubbleSheets.hpp"
#include "animations/shared/Core.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{.x = 1000, .y = 700};

    void advanceFor(Bubble &b, double seconds, double step = 0.01) {
        while (seconds > 1e-12) {
            const double DELTA_TIME = std::min(seconds, step);
            b.advance(DELTA_TIME);
            seconds -= DELTA_TIME;
        }
    }

    void separateSurfacesDuringOpening() {
        for (const Vec2 SIZE : {Vec2{.x = 1400, .y = 300}, Vec2{.x = 300, .y = 1400}, SIZE}) {
            Bubble testBubble;
            testBubble.begin(Transition::Open, 1, 42, SIZE);
            advanceFor(testBubble, 0.25);
            auto bubbleFrame = testBubble.frame(SIZE);
            CHECK(bubbleFrame.shell.visibility > 0 && bubbleFrame.window.visibility == 0);
            CHECK(bubbleFrame.shell.halfSize.x == bubbleFrame.shell.halfSize.y &&
                  bubbleFrame.shell.corner == bubbleFrame.shell.halfSize.x);
            CHECK(bubbleFrame.shell.depth > 0 && bubbleFrame.shell.film > 0);
            const double SHELL_RADIUS = bubbleFrame.shell.halfSize.x;

            advanceFor(testBubble, 0.19);
            bubbleFrame = testBubble.frame(SIZE);
            CHECK(bubbleFrame.shell.visibility > 0 && bubbleFrame.shell.holes[0].radius > 0);
            CHECK(bubbleFrame.window.visibility > 0);
            CHECK(bubbleFrame.window.halfSize.x == bubbleFrame.window.halfSize.y &&
                  bubbleFrame.window.corner == bubbleFrame.window.halfSize.x);
            CHECK(bubbleFrame.window.halfSize.x < bubbleFrame.shell.halfSize.x);
            CHECK(bubbleFrame.shell.halfSize.x > SHELL_RADIUS);

            for (int i = 0; i < 20; ++i) {
                bubbleFrame = testBubble.frame(SIZE);
                if (bubbleFrame.shell.visibility > 0) {
                    CHECK(bubbleFrame.shell.halfSize.x == bubbleFrame.shell.halfSize.y);
                    CHECK(bubbleFrame.shell.corner == bubbleFrame.shell.halfSize.x &&
                          bubbleFrame.shell.ripple == 0);
                    // The inner film is still clear while the outer bubble bursts: no window image
                    // pops in with it.
                    CHECK(bubbleFrame.window.content <= 0.1);
                }
                testBubble.advance(0.01);
            }
            advanceFor(testBubble, 0.14);
            bubbleFrame = testBubble.frame(SIZE);
            CHECK(bubbleFrame.shell.visibility == 0);
            CHECK(bubbleFrame.window.halfSize.x == SIZE.x / 2 &&
                  bubbleFrame.window.halfSize.y == SIZE.y / 2);
            CHECK(bubbleFrame.window.corner == 0);
        }
    }

    void flatEndpointAndSplat() {
        Bubble bubble;
        bubble.begin(Transition::Open, 1, 3, SIZE);
        bool overshoot = false;
        for (int i = 0; i < 100; ++i) {
            bubble.advance(0.01);
            const auto SPLAT_FRAME = bubble.frame(SIZE);
            overshoot |= SPLAT_FRAME.window.halfSize.x > SIZE.x / 2 &&
                         SPLAT_FRAME.window.halfSize.y > SIZE.y / 2;
            CHECK(SPLAT_FRAME.window.halfSize.x <= SIZE.x * 0.54 &&
                  SPLAT_FRAME.window.halfSize.y <= SIZE.y * 0.54);
        }
        const auto FRAME = bubble.frame(SIZE);
        CHECK(overshoot);
        CHECK(!bubble.active() && !bubble.closed());
        CHECK(FRAME.window.halfSize.x == SIZE.x / 2 && FRAME.window.halfSize.y == SIZE.y / 2);
        CHECK(FRAME.window.corner == 0 && FRAME.window.depth == 0 && FRAME.window.film == 0 &&
              FRAME.window.ripple == 0);
        CHECK(FRAME.window.content == 1 && FRAME.window.visibility == 1 &&
              FRAME.shell.visibility == 0);
        CHECK(FRAME.window.core.x == 0 && FRAME.window.core.y == 0);
    }

    void fluidCoreOutlivesTheLanding() {
        for (const Vec2 SIZE : {Vec2{.x = 1400, .y = 300}, Vec2{.x = 300, .y = 1400}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Open, 1, 3, SIZE);
            advanceFor(bubble, 0.64);
            const double IMPACT = bubble.frame(SIZE).window.core.x;
            CHECK(IMPACT > 0);
            advanceFor(bubble, 0.12);
            CHECK(
                bubble.frame(SIZE).window.core.x < 0);  // pressure recoils through the screen plane
            advanceFor(bubble, 0.02);
            const auto LANDED = bubble.frame(SIZE).window;
            CHECK(LANDED.halfSize.x == SIZE.x / 2 && LANDED.halfSize.y == SIZE.y / 2);
            CHECK(LANDED.corner == 0 && LANDED.depth == 0 && LANDED.ripple == 0);
            CHECK(LANDED.content == 1);
            CHECK(std::abs(LANDED.core.x) + std::abs(LANDED.core.y) > 0);
            advanceFor(bubble, 0.10);
            CHECK(bubble.frame(SIZE).window.core.x >
                  0);  // a smaller rebound after the boundary has stopped
            CHECK(bubble.frame(SIZE).window.core.x < IMPACT);
            advanceFor(bubble, 0.10);
            CHECK(std::abs(bubble.frame(SIZE).window.core.x) +
                      std::abs(bubble.frame(SIZE).window.core.y) <
                  IMPACT * 0.1);
            advanceFor(bubble, 0.02);
            CHECK(bubble.frame(SIZE).window.core.x == 0 && bubble.frame(SIZE).window.core.y == 0);
            bubble.begin(Transition::Close, 1, 9, SIZE);
            CHECK(bubble.frame(SIZE).window.core.x == 0 && bubble.frame(SIZE).window.core.y == 0);
        }
    }

    void checkSurfaceContinuity(const BubbleSurface &before, const BubbleSurface &after) {
        CHECK(before.halfSize.x == after.halfSize.x && before.halfSize.y == after.halfSize.y);
        CHECK(before.corner == after.corner && before.depth == after.depth);
        CHECK(before.core.x == after.core.x && before.core.y == after.core.y);
        CHECK(before.content == after.content && before.film == after.film);
        CHECK(before.visibility == after.visibility && before.ripple == after.ripple);
        if (before.closing) {
            CHECK(before.ruptureVisibility == after.ruptureVisibility);
            CHECK(before.clock == after.clock);
            CHECK(before.holeCount == after.holeCount);
            for (std::size_t i = 0; i < before.holeCount; ++i) {
                CHECK(before.holes[i].radius == after.holes[i].radius);
            }
        }
    }

    void interruptedCloseIsContinuous() {
        bool inheritedSheet = false;
        for (const double TIME : {0.0, 0.2, 0.38, 0.44, 0.55, 0.62, 0.85}) {
            Bubble bubble;
            bubble.begin(Transition::Open, 1, 9, SIZE);
            advanceFor(bubble, TIME);
            const auto BEFORE = bubble.frame(SIZE);
            bubble.begin(Transition::Close, .42, 11, SIZE);
            const auto AFTER = bubble.frame(SIZE);
            CHECK(bubble.active() && !bubble.opening());
            CHECK(BEFORE.seed == AFTER.seed);
            checkSurfaceContinuity(BEFORE.window, AFTER.window);
            checkSurfaceContinuity(BEFORE.shell, AFTER.shell);
            CHECK(AFTER.window.ruptureVisibility == BEFORE.window.visibility);
            if (TIME == .38) {
                CHECK(AFTER.window.ruptureVisibility > 0 && AFTER.window.ruptureVisibility < 1);
            }
            std::array<BubbleParticle, kBubbleMaxParticles> beforeParticles{};
            std::array<BubbleParticle, kBubbleMaxParticles> afterParticles{};
            const auto BEFORE_COUNT = bubbleParticles(BEFORE.shell, beforeParticles);
            const auto AFTER_COUNT = bubbleParticles(AFTER.shell, afterParticles);
            CHECK(BEFORE_COUNT == AFTER_COUNT);
            for (std::size_t i = 0; i < std::min(BEFORE_COUNT, AFTER_COUNT); ++i) {
                CHECK(beforeParticles[i].centre.x == afterParticles[i].centre.x &&
                      beforeParticles[i].centre.y == afterParticles[i].centre.y);
                CHECK(beforeParticles[i].radius == afterParticles[i].radius &&
                      beforeParticles[i].tail.x == afterParticles[i].tail.x &&
                      beforeParticles[i].tail.y == afterParticles[i].tail.y);
            }
            std::array<BubbleSheet, kBubbleMaxSheets> originalSheets{};
            std::array<BubbleSheet, kBubbleMaxSheets> inheritedSheets{};
            const auto SHEET_COUNT = bubbleSheets(BEFORE.shell, originalSheets);
            CHECK(bubbleSheets(AFTER.shell, inheritedSheets) == SHEET_COUNT);
            for (std::size_t i = 0; i < SHEET_COUNT; ++i) {
                const auto &old = originalSheets[i];
                const auto &inherited = inheritedSheets[i];
                CHECK(old.id == inherited.id && old.birth == inherited.birth);
                CHECK(old.age == inherited.age && old.scale == inherited.scale);
                const auto BEFORE_SAMPLE = bubbleSheetSample(BEFORE.shell, old, old.phi, .5);
                const auto AFTER_SAMPLE =
                    bubbleSheetSample(AFTER.shell, inherited, inherited.phi, .5);
                CHECK(BEFORE_SAMPLE.position.x == AFTER_SAMPLE.position.x &&
                      BEFORE_SAMPLE.position.y == AFTER_SAMPLE.position.y &&
                      BEFORE_SAMPLE.position.z == AFTER_SAMPLE.position.z);
                inheritedSheet |= old.scale > 0;
            }
            Bubble progressingSheets = bubble;
            advanceFor(progressingSheets, .025);
            const auto PROGRESSED = progressingSheets.frame(SIZE).shell;
            const auto PROGRESSED_COUNT = bubbleSheets(PROGRESSED, inheritedSheets);
            CHECK(PROGRESSED.ruptureVisibility == AFTER.shell.ruptureVisibility);
            for (std::size_t i = 0; i < SHEET_COUNT; ++i) {
                for (std::size_t j = 0; j < PROGRESSED_COUNT; ++j) {
                    if (originalSheets[i].id == inheritedSheets[j].id) {
                        CHECK(
                            std::abs(inheritedSheets[j].age - originalSheets[i].age - .025) < 1e-8);
                    }
                }
            }
            if (TIME == 0.44 || TIME == 0.55 || TIME == 0.62) {
                CHECK(BEFORE.window.visibility > 0 && BEFORE.shell.holes[0].radius > 0);
                advanceFor(bubble, 0.05);
                const auto PROGRESSING = bubble.frame(SIZE);
                CHECK(PROGRESSING.shell.holes[0].radius >= BEFORE.shell.holes[0].radius);
            } else if (TIME <= 0.2) {
                CHECK(BEFORE.window.visibility == 0);
                advanceFor(bubble, 0.1);
                CHECK(bubble.frame(SIZE).window.visibility == 0);
            }
        }
        CHECK(inheritedSheet);
    }

    void interruptedResizeKeepsShellCircular() {
        Bubble bubble;
        bubble.begin(Transition::Open, 1, 9, SIZE);
        advanceFor(bubble, 0.44);
        bubble.begin(Transition::Close, 1, 11, SIZE);
        const auto ORIGINAL = bubble.frame(SIZE);
        const auto RESIZED = bubble.frame({.x = SIZE.x * 1.5, .y = SIZE.y * 0.5});
        CHECK(ORIGINAL.shell.visibility > 0 && ORIGINAL.shell.holes[0].radius > 0);
        CHECK(RESIZED.shell.halfSize.x == ORIGINAL.shell.halfSize.x * 0.5);
        CHECK(RESIZED.shell.halfSize.y == ORIGINAL.shell.halfSize.y * 0.5);
        CHECK(RESIZED.shell.corner == ORIGINAL.shell.corner * 0.5);
        CHECK(RESIZED.shell.holes[0].centre.x == ORIGINAL.shell.holes[0].centre.x);
        CHECK(std::abs(RESIZED.shell.holes[0].radius - ORIGINAL.shell.holes[0].radius) < 1e-9);
        CHECK(RESIZED.window.halfSize.x == ORIGINAL.window.halfSize.x * 1.5);
        CHECK(RESIZED.window.halfSize.y == ORIGINAL.window.halfSize.y * 0.5);
        CHECK(RESIZED.window.holes[0].centre.x == ORIGINAL.window.holes[0].centre.x * 1.5);
        CHECK(RESIZED.window.holes[0].centre.y == ORIGINAL.window.holes[0].centre.y * .5);
        CHECK(RESIZED.window.speed == ORIGINAL.window.speed * 1.5);
        CHECK(bubble.frame(SIZE).window.holes[0].radius == ORIGINAL.window.holes[0].radius);
    }

    void ruptureCoversCorners() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Close, 1, 19, SIZE);
            auto film = bubble.frame(SIZE).window;
            CHECK(film.visibility == 1 && bubble.frame(SIZE).shell.visibility == 0);
            CHECK(film.holes[0].centre.x != 0 && film.holes[0].centre.y != 0);
            CHECK(film.holes[0].radius == 0);
            advanceFor(bubble, 0.9);
            film = bubble.frame(SIZE).window;
            const auto &hole = film.holes[0];
            const double FARTHEST = std::hypot(
                (SIZE.x / 2) + std::abs(hole.centre.x), (SIZE.y / 2) + std::abs(hole.centre.y));
            CHECK(hole.radius * .95 >= FARTHEST);
            advanceFor(bubble, 0.2);
            CHECK(bubble.closed() && !bubble.active());
            CHECK(bubble.frame(SIZE).window.visibility == 0 &&
                  bubble.frame(SIZE).shell.visibility == 0);
            const Box BOUNDS = bubble.bounds(SIZE);
            CHECK(BOUNDS.w == 0 && BOUNDS.h == 0);
        }
    }

    void checkSurfaceBounds(const Box &box, Vec2 size, const BubbleSurface &surface) {
        if (surface.visibility <= 0) {
            return;
        }
        CHECK(box.x <= size.x / 2 - surface.halfSize.x - std::abs(surface.ripple));
        CHECK(box.y <= size.y / 2 - surface.halfSize.y - std::abs(surface.ripple));
        CHECK(box.x + box.w >= size.x / 2 + surface.halfSize.x + std::abs(surface.ripple));
        CHECK(box.y + box.h >= size.y / 2 + surface.halfSize.y + std::abs(surface.ripple));
    }

    void boundsContainTheSurfaces() {
        for (Vec2 size : {Vec2{.x = 1400, .y = 300}, Vec2{.x = 300, .y = 1400}, SIZE}) {
            const auto check = [&](Transition direction) {
                Bubble bubble;
                bubble.begin(direction, 1, 8, size);
                const auto INITIAL = bubble.frame(size);
                if (direction == Transition::Open) {
                    CHECK(INITIAL.window.visibility == 0 && INITIAL.shell.visibility == 0);
                }
                CHECK(bubble.bounds(size).w > 0 && bubble.bounds(size).h > 0);
                for (int i = 0; i < 100; ++i) {
                    const auto FRAME = bubble.frame(size);
                    const auto BOX = bubble.bounds(size);
                    checkSurfaceBounds(BOX, size, FRAME.window);
                    checkSurfaceBounds(BOX, size, FRAME.shell);
                    std::array<BubbleParticle, kBubbleMaxParticles> particles{};
                    for (const auto *surface : {&FRAME.window, &FRAME.shell}) {
                        const auto COUNT = bubbleParticles(*surface, particles);
                        for (std::size_t j = 0; j < COUNT; ++j) {
                            const auto &particle = particles[j];
                            CHECK(BOX.x <= size.x / 2 + particle.centre.x - particle.radius);
                            CHECK(
                                BOX.x + BOX.w >= size.x / 2 + particle.centre.x + particle.radius);
                            CHECK(BOX.y <= size.y / 2 + particle.centre.y - particle.radius);
                            CHECK(
                                BOX.y + BOX.h >= size.y / 2 + particle.centre.y + particle.radius);
                            CHECK(BOX.x <= size.x / 2 + particle.tail.x - particle.tailRadius);
                            CHECK(BOX.x + BOX.w >=
                                  size.x / 2 + particle.tail.x + particle.tailRadius);
                            CHECK(BOX.y <= size.y / 2 + particle.tail.y - particle.tailRadius);
                            CHECK(BOX.y + BOX.h >=
                                  size.y / 2 + particle.tail.y + particle.tailRadius);
                        }
                    }
                    bubble.advance(0.01);
                }
            };
            check(Transition::Open);
            check(Transition::Close);
        }
    }

    void wideWindowsKeepTightVerticalBounds() {
        Bubble normal;
        Bubble wide;
        normal.begin(Transition::Close, 1, 8, {.x = 600, .y = 200});
        wide.begin(Transition::Close, 1, 8, {.x = 1800, .y = 200});
        advanceFor(normal, 0.6);
        advanceFor(wide, 0.6);
        CHECK(normal.bounds({600, 200}).h == wide.bounds({1800, 200}).h);
    }

    void localizedRuptureAndCoverage() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Close, .42, 19, SIZE);
            advanceFor(bubble, .42 * .149);
            auto surface = bubble.frame(SIZE).window;
            CHECK(surface.holes[0].radius == 0);
            CHECK(surface.holes[0].precursor > .9);
            CHECK(surface.film == 0);
            for (std::size_t i = 1; i < surface.holeCount; ++i) {
                Bubble probe;
                probe.begin(Transition::Close, .42, 19, SIZE);
                advanceFor(probe, .42 * surface.holes[i].start);
                const auto AT_BIRTH = probe.frame(SIZE).window;
                CHECK(AT_BIRTH.holes[i].start > AT_BIRTH.holes[0].start);
                for (std::size_t j = 0; j < i; ++j) {
                    const auto &priorHole = AT_BIRTH.holes[j];
                    const auto CENTRE = AT_BIRTH.holes[i].centre;
                    CHECK(
                        std::hypot(CENTRE.x - priorHole.centre.x, CENTRE.y - priorHole.centre.y) >
                        bubbleHoleBoundary(priorHole, bubbleHoleAzimuth(priorHole, CENTRE, false)));
                }
            }
            advanceFor(bubble, .42 * .21);
            const auto FIRST = bubble.frame(SIZE).window;
            advanceFor(bubble, .42 * .10);
            const auto SECOND = bubble.frame(SIZE).window;
            CHECK(std::abs((SECOND.holes[0].radius - FIRST.holes[0].radius) - SECOND.speed * .1) <
                  SECOND.speed * .006);
            advanceFor(bubble, .42 * .151);
            surface = bubble.frame(SIZE).window;
            for (int row = -5; row <= 5; ++row) {
                for (int col = -5; col <= 5; ++col) {
                    const Vec3 POINT{.x = SIZE.x * .1 * col, .y = SIZE.y * .1 * row, .z = 0};
                    bool covered = false;
                    for (std::size_t i = 0; i < surface.holeCount; ++i) {
                        const auto &hole = surface.holes[i];
                        covered |= std::hypot(POINT.x - hole.centre.x, POINT.y - hole.centre.y) <=
                                   bubbleHoleBoundary(hole, bubbleHoleAzimuth(hole, POINT, false));
                    }
                    CHECK(covered);
                }
            }
        }
    }

    void precursorAndRemnants() {
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 42, SIZE);
        auto surface = bubble.frame(SIZE).window;
        CHECK(surface.film == 0 && surface.thickness >= 380 && surface.thickness <= 650);
        advanceFor(bubble, .42 * .02);
        CHECK(bubble.frame(SIZE).window.holes[0].precursor > 0);
        advanceFor(bubble, .42 * .13);
        CHECK(bubble.frame(SIZE).window.holes[0].radius < 1e-8);
        advanceFor(bubble, 1.0 / 60);
        surface = bubble.frame(SIZE).window;
        CHECK(surface.holes[0].radius < .05 * std::min(surface.halfSize.x, surface.halfSize.y));
        advanceFor(bubble, .42 * .20);
        surface = bubble.frame(SIZE).window;
        std::array<BubbleParticle, kBubbleMaxParticles> particles{};
        auto count = bubbleParticles(surface, particles);
        std::size_t ligaments = 0;
        for (std::size_t i = 0; i < count; ++i) {
            ligaments += static_cast<std::size_t>(particles[i].tailRadius > 0);
        }
        CHECK(ligaments > 0);
        advanceFor(bubble, .42 * .42);
        surface = bubble.frame(SIZE).window;
        count = bubbleParticles(surface, particles);
        CHECK(count > 0);
        advanceFor(bubble, .42 * .2);
        CHECK(bubble.closed());
        CHECK(bubbleParticles(bubble.frame(SIZE).window, particles) == 0);
    }

    void sphericalCoverage() {
        constexpr double PI_VALUE = 3.14159265358979323846;
        for (std::uint64_t seed = 0; seed < 32; ++seed) {
            Bubble bubble;
            bubble.begin(Transition::Open, .62, seed, SIZE);
            advanceFor(bubble, .62 * .55);
            const auto SHELL = bubble.frame(SIZE).shell;
            for (int row = 0; row <= 20; ++row) {
                const double HEIGHT = 1 - (row * .1);
                const double RADIAL = std::sqrt(std::max(0.0, 1 - (HEIGHT * HEIGHT)));
                for (int col = 0; col < 40; ++col) {
                    const double ANGLE = 2 * PI_VALUE * col / 40;
                    const Vec3 POINT{
                        .x = RADIAL * std::cos(ANGLE), .y = RADIAL * std::sin(ANGLE), .z = HEIGHT};
                    bool covered = false;
                    for (std::size_t i = 0; i < SHELL.holeCount; ++i) {
                        const auto &hole = SHELL.holes[i];
                        const double DISTANCE = std::acos(
                            std::clamp((POINT.x * hole.centre.x) + (POINT.y * hole.centre.y) +
                                           (POINT.z * hole.centre.z),
                                -1.0,
                                1.0));
                        covered |= DISTANCE <=
                                   bubbleHoleBoundary(hole, bubbleHoleAzimuth(hole, POINT, true));
                    }
                    CHECK(covered);
                }
            }
        }
    }

    void seededFlatCoverage() {
        for (const Vec2 SIZE : {Vec2{.x = 1800, .y = 200}, Vec2{.x = 200, .y = 1800}, SIZE}) {
            for (std::uint64_t seed = 0; seed < 16; ++seed) {
                Bubble bubble;
                bubble.begin(Transition::Close, .42, seed, SIZE);
                advanceFor(bubble, .42 * .62);
                const auto SURFACE = bubble.frame(SIZE).window;
                for (int row = -3; row <= 3; ++row) {
                    for (int col = -3; col <= 3; ++col) {
                        const Vec3 POINT{.x = col * SIZE.x / 6, .y = row * SIZE.y / 6, .z = 0};
                        bool covered = false;
                        for (std::size_t i = 0; i < SURFACE.holeCount; ++i) {
                            const auto &hole = SURFACE.holes[i];
                            covered |=
                                std::hypot(POINT.x - hole.centre.x, POINT.y - hole.centre.y) <=
                                bubbleHoleBoundary(hole, bubbleHoleAzimuth(hole, POINT, false));
                        }
                        CHECK(covered);
                    }
                }
            }
        }
    }

    void impactsUseAnimationTime() {
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 19, SIZE);
        const auto FIRST = bubble.frame(SIZE).window;
        std::array<BubbleImpact, kBubbleMaxImpacts> firstImpacts{};
        std::array<BubbleImpact, kBubbleMaxImpacts> laterImpacts{};
        const auto FIRST_COUNT = bubbleImpacts(FIRST, firstImpacts);
        CHECK(FIRST_COUNT <= kBubbleMaxImpacts);
        advanceFor(bubble, .01);
        const auto LATER_COUNT = bubbleImpacts(bubble.frame(SIZE).window, laterImpacts);
        CHECK(FIRST_COUNT == LATER_COUNT);
        for (std::size_t i = 0; i < FIRST_COUNT; ++i) {
            CHECK(std::abs((firstImpacts[i].delay - laterImpacts[i].delay) - .01) < 1e-10);
            CHECK(firstImpacts[i].position.x == laterImpacts[i].position.x &&
                  firstImpacts[i].position.y == laterImpacts[i].position.y);
        }
    }

    void impactsFollowDroplets() {
        for (const auto SEED : {42ULL, 7ULL, 99ULL, 1234ULL}) {
            for (const auto DIRECTION : {Transition::Open, Transition::Close}) {
                const bool OPENING = DIRECTION == Transition::Open;
                Bubble bubble;
                bubble.begin(DIRECTION, OPENING ? .62 : .42, SEED, {.x = 900, .y = 560});
                if (OPENING) {
                    advanceFor(bubble, .62 * .31);
                }
                auto frame = bubble.frame({.x = 900, .y = 560});
                auto surface = OPENING ? frame.shell : frame.window;
                std::array<BubbleImpact, kBubbleMaxImpacts> impacts{};
                const auto COUNT = bubbleImpacts(surface, impacts);
                CHECK(COUNT <= (OPENING ? 3U : 2U));
                if (OPENING) {
                    CHECK(COUNT >= 1);
                }
                for (std::size_t i = 0; i < COUNT; ++i) {
                    CHECK(impacts[i].radius >= 1.5 && impacts[i].radius <= 3.5);
                }
                if (COUNT == 0u) {
                    continue;
                }
                CHECK(impacts[0].delay > .00002);
                advanceFor(bubble, impacts[0].delay - .00001);
                frame = bubble.frame({.x = 900, .y = 560});
                surface = OPENING ? frame.shell : frame.window;
                std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
                const auto BEFORE = detail::bubbleParticlesWithIds(surface, particles);
                std::uint32_t source = 0;
                bool found = false;
                for (std::size_t i = 0; i < BEFORE; ++i) {
                    const auto &drop = particles[i];
                    if (drop.particle.blur > .95 &&
                        std::hypot(drop.particle.centre.x - impacts[0].position.x,
                            drop.particle.centre.y - impacts[0].position.y) < 1) {
                        source = drop.id;
                        found = true;
                        break;
                    }
                }
                CHECK(found);
                advanceFor(bubble, .00002);
                frame = bubble.frame({.x = 900, .y = 560});
                surface = OPENING ? frame.shell : frame.window;
                const auto AFTER = detail::bubbleParticlesWithIds(surface, particles);
                for (std::size_t i = 0; found && i < AFTER; ++i) {
                    CHECK(particles[i].id != source);
                }
            }
        }
    }

    void particleContinuityAndEvaporation() {
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 19, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> previous{};
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> current{};
        std::size_t oldCount = 0;
        std::size_t peak = 0;
        bool burst = false;
        for (int frame = 0; frame < 101; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubbleParticlesWithIds(SURFACE, current);
            peak = std::max(peak, COUNT);
            for (std::size_t i = 0; i < COUNT; ++i) {
                const auto &currentParticle = current[i];
                burst |= currentParticle.id >= 1000 && currentParticle.id < 2000;
                for (std::size_t j = 0; j < oldCount; ++j) {
                    if (previous[j].id != currentParticle.id) {
                        continue;
                    }
                    const auto &priorParticle = previous[j].particle;
                    const bool ATTACHED = currentParticle.id >= 2000 && currentParticle.id < 3000;
                    // Attached ligaments follow the rolling film, not just the puncture front.
                    const double SPEED_LIMIT = SURFACE.speed * (ATTACHED ? 2 : 1);
                    CHECK(std::hypot(currentParticle.particle.centre.x - priorParticle.centre.x,
                              currentParticle.particle.centre.y - priorParticle.centre.y) <=
                          SPEED_LIMIT * .01 + 2);
                    if (ATTACHED) {
                        CHECK(currentParticle.particle.radius + 1e-9 >= priorParticle.radius);
                    } else if (currentParticle.particle.blur == 0 && priorParticle.blur == 0) {
                        CHECK(currentParticle.particle.radius <= priorParticle.radius + 1e-9);
                    }
                }
            }
            previous = current;
            oldCount = COUNT;
            bubble.advance(.0042);
        }
        CHECK(peak > 0 && peak <= kBubbleMaxParticles);
        CHECK(burst);
        CHECK(oldCount == 0);
    }

    void fingersStartGradually() {
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 42, SIZE);
        advanceFor(bubble, .42 * .17);
        auto surface = bubble.frame(SIZE).window;
        CHECK(surface.holes[0].radius < std::max(24.0, .08 * surface.halfSize.y));
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
        CHECK(detail::bubbleParticlesWithIds(surface, particles) == 0);
        Bubble shell;
        shell.begin(Transition::Open, .62, 42, SIZE);
        advanceFor(shell, .62 * .32);
        const auto EARLY_SHELL = shell.frame(SIZE).shell;
        CHECK(EARLY_SHELL.holes[0].radius * EARLY_SHELL.halfSize.x <
              std::max(24.0, .08 * EARLY_SHELL.halfSize.x));
        CHECK(detail::bubbleParticlesWithIds(EARLY_SHELL, particles) == 0);
        bool varied = false;
        bool satellite = false;
        for (int frame = 0; frame < 20; ++frame) {
            bubble.advance(.005);
            surface = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubbleParticlesWithIds(surface, particles);
            double shortest = 1e9;
            double longest = 0;
            std::size_t forming = 0;
            for (std::size_t i = 0; i < COUNT; ++i) {
                if (particles[i].id >= 10000) {
                    const auto SOURCE = (particles[i].id - 10000) / 2;
                    for (std::size_t j = 0; j < COUNT; ++j) {
                        if (particles[j].id != SOURCE) {
                            continue;
                        }
                        satellite = true;
                        CHECK(particles[i].particle.radius < particles[j].particle.radius);
                    }
                }
                const auto &drop = particles[i];
                if (drop.id < 2000 || drop.id >= 3000) {
                    continue;
                }
                ++forming;
                CHECK(drop.particle.radius > drop.particle.tailRadius);
                const double LENGTH = std::hypot(drop.particle.centre.x - drop.particle.tail.x,
                    drop.particle.centre.y - drop.particle.tail.y);
                shortest = std::min(shortest, LENGTH);
                longest = std::max(longest, LENGTH);
            }
            varied |= forming >= 3 && longest - shortest > 3;
        }
        CHECK(varied && satellite);
    }

    void curledGeometryAndAttachments() {
        BubbleSurface surface;
        surface.halfSize = {.x = 300, .y = 200};
        surface.aureole = 1;
        BubbleHole hole;
        hole.phase = {.2, -.4, 0, 0};
        const auto SECTION = bubbleCurlSection(surface, hole, 100, .7);
        const Vec3 BASE{.x = 100, .y = 20, .z = 0};
        const Vec3 OUTWARD{.x = 1, .y = 0, .z = 0};
        const Vec3 NORMAL{.x = 0, .y = 0, .z = 1};
        const auto ROOT = bubbleCurlSample(BASE, OUTWARD, NORMAL, SECTION, 0);
        CHECK(ROOT.position.x == BASE.x && ROOT.position.y == BASE.y && ROOT.position.z == BASE.z);
        CHECK(ROOT.normal.x == 0 && ROOT.normal.y == 0 && ROOT.normal.z == 1);
        for (int i = 0; i <= 20; ++i) {
            const auto SAMPLE = bubbleCurlSample(BASE, OUTWARD, NORMAL, SECTION, i / 20.0);
            CHECK(std::abs(std::hypot(SAMPLE.normal.x, SAMPLE.normal.z) - 1) < 1e-12);
            CHECK(std::hypot(SAMPLE.position.x - BASE.x, SAMPLE.position.z - BASE.z) <=
                  SECTION.width + 1e-9);
        }
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 42, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
        bool attached = false;
        for (int frame = 0; frame < 90; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubbleParticlesWithIds(SURFACE, particles);
            for (std::size_t i = 0; i < COUNT; ++i) {
                const auto &drop = particles[i];
                if (drop.id < 2000 || drop.id >= 3000) {
                    continue;
                }
                const std::size_t HOLE_INDEX = (drop.id - 2000) / 104;
                const auto &sourceHole = SURFACE.holes[HOLE_INDEX];
                const double PHI = std::atan2(drop.particle.tail.y - sourceHole.centre.y,
                    drop.particle.tail.x - sourceHole.centre.x);
                const auto EXPECTED = bubblePeelSample(SURFACE, sourceHole, PHI, 1);
                CHECK(std::hypot(EXPECTED.position.x - drop.particle.tail.x,
                          EXPECTED.position.y - drop.particle.tail.y) < 1e-6);
                attached = true;
            }
            bubble.advance(.0042);
        }
        CHECK(attached);
    }

    void sheetsContract() {
        Bubble bubble;
        bubble.begin(Transition::Close, 2.0, 19, SIZE);
        std::array<BubbleSheet, kBubbleMaxSheets> sheets{};
        std::array<BubbleSheet, kBubbleMaxSheets> again{};
        bool born = false;
        bool moved = false;
        bool expired = false;
        std::array<std::uint32_t, kBubbleMaxSheets> retired{};
        std::size_t retiredCount = 0;
        for (int frame = 0; frame < 999; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = bubbleSheets(SURFACE, sheets);
            CHECK(COUNT <= kBubbleMaxSheets);
            CHECK(bubbleSheets(SURFACE, again) == COUNT);
            for (std::size_t i = 0; i < COUNT; ++i) {
                const auto &sheet = sheets[i];
                CHECK(sheet.id == again[i].id && sheet.birth == again[i].birth);
                for (std::size_t j = 0; j < retiredCount; ++j) {
                    if (retired[j] == sheet.id) {
                        CHECK(sheet.scale == 0);
                    }
                }
                if (sheet.scale == 0 &&
                    std::find(retired.begin(), retired.begin() + retiredCount, sheet.id) ==
                        retired.begin() + retiredCount) {
                    retired[retiredCount++] = sheet.id;
                }
                CHECK(sheet.halfAngle > 0 && sheet.halfAngle <= .35);
                CHECK(sheet.scale >= 0 && sheet.scale <= 1);
                auto birth = SURFACE;
                birth.clock = sheet.birth;
                std::array<BubbleSheet, kBubbleMaxSheets> atBirth{};
                const auto BIRTH_COUNT = bubbleSheets(birth, atBirth);
                for (std::size_t j = 0; j < BIRTH_COUNT; ++j) {
                    if (atBirth[j].id != sheet.id) {
                        continue;
                    }
                    auto birthHole = birth.holes[sheet.hole];
                    birthHole.radius = sheet.radii[sheet.hole];
                    for (std::size_t mode = 0; mode < birthHole.amplitude.size(); ++mode) {
                        birthHole.amplitude[mode] =
                            sheet.onsets[sheet.hole] * (.014 - (.002 * mode));
                    }
                    const auto SOURCE = bubblePeelSample(birth, birthHole, sheet.phi, .5);
                    const auto DETACHED = bubbleSheetSample(birth, atBirth[j], sheet.phi, .5);
                    CHECK(std::hypot(SOURCE.position.x - DETACHED.position.x,
                              SOURCE.position.y - DETACHED.position.y) < 1e-8);
                    CHECK(std::abs(SOURCE.position.z - DETACHED.position.z) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.x - DETACHED.normal.x) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.y - DETACHED.normal.y) < 1e-8);
                    CHECK(std::abs(SOURCE.normal.z - DETACHED.normal.z) < 1e-8);
                    const double BOUNDARY = bubbleHoleBoundary(birthHole, sheet.phi);
                    const double WIDTH =
                        bubbleCurlSection(birth, birthHole, BOUNDARY, sheet.phi).width;
                    const Vec3 MATERIAL{
                        .x = birthHole.centre.x + ((BOUNDARY + (WIDTH * .5)) * std::cos(sheet.phi)),
                        .y = birthHole.centre.y + ((BOUNDARY + (WIDTH * .5)) * std::sin(sheet.phi)),
                        .z = 0};
                    CHECK(bubbleSheetContains(birth, sheet, MATERIAL));
                    born = true;
                }
                moved |= sheet.age > 0 && sheet.scale < 1 && sheet.scale > 0 &&
                         std::hypot(sheet.translation.x, sheet.translation.y) > 0;
                expired |= sheet.scale == 0;
                const auto BOX = bubble.bounds(SIZE);
                for (const double OFFSET : {-1.0, 0.0, 1.0}) {
                    for (const double FRACTION : {0.0, .5, 1.0}) {
                        const auto POINT = bubbleSheetSample(
                            SURFACE, sheet, sheet.phi + (OFFSET * sheet.halfAngle), FRACTION)
                                               .position;
                        CHECK(POINT.x + SIZE.x * .5 >= BOX.x &&
                              POINT.x + SIZE.x * .5 <= BOX.x + BOX.w);
                        CHECK(POINT.y + SIZE.y * .5 >= BOX.y &&
                              POINT.y + SIZE.y * .5 <= BOX.y + BOX.h);
                    }
                }
            }
            bubble.advance(.002);
        }
        CHECK(born && moved && expired);
    }

    void pinchBirthIsContinuous() {
        Bubble bubble;
        bubble.begin(Transition::Close, .42, 42, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> previous{};
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> current{};
        std::size_t oldCount = 0;
        bool observed = false;
        for (int frame = 0; frame < 2000; ++frame) {
            const auto SURFACE = bubble.frame(SIZE).window;
            const auto COUNT = detail::bubbleParticlesWithIds(SURFACE, current);
            for (std::size_t i = 0; i < COUNT; ++i) {
                if (current[i].id >= 2000) {
                    continue;
                }
                for (std::size_t j = 0; j < oldCount; ++j) {
                    if (previous[j].id != current[i].id + 2000) {
                        continue;
                    }
                    const auto &priorParticle = previous[j].particle;
                    const auto &currentParticle = current[i].particle;
                    CHECK(std::hypot(priorParticle.centre.x - currentParticle.centre.x,
                              priorParticle.centre.y - currentParticle.centre.y) <
                          SURFACE.speed * SURFACE.rate * .00021 + 1);
                    CHECK(std::hypot(priorParticle.tail.x - currentParticle.tail.x,
                              priorParticle.tail.y - currentParticle.tail.y) <
                          SURFACE.speed * SURFACE.rate * .00021 + 1);
                    observed = true;
                }
            }
            previous = current;
            oldCount = COUNT;
            bubble.advance(.00021);
        }
        CHECK(observed);
    }

    void sheetsKeepTheirSourceMaterial() {
        BubbleSurface surface;
        surface.halfSize = {.x = 200, .y = 200};
        surface.holeCount = 2;
        surface.aureole = 1;
        surface.holes[1].centre = {.x = 20, .y = 0, .z = 0};
        BubbleSheet sheet;
        sheet.hole = 0;
        sheet.halfAngle = .35;
        sheet.radii = {100, 100, 0, 0};
        // Both annuli cover this point, but the second hole owns it.
        CHECK(!bubbleSheetContains(surface, sheet, {125, 0, 0}));
        sheet.phi = 3.141592653589793;
        CHECK(bubbleSheetContains(surface, sheet, {-125, 0, 0}));
        // Later front motion cannot change which original material detached.
        surface.holes[0].radius = 500;
        surface.holes[1].radius = 0;
        CHECK(bubbleSheetContains(surface, sheet, {-125, 0, 0}));
        sheet.phi = 0;
        CHECK(!bubbleSheetContains(surface, sheet, {125, 0, 0}));
    }

    void deterministicAndInvalidInput() {
        Bubble firstBubble;
        Bubble bubble;
        firstBubble.begin(Transition::Open, 1, 55, SIZE);
        bubble.begin(Transition::Open, 1, 55, SIZE);
        advanceFor(firstBubble, 0.84, 0.01);
        advanceFor(bubble, 0.84, 0.02);
        const auto FIRST_FRAME = firstBubble.frame(SIZE);
        const auto SECOND_FRAME = bubble.frame(SIZE);
        CHECK(std::abs(FIRST_FRAME.window.halfSize.x - SECOND_FRAME.window.halfSize.x) < 1e-9);
        CHECK(std::abs(FIRST_FRAME.window.depth - SECOND_FRAME.window.depth) < 1e-9 &&
              FIRST_FRAME.seed == SECOND_FRAME.seed);
        CHECK(std::abs(FIRST_FRAME.window.core.x - SECOND_FRAME.window.core.x) < 1e-9 &&
              std::abs(FIRST_FRAME.window.core.y - SECOND_FRAME.window.core.y) < 1e-9);
        firstBubble.advance(-1);
        firstBubble.advance(std::numeric_limits<double>::quiet_NaN());
        CHECK(firstBubble.frame(SIZE).window.halfSize.x == FIRST_FRAME.window.halfSize.x);
        firstBubble.advance(30);
        CHECK(std::isfinite(firstBubble.frame(SIZE).window.halfSize.x));
        firstBubble.reset();
        CHECK(!firstBubble.active() && !firstBubble.closed());
        firstBubble.begin(Transition::Close, 0, 1, SIZE);
        CHECK(!firstBubble.active() && !firstBubble.closed());
        firstBubble.begin(Transition::Open, 1, 1, {.x = 0, .y = 100});
        CHECK(!firstBubble.active());
        const Box INVALID =
            firstBubble.bounds({.x = std::numeric_limits<double>::infinity(), .y = 2});
        CHECK(INVALID.w == 0 && INVALID.h == 0);
    }
}  // namespace

int main() {
    separateSurfacesDuringOpening();
    flatEndpointAndSplat();
    fluidCoreOutlivesTheLanding();
    interruptedCloseIsContinuous();
    interruptedResizeKeepsShellCircular();
    ruptureCoversCorners();
    boundsContainTheSurfaces();
    wideWindowsKeepTightVerticalBounds();
    deterministicAndInvalidInput();
    localizedRuptureAndCoverage();
    impactsUseAnimationTime();
    impactsFollowDroplets();
    precursorAndRemnants();
    sphericalCoverage();
    seededFlatCoverage();
    particleContinuityAndEvaporation();
    fingersStartGradually();
    curledGeometryAndAttachments();
    sheetsContract();
    sheetsKeepTheirSourceMaterial();
    pinchBirthIsContinuous();
    return g_failures == 0 ? 0 : 1;
}
