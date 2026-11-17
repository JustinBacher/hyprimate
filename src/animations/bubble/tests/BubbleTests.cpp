#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleCurl.hpp"
#include "animations/bubble/BubbleRupture.hpp"
#include "animations/bubble/BubbleSheets.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{1000, 700};

    void advanceFor(Bubble &b, double seconds, double step = 0.01) {
        while (seconds > 1e-12) {
            const double dt = std::min(seconds, step);
            b.advance(dt);
            seconds -= dt;
        }
    }

    void formationDuringOpening() {
        for (Vec2 size : {Vec2{320, 900}, Vec2{1100, 320}, Vec2{640, 640}, SIZE}) {
            Bubble b;
            b.begin(Transition::Open, 1, 42, size);
            auto previous = b.frame(size);
            const double shortHalf = std::min(size.x, size.y) / 2;
            bool contact = false, deformed = false, lingered = false, lateBirth = false;
            bool shortFirst = size.x == size.y;
            bool earlyMorph = false, grewFromCompanion = false;
            double emerged = -1;
            std::size_t peakEarly = 0, births = 0;
            double smallest = std::numeric_limits<double>::infinity(), largest = 0;
            for (int i = 1; i <= 1000; ++i) {
                b.advance(.001);
                const auto f = b.frame(size);
                CHECK(f.formationCount <= f.formation.size());
                CHECK(f.shell.visibility == 0 && f.window.holeCount == 0);
                // Visibility, size and position of the window bubble change continuously.
                CHECK(std::abs(f.window.visibility - previous.window.visibility) < .05);
                CHECK(std::abs(f.window.content - previous.window.content) < .05);
                if (previous.window.visibility > 0) {
                    CHECK(std::abs(f.window.halfSize.x - previous.window.halfSize.x) < size.x * .03);
                    CHECK(std::abs(f.window.halfSize.y - previous.window.halfSize.y) < size.y * .03);
                    CHECK(std::abs(f.window.corner - previous.window.corner) < std::max(size.x, size.y) * .03);
                }
                if (emerged < 0 && f.window.visibility > 0) {
                    emerged = i / 1000.0;
                    // It is the film it grew from, drawn at the same place and size while its
                    // membrane takes over, and that film was already there a moment before.
                    const auto &origin = f.formation[kBubbleFoamCapacity];
                    CHECK(f.formationCount == kBubbleFormationCapacity && origin.visibility > .9);
                    CHECK(std::abs(origin.radius - f.window.halfSize.x) < 1e-6 * size.x);
                    CHECK(origin.centre.x == f.window.motion.carry.x && origin.centre.y == f.window.motion.carry.y);
                    for (std::size_t j = 0; j < previous.formationCount; ++j) {
                        const auto &p = previous.formation[j];
                        if (p.visibility <= 0) continue;
                        const double gap = std::hypot(p.centre.x - origin.centre.x, p.centre.y - origin.centre.y);
                        grewFromCompanion |= gap < origin.radius * .2 && p.radius > origin.radius * .8;
                    }
                }
                if (f.window.visibility > 0) {
                    const double xEdge = f.window.halfSize.x + std::abs(f.window.motion.carry.x);
                    const double yEdge = f.window.halfSize.y + std::abs(f.window.motion.carry.y);
                    const bool xContact = xEdge >= size.x / 2 - shortHalf * .02;
                    const bool yContact = yEdge >= size.y / 2 - shortHalf * .02;
                    if (!contact && (xContact || yContact))
                        shortFirst = size.x == size.y || (size.x < size.y ? xContact : yContact);
                    contact |= xContact || yContact;
                    if (std::abs(f.window.halfSize.x - f.window.halfSize.y) > shortHalf * .1) {
                        CHECK(contact);
                        deformed = true;
                    }
                    if (contact && !(xContact && yContact)) {
                        const double small = std::min(f.window.halfSize.x, f.window.halfSize.y);
                        constexpr double pi = 3.14159265358979323846;
                        const double area = 4 * f.window.halfSize.x * f.window.halfSize.y -
                            (4 - pi) * f.window.corner * f.window.corner;
                        // A modest amount of inflation after first contact must
                        // already flatten the ends, before the other walls arrive.
                        if (area > 1.08 * pi * small * small) {
                            CHECK(f.window.corner < .97 * small);
                            earlyMorph = true;
                        }
                    }
                }
                std::size_t live = 0;
                for (std::size_t j = 0; j < f.formationCount; ++j) {
                    const auto &s = f.formation[j];
                    if (s.visibility <= 0) continue;
                    ++live;
                    CHECK(s.radius > 0 && std::isfinite(s.centre.x) && std::isfinite(s.centre.y));
                    // Wall thickness is the film's own, not its size.
                    CHECK(s.thickness > 250 && s.thickness < 900);
                    if (j < previous.formationCount && previous.formation[j].visibility > 0 &&
                        previous.formation[j].seed == s.seed) {
                        const auto &p = previous.formation[j];
                        CHECK(std::abs(s.radius - p.radius) < std::max(size.x, size.y) * .02);
                        CHECK(std::hypot(s.centre.x - p.centre.x, s.centre.y - p.centre.y) <
                              std::max(size.x, size.y) * .05);
                    } else {
                        ++births;
                        lateBirth |= i > 300;
                    }
                    if (s.growth >= 1 && s.collapse == 0) {
                        smallest = std::min(smallest, s.radius);
                        largest = std::max(largest, s.radius);
                    }
                }
                if (i < 500) peakEarly = std::max(peakEarly, live);
                lingered |= b.landed() && live > 0;
                previous = f;
            }
            // Small bubbles hold the stage for the first half; the window bubble forms late.
            CHECK(emerged >= .5 && emerged <= .72);
            CHECK(grewFromCompanion);
            CHECK(peakEarly >= 15 && births > peakEarly && lateBirth);
            CHECK(largest > smallest * 3);
            CHECK(contact && shortFirst && lingered);
            if (size.x != size.y) CHECK(deformed && earlyMorph);
            CHECK(!b.active() && !b.closed());
            CHECK(previous.window.halfSize.x == size.x / 2 && previous.window.halfSize.y == size.y / 2);
            CHECK(previous.window.corner == 0 && previous.window.depth == 0);
            CHECK(previous.window.content == 1 && previous.window.visibility == 1);
            CHECK(previous.window.core.x == 0 && previous.window.core.y == 0);
            for (std::size_t j = 0; j < previous.formationCount; ++j)
                CHECK(previous.formation[j].visibility == 0);
        }
    }

    // Two pressed films coalesce through a neck: the smaller empties into the larger and their
    // air, a volume, is conserved; nothing is merged that never touched.
    void foamCoalescenceConservesVolume() {
        BubbleFoam foam;
        foam.reset(3, {}, 4);
        const int a = foam.spawn({0, 0}, {}, .05, 600, 1, 10);
        const int b = foam.spawn({.075, 0}, {}, .03, 600, 1, 10);
        const int far = foam.spawn({.6, 0}, {}, .03, 600, 1, 10);
        const double volume = std::pow(.05, 3) + std::pow(.03, 3);
        BubbleFoamFlow still;
        for (int i = 0; i < 360 && foam.film(b).live; ++i) foam.step(1.0 / 360, still, nullptr);
        CHECK(foam.film(a).live && !foam.film(b).live);
        CHECK(std::abs(std::pow(foam.film(a).radius, 3) - volume) < volume * .02);
        CHECK(foam.film(far).live && std::abs(foam.film(far).radius - .03) < 1e-9);
    }

    // Drained films rupture on their own schedule rather than all at once.
    void foamFilmsRuptureIndependently() {
        BubbleFoam foam;
        foam.reset(9, {}, 8);
        for (int i = 0; i < 8; ++i) foam.spawn({i * .3, 0}, {}, .02, 600, 1, 10);
        foam.expire(.5);
        BubbleFoamFlow still;
        std::size_t previous = foam.liveCount(), distinct = 0;
        for (int i = 0; i < 300; ++i) {
            foam.step(1.0 / 360, still, nullptr);
            if (foam.liveCount() != previous) ++distinct;
            CHECK(previous - foam.liveCount() <= 2);
            previous = foam.liveCount();
        }
        CHECK(previous == 0 && distinct >= 4);
    }

    // Where the window bubble forms comes from the foam's own history, not a fixed point, and
    // its companions keep pushing it while it grows.
    void formationMovementDependsOnSeed() {
        Vec2 origins[2]{};
        for (int seed = 0; seed < 2; ++seed) {
            Bubble b;
            b.begin(Transition::Open, 1, 40 + seed, SIZE);
            bool emerged = false, pushed = false;
            Vec2 previous{};
            for (int i = 0; i < 900; ++i) {
                b.advance(.001);
                const auto frame = b.frame(SIZE);
                const auto &s = frame.window;
                if (s.visibility <= 0) continue;
                if (!emerged) {
                    emerged = true;
                    origins[seed] = s.motion.carry;
                } else if (!b.landed()) {
                    bool touching = false;
                    for (std::size_t j = 0; j < kBubbleFoamCapacity && j < frame.formationCount; ++j) {
                        const auto &other = frame.formation[j];
                        if (other.visibility <= 0) continue;
                        const double distance = std::hypot(s.motion.carry.x - other.centre.x,
                                                           s.motion.carry.y - other.centre.y);
                        touching |= distance <= std::max(s.halfSize.x, s.halfSize.y) + other.radius;
                    }
                    const double step = std::hypot(s.motion.carry.x - previous.x, s.motion.carry.y - previous.y);
                    pushed |= touching && step > 1e-3;
                }
                previous = s.motion.carry;
            }
            CHECK(emerged && pushed);
        }
        CHECK(std::hypot(origins[0].x - origins[1].x, origins[0].y - origins[1].y) >
              .02 * std::min(SIZE.x, SIZE.y));
    }

    void pressureChangesInflationAndSettles() {
        constexpr double pi = 3.14159265358979323846;
        for (Vec2 size : {Vec2{320, 900}, Vec2{1100, 320}, Vec2{640, 640}, SIZE}) {
            Bubble b;
            b.begin(Transition::Open, 1, 42, size);
            const auto area = [](const BubbleSurface &s) {
                return 4 * s.halfSize.x * s.halfSize.y - (4 - pi) * s.corner * s.corner;
            };
            double previousArea = area(b.frame(size).window);
            double minRate = std::numeric_limits<double>::infinity(), maxRate = 0;
            double peakCore = 0, lateCore = 0;
            bool positive = false, negative = false;
            double freeRate = 0, constrainedRate = 0;
            int contactStep = 0;
            for (int i = 1; i <= 100; ++i) {
                b.advance(.01);
                const auto f = b.frame(size);
                // Equal integration intervals avoid mistaking substep quantisation (or
                // sqrt(area)'s changing radius rate) for variable air flow.
                if (i % 5 == 0) {
                    const double currentArea = area(f.window);
                    const double rate = currentArea - previousArea;
                    if (i >= 15 && !b.landed() && rate > size.x * size.y * .001) {
                        minRate = std::min(minRate, rate);
                        maxRate = std::max(maxRate, rate);
                    }
                    if (std::max(size.x, size.y) > 2 * std::min(size.x, size.y)) {
                        const double radius = std::min(size.x / 2 - std::abs(f.window.motion.carry.x),
                            size.y / 2 - std::abs(f.window.motion.carry.y));
                        const double contactRatio = currentArea / (pi * radius * radius);
                        if (contactStep == 0) {
                            if (contactRatio < 1) freeRate = rate;
                            else contactStep = i;
                        }
                        // Compare complete intervals on either side of contact,
                        // allowing the inlet to respond. A moving bubble can
                        // cross a narrow area-ratio band within a single interval.
                        if (contactStep > 0 && i == contactStep + 10)
                            constrainedRate = rate;
                    }
                    previousArea = currentArea;
                }
                if (b.landed()) {
                    peakCore = std::max(peakCore, std::hypot(f.window.core.x, f.window.core.y));
                    positive |= f.window.core.x > .0001 * std::min(size.x, size.y);
                    negative |= f.window.core.x < -.0001 * std::min(size.x, size.y);
                }
                if (i == 99) lateCore = std::hypot(f.window.core.x, f.window.core.y);
            }
            CHECK(maxRate > minRate * 1.15);
            if (std::max(size.x, size.y) > 2 * std::min(size.x, size.y)) {
                CHECK(contactStep > 0 && freeRate > 0 && constrainedRate > 0);
                CHECK(constrainedRate > 1.3 * freeRate);
            }
            CHECK(peakCore > .002 * std::min(size.x, size.y));
            CHECK(positive && negative);
            CHECK(lateCore < peakCore * .1);
            CHECK(b.frame(size).window.core.x == 0 && b.frame(size).window.core.y == 0);
        }
    }

    void formationControlsKeepTheEndpoint() {
        std::array<BubbleFormationParams, 20> cases{};
        cases[0].count = 0;
        cases[1].count = 1;
        cases[2].birthRate = .25;
        cases[3].birthRate = 2;
        cases[4].sizeVariation = 0;
        cases[5].sizeVariation = 1;
        cases[6].growthVariation = 0;
        cases[7].growthVariation = 1;
        cases[8].interaction = .25;
        cases[9].interaction = 2;
        cases[10].damping = .5;
        cases[11].damping = 2;
        cases[12].buoyancy = 0;
        cases[13].buoyancy = 2;
        cases[14].linger = 0;
        cases[15].linger = 1;
        cases[16] = {4, .25, 0, 0, .25, .5, 0, 0};
        cases[17] = {32, 2, 1, 1, 2, 2, 2, 1};
        cases[18] = {32, 2, 1, 1, 2, .5, 2, 1};
        cases[19] = {32, .25, 0, 0, .25, 2, 0, 1};
        for (Vec2 size : {Vec2{320, 900}, Vec2{1100, 320}, Vec2{640, 640}}) {
            for (const auto &params : cases) {
                Bubble b;
                b.begin(Transition::Open, 1, 42, size, params);
                for (int i = 0; i < 99; ++i) {
                    b.advance(.01);
                    const auto f = b.frame(size);
                    std::size_t visible = 0;
                    for (std::size_t j = 0; j < f.formationCount; ++j) {
                        const auto &s = f.formation[j];
                        if (s.visibility > 0) ++visible;
                        CHECK(std::isfinite(s.centre.x) && std::isfinite(s.centre.y));
                        CHECK(std::isfinite(s.radius));
                    }
                    // The film the window bubble grew from is drawn once more while it takes over.
                    CHECK(visible <= static_cast<std::size_t>(params.count) + 1);
                    CHECK(std::isfinite(f.window.core.x) && std::isfinite(f.window.core.y));
                }
                const auto beforeEnd = b.frame(size);
                // The exact native endpoint must not hide a failed inflation solver.
                CHECK(b.landed());
                CHECK(std::abs(beforeEnd.window.halfSize.x - size.x / 2) < .001 * size.x);
                CHECK(std::abs(beforeEnd.window.halfSize.y - size.y / 2) < .001 * size.y);
                CHECK(beforeEnd.window.corner < .001 * std::min(size.x, size.y));
                b.advance(.01);
                CHECK(!b.active() && b.frame(size).formationCount == 0);
            }
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
            for (std::size_t i = 0; i < before.holeCount; ++i)
                CHECK(before.holes[i].radius == after.holes[i].radius);
        }
    }

    void interruptedCloseIsContinuous() {
        for (double t : {0.0, 0.2, 0.38, 0.44, 0.55, 0.62, 0.85, 0.9}) {
            Bubble b;
            b.begin(Transition::Open, 1, 9, SIZE);
            advanceFor(b, t);
            const auto before = b.frame(SIZE);
            b.begin(Transition::Close, .42, 11, SIZE);
            const auto after = b.frame(SIZE);
            CHECK(b.active() && !b.opening());
            CHECK(before.seed == after.seed);
            checkSurfaceContinuity(before.window, after.window);
            CHECK(after.window.ruptureVisibility == before.window.visibility);
            CHECK(after.formationCount == before.formationCount);
            for (std::size_t i = 0; i < before.formationCount && i < after.formationCount; ++i) {
                const auto &p = before.formation[i], &q = after.formation[i];
                CHECK(p.centre.x == q.centre.x && p.centre.y == q.centre.y);
                CHECK(p.radius == q.radius && p.visibility == q.visibility);
                CHECK(p.collapse == q.collapse && p.thickness == q.thickness);
            }
            advanceFor(b, .05);
            const auto progressing = b.frame(SIZE);
            CHECK(progressing.window.holes[0].radius >= after.window.holes[0].radius);
            advanceFor(b, .5);
            CHECK(b.closed() && !b.active());
            const auto closed = b.frame(SIZE);
            for (std::size_t i = 0; i < closed.formationCount; ++i)
                CHECK(closed.formation[i].visibility == 0);
        }
    }

    void interruptedResizeKeepsWindowRupture() {
        Bubble b;
        b.begin(Transition::Open, 1, 9, SIZE);
        advanceFor(b, 0.44);
        b.begin(Transition::Close, 1, 11, SIZE);
        const auto original = b.frame(SIZE);
        const auto resized = b.frame({SIZE.x * 1.5, SIZE.y * 0.5});
        CHECK(resized.window.halfSize.x == original.window.halfSize.x * 1.5);
        CHECK(resized.window.halfSize.y == original.window.halfSize.y * 0.5);
        CHECK(resized.window.holes[0].centre.x == original.window.holes[0].centre.x * 1.5);
        CHECK(resized.window.holes[0].centre.y == original.window.holes[0].centre.y * .5);
        CHECK(resized.window.speed == original.window.speed * 1.5);
        CHECK(b.frame(SIZE).window.holes[0].radius == original.window.holes[0].radius);
    }

    void ruptureCoversCorners() {
        for (Vec2 size : {Vec2{1800, 200}, Vec2{200, 1800}, SIZE}) {
            Bubble b;
            b.begin(Transition::Close, 1, 19, size);
            auto f = b.frame(size).window;
            CHECK(f.visibility == 1 && b.frame(size).shell.visibility == 0);
            CHECK(f.holes[0].centre.x != 0 && f.holes[0].centre.y != 0);
            CHECK(f.holes[0].radius == 0);
            advanceFor(b, 0.9);
            f = b.frame(size).window;
            const auto &h = f.holes[0];
            const double farthest =
                std::hypot(size.x / 2 + std::abs(h.centre.x), size.y / 2 + std::abs(h.centre.y));
            CHECK(h.radius * .95 >= farthest);
            advanceFor(b, 0.2);
            CHECK(b.closed() && !b.active());
            CHECK(b.frame(size).window.visibility == 0 && b.frame(size).shell.visibility == 0);
            const Box bounds = b.bounds(size);
            CHECK(bounds.w == 0 && bounds.h == 0);
        }
    }

    void checkSurfaceBounds(const Box &box, Vec2 size, const BubbleSurface &surface) {
        if (surface.visibility <= 0) return;
        const Vec2 centre{size.x / 2 + surface.motion.carry.x,
            size.y / 2 + surface.motion.carry.y};
        CHECK(box.x <= centre.x - surface.halfSize.x - std::abs(surface.ripple));
        CHECK(box.y <= centre.y - surface.halfSize.y - std::abs(surface.ripple));
        CHECK(box.x + box.w >= centre.x + surface.halfSize.x + std::abs(surface.ripple));
        CHECK(box.y + box.h >= centre.y + surface.halfSize.y + std::abs(surface.ripple));
    }

    void boundsContainTheSurfaces() {
        for (Vec2 size : {Vec2{320, 900}, Vec2{1100, 320}, Vec2{640, 640}, SIZE}) {
            const auto check = [&](Transition direction) {
                Bubble b;
                b.begin(direction, 1, 8, size);
                CHECK(b.bounds(size).w > 0 && b.bounds(size).h > 0);
                for (int i = 0; i < 100; ++i) {
                    const auto f = b.frame(size);
                    const auto box = b.bounds(size);
                    checkSurfaceBounds(box, size, f.window);
                    checkSurfaceBounds(box, size, f.shell);
                    for (std::size_t j = 0; j < f.formationCount; ++j) {
                        const auto &s = f.formation[j];
                        if (s.visibility <= 0) continue;
                        const double reach = s.radius * (1 + std::abs(s.squash.x) + std::abs(s.squash.y));
                        CHECK(box.x <= size.x / 2 + s.centre.x - reach);
                        CHECK(box.y <= size.y / 2 + s.centre.y - reach);
                        CHECK(box.x + box.w >= size.x / 2 + s.centre.x + reach);
                        CHECK(box.y + box.h >= size.y / 2 + s.centre.y + reach);
                    }
                    std::array<BubbleParticle, kBubbleMaxParticles> particles{};
                    for (const auto *surface : {&f.window, &f.shell}) {
                        const auto count = bubbleParticles(*surface, particles);
                        for (std::size_t j = 0; j < count; ++j) {
                            const auto &p = particles[j];
                            CHECK(box.x <= size.x / 2 + p.centre.x - p.radius);
                            CHECK(box.x + box.w >= size.x / 2 + p.centre.x + p.radius);
                            CHECK(box.y <= size.y / 2 + p.centre.y - p.radius);
                            CHECK(box.y + box.h >= size.y / 2 + p.centre.y + p.radius);
                            CHECK(box.x <= size.x / 2 + p.tail.x - p.tailRadius);
                            CHECK(box.x + box.w >= size.x / 2 + p.tail.x + p.tailRadius);
                            CHECK(box.y <= size.y / 2 + p.tail.y - p.tailRadius);
                            CHECK(box.y + box.h >= size.y / 2 + p.tail.y + p.tailRadius);
                        }
                    }
                    b.advance(0.01);
                }
            };
            check(Transition::Open);
            check(Transition::Close);
        }
    }

    void wideWindowsKeepTightVerticalBounds() {
        Bubble normal, wide;
        normal.begin(Transition::Close, 1, 8, {600, 200});
        wide.begin(Transition::Close, 1, 8, {1800, 200});
        advanceFor(normal, 0.6);
        advanceFor(wide, 0.6);
        CHECK(normal.bounds({600, 200}).h == wide.bounds({1800, 200}).h);
    }

    void localizedRuptureAndCoverage() {
        for (Vec2 size : {Vec2{1800, 200}, Vec2{200, 1800}, SIZE}) {
            Bubble b;
            b.begin(Transition::Close, .42, 19, size);
            advanceFor(b, .42 * .149);
            auto s = b.frame(size).window;
            CHECK(s.holes[0].radius == 0);
            CHECK(s.holes[0].precursor > .9);
            CHECK(s.film == 0);
            for (std::size_t i = 1; i < s.holeCount; ++i) {
                Bubble probe;
                probe.begin(Transition::Close, .42, 19, size);
                advanceFor(probe, .42 * s.holes[i].start);
                const auto atBirth = probe.frame(size).window;
                CHECK(atBirth.holes[i].start > atBirth.holes[0].start);
                for (std::size_t j = 0; j < i; ++j) {
                    const auto &h = atBirth.holes[j];
                    const auto p = atBirth.holes[i].centre;
                    CHECK(std::hypot(p.x - h.centre.x, p.y - h.centre.y) >
                          bubbleHoleBoundary(h, bubbleHoleAzimuth(h, p, false)));
                }
            }
            advanceFor(b, .42 * .21);
            const auto first = b.frame(size).window;
            advanceFor(b, .42 * .10);
            const auto second = b.frame(size).window;
            CHECK(std::abs((second.holes[0].radius - first.holes[0].radius) - second.speed * .1) <
                  second.speed * .006);
            advanceFor(b, .42 * .151);
            s = b.frame(size).window;
            for (int y = -5; y <= 5; ++y) {
                for (int x = -5; x <= 5; ++x) {
                    const Vec3 p{size.x * .1 * x, size.y * .1 * y, 0};
                    bool covered = false;
                    for (std::size_t i = 0; i < s.holeCount; ++i) {
                        const auto &h = s.holes[i];
                        covered |= std::hypot(p.x - h.centre.x, p.y - h.centre.y) <=
                                   bubbleHoleBoundary(h, bubbleHoleAzimuth(h, p, false));
                    }
                    CHECK(covered);
                }
            }
        }
    }

    void precursorAndRemnants() {
        Bubble b;
        b.begin(Transition::Close, .42, 42, SIZE);
        auto s = b.frame(SIZE).window;
        CHECK(s.film == 0 && s.thickness >= 380 && s.thickness <= 650);
        advanceFor(b, .42 * .02);
        CHECK(b.frame(SIZE).window.holes[0].precursor > 0);
        advanceFor(b, .42 * .13);
        CHECK(b.frame(SIZE).window.holes[0].radius < 1e-8);
        advanceFor(b, 1.0 / 60);
        s = b.frame(SIZE).window;
        CHECK(s.holes[0].radius < .05 * std::min(s.halfSize.x, s.halfSize.y));
        advanceFor(b, .42 * .20);
        s = b.frame(SIZE).window;
        std::array<BubbleParticle, kBubbleMaxParticles> particles{};
        auto n = bubbleParticles(s, particles);
        std::size_t ligaments = 0;
        for (std::size_t i = 0; i < n; ++i) ligaments += particles[i].tailRadius > 0;
        CHECK(ligaments > 0);
        advanceFor(b, .42 * .42);
        s = b.frame(SIZE).window;
        n = bubbleParticles(s, particles);
        CHECK(n > 0);
        advanceFor(b, .42 * .2);
        CHECK(b.closed());
        CHECK(bubbleParticles(b.frame(SIZE).window, particles) == 0);
    }


    void seededFlatCoverage() {
        for (Vec2 size : {Vec2{1800, 200}, Vec2{200, 1800}, SIZE}) {
            for (std::uint64_t seed = 0; seed < 16; ++seed) {
                Bubble b;
                b.begin(Transition::Close, .42, seed, size);
                advanceFor(b, .42 * .62);
                const auto s = b.frame(size).window;
                for (int y = -3; y <= 3; ++y) {
                    for (int x = -3; x <= 3; ++x) {
                        const Vec3 p{x * size.x / 6, y * size.y / 6, 0};
                        bool covered = false;
                        for (std::size_t i = 0; i < s.holeCount; ++i) {
                            const auto &h = s.holes[i];
                            covered |= std::hypot(p.x - h.centre.x, p.y - h.centre.y) <=
                                       bubbleHoleBoundary(h, bubbleHoleAzimuth(h, p, false));
                        }
                        CHECK(covered);
                    }
                }
            }
        }
    }

    void impactsUseAnimationTime() {
        Bubble b;
        b.begin(Transition::Close, .42, 19, SIZE);
        auto first = b.frame(SIZE).window;
        std::array<BubbleImpact, kBubbleMaxImpacts> a{}, c{};
        const auto n = bubbleImpacts(first, a);
        CHECK(n <= kBubbleMaxImpacts);
        advanceFor(b, .01);
        const auto m = bubbleImpacts(b.frame(SIZE).window, c);
        CHECK(n == m);
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(std::abs((a[i].delay - c[i].delay) - .01) < 1e-10);
            CHECK(a[i].position.x == c[i].position.x && a[i].position.y == c[i].position.y);
        }
    }

    void impactsFollowDroplets() {
        for (const auto seed : {42ULL, 7ULL, 99ULL, 1234ULL}) {
            for (const auto direction : {Transition::Open, Transition::Close}) {
                const bool opening = direction == Transition::Open;
                Bubble b;
                b.begin(direction, opening ? .62 : .42, seed, {900, 560});
                if (opening) advanceFor(b, .62 * .31);
                auto frame = b.frame({900, 560});
                auto surface = frame.window;
                std::array<BubbleImpact, kBubbleMaxImpacts> impacts{};
                const auto count = bubbleImpacts(surface, impacts);
                CHECK(count <= 2u);
                if (opening) CHECK(count == 0); // formation drains; it does not spray glass debris
                for (std::size_t i = 0; i < count; ++i)
                    CHECK(impacts[i].radius >= 1.5 && impacts[i].radius <= 3.5);
                if (!count) continue;
                CHECK(impacts[0].delay > .00002);
                advanceFor(b, impacts[0].delay - .00001);
                frame = b.frame({900, 560});
                surface = frame.window;
                std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
                const auto before = detail::bubbleParticlesWithIds(surface, particles);
                std::uint32_t source = 0;
                bool found = false;
                for (std::size_t i = 0; i < before; ++i) {
                    const auto &p = particles[i];
                    if (p.particle.blur > .95 &&
                        std::hypot(p.particle.centre.x - impacts[0].position.x,
                            p.particle.centre.y - impacts[0].position.y) < 1) {
                        source = p.id;
                        found = true;
                        break;
                    }
                }
                CHECK(found);
                advanceFor(b, .00002);
                frame = b.frame({900, 560});
                surface = frame.window;
                const auto after = detail::bubbleParticlesWithIds(surface, particles);
                for (std::size_t i = 0; found && i < after; ++i) CHECK(particles[i].id != source);
            }
        }
    }

    void particleContinuityAndEvaporation() {
        Bubble b;
        b.begin(Transition::Close, .42, 19, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> previous{}, current{};
        std::size_t oldCount = 0, peak = 0;
        bool burst = false;
        for (int frame = 0; frame < 101; ++frame) {
            const auto s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, current);
            peak = std::max(peak, count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &p = current[i];
                burst |= p.id >= 1000 && p.id < 2000;
                for (std::size_t j = 0; j < oldCount; ++j) {
                    if (previous[j].id != p.id) continue;
                    const auto &q = previous[j].particle;
                    const bool attached = p.id >= 2000 && p.id < 3000;
                    // Attached ligaments follow the rolling film, not just the puncture front.
                    const double speedLimit = s.speed * (attached ? 2 : 1);
                    CHECK(std::hypot(p.particle.centre.x - q.centre.x,
                              p.particle.centre.y - q.centre.y) <= speedLimit * .01 + 2);
                    if (attached) {
                        CHECK(p.particle.radius + 1e-9 >= q.radius);
                    } else if (p.particle.blur == 0 && q.blur == 0) {
                        CHECK(p.particle.radius <= q.radius + 1e-9);
                    }
                }
            }
            previous = current;
            oldCount = count;
            b.advance(.0042);
        }
        CHECK(peak > 0 && peak <= kBubbleMaxParticles);
        CHECK(burst);
        CHECK(oldCount == 0);
    }

    void fingersStartGradually() {
        Bubble b;
        b.begin(Transition::Close, .42, 42, SIZE);
        advanceFor(b, .42 * .17);
        auto s = b.frame(SIZE).window;
        CHECK(s.holes[0].radius < std::max(24.0, .08 * s.halfSize.y));
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
        CHECK(detail::bubbleParticlesWithIds(s, particles) == 0);
        Bubble shell;
        shell.begin(Transition::Open, .62, 42, SIZE);
        advanceFor(shell, .62 * .32);
        const auto earlyShell = shell.frame(SIZE).shell;
        CHECK(earlyShell.holes[0].radius * earlyShell.halfSize.x <
              std::max(24.0, .08 * earlyShell.halfSize.x));
        CHECK(detail::bubbleParticlesWithIds(earlyShell, particles) == 0);
        bool varied = false, satellite = false;
        for (int frame = 0; frame < 20; ++frame) {
            b.advance(.005);
            s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, particles);
            double shortest = 1e9, longest = 0;
            std::size_t forming = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (particles[i].id >= 10000) {
                    const auto source = (particles[i].id - 10000) / 2;
                    for (std::size_t j = 0; j < count; ++j) {
                        if (particles[j].id != source) continue;
                        satellite = true;
                        CHECK(particles[i].particle.radius < particles[j].particle.radius);
                    }
                }
                const auto &p = particles[i];
                if (p.id < 2000 || p.id >= 3000) continue;
                ++forming;
                CHECK(p.particle.radius > p.particle.tailRadius);
                const double length = std::hypot(p.particle.centre.x - p.particle.tail.x,
                    p.particle.centre.y - p.particle.tail.y);
                shortest = std::min(shortest, length);
                longest = std::max(longest, length);
            }
            varied |= forming >= 3 && longest - shortest > 3;
        }
        CHECK(varied && satellite);
    }

    void curledGeometryAndAttachments() {
        BubbleSurface surface;
        surface.halfSize = {300, 200};
        surface.aureole = 1;
        BubbleHole hole;
        hole.phase = {.2, -.4, 0, 0};
        const auto section = bubbleCurlSection(surface, hole, 100, .7);
        const Vec3 base{100, 20, 0}, outward{1, 0, 0}, normal{0, 0, 1};
        const auto root = bubbleCurlSample(base, outward, normal, section, 0);
        CHECK(root.position.x == base.x && root.position.y == base.y && root.position.z == base.z);
        CHECK(root.normal.x == 0 && root.normal.y == 0 && root.normal.z == 1);
        for (int i = 0; i <= 20; ++i) {
            const auto sample = bubbleCurlSample(base, outward, normal, section, i / 20.0);
            CHECK(std::abs(std::hypot(sample.normal.x, sample.normal.z) - 1) < 1e-12);
            CHECK(std::hypot(sample.position.x - base.x, sample.position.z - base.z) <=
                  section.width + 1e-9);
        }
        Bubble b;
        b.begin(Transition::Close, .42, 42, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> particles{};
        bool attached = false;
        for (int frame = 0; frame < 90; ++frame) {
            const auto s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, particles);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &p = particles[i];
                if (p.id < 2000 || p.id >= 3000) continue;
                const std::size_t hi = (p.id - 2000) / 104;
                const auto &h = s.holes[hi];
                const double phi =
                    std::atan2(p.particle.tail.y - h.centre.y, p.particle.tail.x - h.centre.x);
                const auto expected = bubblePeelSample(s, h, phi, 1);
                CHECK(std::hypot(expected.position.x - p.particle.tail.x,
                          expected.position.y - p.particle.tail.y) < 1e-6);
                attached = true;
            }
            b.advance(.0042);
        }
        CHECK(attached);
    }

    void sheetsContract() {
        Bubble b;
        b.begin(Transition::Close, 2.0, 19, SIZE);
        std::array<BubbleSheet, kBubbleMaxSheets> sheets{}, again{};
        bool born = false, moved = false, expired = false;
        std::array<std::uint32_t, kBubbleMaxSheets> retired{};
        std::size_t retiredCount = 0;
        for (int frame = 0; frame < 999; ++frame) {
            auto s = b.frame(SIZE).window;
            const auto count = bubbleSheets(s, sheets);
            CHECK(count <= kBubbleMaxSheets);
            CHECK(bubbleSheets(s, again) == count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &sheet = sheets[i];
                CHECK(sheet.id == again[i].id && sheet.birth == again[i].birth);
                for (std::size_t j = 0; j < retiredCount; ++j)
                    if (retired[j] == sheet.id) CHECK(sheet.scale == 0);
                if (sheet.scale == 0 &&
                    std::find(retired.begin(), retired.begin() + retiredCount, sheet.id) ==
                        retired.begin() + retiredCount)
                    retired[retiredCount++] = sheet.id;
                CHECK(sheet.halfAngle > 0 && sheet.halfAngle <= .35);
                CHECK(sheet.scale >= 0 && sheet.scale <= 1);
                auto birth = s;
                birth.clock = sheet.birth;
                std::array<BubbleSheet, kBubbleMaxSheets> atBirth{};
                const auto birthCount = bubbleSheets(birth, atBirth);
                for (std::size_t j = 0; j < birthCount; ++j) {
                    if (atBirth[j].id != sheet.id) continue;
                    auto h = birth.holes[sheet.hole];
                    h.radius = sheet.radii[sheet.hole];
                    for (std::size_t m = 0; m < h.amplitude.size(); ++m)
                        h.amplitude[m] = sheet.onsets[sheet.hole] * (.014 - .002 * m);
                    const auto source = bubblePeelSample(birth, h, sheet.phi, .5);
                    const auto detached = bubbleSheetSample(birth, atBirth[j], sheet.phi, .5);
                    CHECK(std::hypot(source.position.x - detached.position.x,
                              source.position.y - detached.position.y) < 1e-8);
                    CHECK(std::abs(source.position.z - detached.position.z) < 1e-8);
                    CHECK(std::abs(source.normal.x - detached.normal.x) < 1e-8);
                    CHECK(std::abs(source.normal.y - detached.normal.y) < 1e-8);
                    CHECK(std::abs(source.normal.z - detached.normal.z) < 1e-8);
                    const double boundary = bubbleHoleBoundary(h, sheet.phi);
                    const double width = bubbleCurlSection(birth, h, boundary, sheet.phi).width;
                    const Vec3 material{h.centre.x + (boundary + width * .5) * std::cos(sheet.phi),
                        h.centre.y + (boundary + width * .5) * std::sin(sheet.phi),
                        0};
                    CHECK(bubbleSheetContains(birth, sheet, material));
                    born = true;
                }
                moved |= sheet.age > 0 && sheet.scale < 1 && sheet.scale > 0 &&
                         std::hypot(sheet.translation.x, sheet.translation.y) > 0;
                expired |= sheet.scale == 0;
                const auto box = b.bounds(SIZE);
                for (double offset : {-1.0, 0.0, 1.0})
                    for (double u : {0.0, .5, 1.0}) {
                        const auto p =
                            bubbleSheetSample(s, sheet, sheet.phi + offset * sheet.halfAngle, u)
                                .position;
                        CHECK(p.x + SIZE.x * .5 >= box.x && p.x + SIZE.x * .5 <= box.x + box.w);
                        CHECK(p.y + SIZE.y * .5 >= box.y && p.y + SIZE.y * .5 <= box.y + box.h);
                    }
            }
            b.advance(.002);
        }
        CHECK(born && moved && expired);
    }

    void pinchBirthIsContinuous() {
        Bubble b;
        b.begin(Transition::Close, .42, 42, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> previous{}, current{};
        std::size_t oldCount = 0;
        bool observed = false;
        for (int frame = 0; frame < 2000; ++frame) {
            const auto s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, current);
            for (std::size_t i = 0; i < count; ++i) {
                if (current[i].id >= 2000) continue;
                for (std::size_t j = 0; j < oldCount; ++j) {
                    if (previous[j].id != current[i].id + 2000) continue;
                    const auto &a = previous[j].particle;
                    const auto &p = current[i].particle;
                    CHECK(std::hypot(a.centre.x - p.centre.x, a.centre.y - p.centre.y) <
                          s.speed * s.rate * .00021 + 1);
                    CHECK(std::hypot(a.tail.x - p.tail.x, a.tail.y - p.tail.y) <
                          s.speed * s.rate * .00021 + 1);
                    observed = true;
                }
            }
            previous = current;
            oldCount = count;
            b.advance(.00021);
        }
        CHECK(observed);
    }

    void sheetsKeepTheirSourceMaterial() {
        BubbleSurface surface;
        surface.halfSize = {200, 200};
        surface.holeCount = 2;
        surface.aureole = 1;
        surface.holes[1].centre = {20, 0, 0};
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
        Bubble a, b;
        a.begin(Transition::Open, 1, 55, SIZE);
        b.begin(Transition::Open, 1, 55, SIZE);
        advanceFor(a, 0.84, 0.01);
        advanceFor(b, 0.84, 0.02);
        auto x = a.frame(SIZE), y = b.frame(SIZE);
        CHECK(std::abs(x.window.halfSize.x - y.window.halfSize.x) < 1e-9);
        CHECK(std::abs(x.window.depth - y.window.depth) < 1e-9 && x.seed == y.seed);
        CHECK(std::abs(x.window.core.x - y.window.core.x) < 1e-9 &&
              std::abs(x.window.core.y - y.window.core.y) < 1e-9);
        CHECK(x.formationCount == y.formationCount);
        for (std::size_t i = 0; i < x.formationCount && i < y.formationCount; ++i) {
            const auto &p = x.formation[i];
            const auto &q = y.formation[i];
            CHECK(std::abs(p.radius - q.radius) < 1e-9);
            CHECK(std::abs(p.centre.x - q.centre.x) < 1e-9);
            CHECK(std::abs(p.centre.y - q.centre.y) < 1e-9);
            CHECK(std::abs(p.visibility - q.visibility) < 1e-9);
        }
        a.advance(-1);
        a.advance(std::numeric_limits<double>::quiet_NaN());
        CHECK(a.frame(SIZE).window.halfSize.x == x.window.halfSize.x);
        a.advance(30);
        CHECK(std::isfinite(a.frame(SIZE).window.halfSize.x));
        a.reset();
        CHECK(!a.active() && !a.closed());
        a.begin(Transition::Close, 0, 1, SIZE);
        CHECK(!a.active() && !a.closed());
        a.begin(Transition::Open, 1, 1, {0, 100});
        CHECK(!a.active());
        const Box invalid = a.bounds({std::numeric_limits<double>::infinity(), 2});
        CHECK(invalid.w == 0 && invalid.h == 0);
    }
}  // namespace

int main() {
    formationDuringOpening();
    formationMovementDependsOnSeed();
    foamCoalescenceConservesVolume();
    foamFilmsRuptureIndependently();
    pressureChangesInflationAndSettles();
    formationControlsKeepTheEndpoint();
    interruptedCloseIsContinuous();
    interruptedResizeKeepsWindowRupture();
    ruptureCoversCorners();
    boundsContainTheSurfaces();
    wideWindowsKeepTightVerticalBounds();
    deterministicAndInvalidInput();
    localizedRuptureAndCoverage();
    impactsUseAnimationTime();
    impactsFollowDroplets();
    precursorAndRemnants();
    seededFlatCoverage();
    particleContinuityAndEvaporation();
    fingersStartGradually();
    curledGeometryAndAttachments();
    sheetsContract();
    sheetsKeepTheirSourceMaterial();
    pinchBirthIsContinuous();
    return g_failures == 0 ? 0 : 1;
}
