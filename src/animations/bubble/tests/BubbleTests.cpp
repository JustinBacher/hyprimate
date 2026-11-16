#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleCurl.hpp"
#include "animations/bubble/BubbleRupture.hpp"

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

    void separateSurfacesDuringOpening() {
        for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
            Bubble b;
            b.begin(Transition::Open, 1, 42, size);
            advanceFor(b, 0.25);
            auto f = b.frame(size);
            CHECK(f.shell.visibility > 0 && f.window.visibility == 0);
            CHECK(f.shell.halfSize.x == f.shell.halfSize.y && f.shell.corner == f.shell.halfSize.x);
            CHECK(f.shell.depth > 0 && f.shell.film > 0);
            const double shellRadius = f.shell.halfSize.x;

            advanceFor(b, 0.19);
            f = b.frame(size);
            CHECK(f.shell.visibility > 0 && f.shell.holes[0].radius > 0);
            CHECK(f.window.visibility > 0);
            CHECK(f.window.halfSize.x == f.window.halfSize.y &&
                  f.window.corner == f.window.halfSize.x);
            CHECK(f.window.halfSize.x < f.shell.halfSize.x);
            CHECK(f.shell.halfSize.x > shellRadius);

            for (int i = 0; i < 20; ++i) {
                f = b.frame(size);
                if (f.shell.visibility > 0) {
                    CHECK(f.shell.halfSize.x == f.shell.halfSize.y);
                    CHECK(f.shell.corner == f.shell.halfSize.x && f.shell.ripple == 0);
                    // The inner film is still clear while the outer bubble bursts: no window image
                    // pops in with it.
                    CHECK(f.window.content <= 0.1);
                }
                b.advance(0.01);
            }
            advanceFor(b, 0.14);
            f = b.frame(size);
            CHECK(f.shell.visibility == 0);
            CHECK(f.window.halfSize.x == size.x / 2 && f.window.halfSize.y == size.y / 2);
            CHECK(f.window.corner == 0);
        }
    }

    void flatEndpointAndSplat() {
        Bubble b;
        b.begin(Transition::Open, 1, 3, SIZE);
        bool overshoot = false;
        for (int i = 0; i < 100; ++i) {
            b.advance(0.01);
            const auto f = b.frame(SIZE);
            overshoot |= f.window.halfSize.x > SIZE.x / 2 && f.window.halfSize.y > SIZE.y / 2;
            CHECK(f.window.halfSize.x <= SIZE.x * 0.54 && f.window.halfSize.y <= SIZE.y * 0.54);
        }
        const auto f = b.frame(SIZE);
        CHECK(overshoot);
        CHECK(!b.active() && !b.closed());
        CHECK(f.window.halfSize.x == SIZE.x / 2 && f.window.halfSize.y == SIZE.y / 2);
        CHECK(f.window.corner == 0 && f.window.depth == 0 && f.window.film == 0 &&
              f.window.ripple == 0);
        CHECK(f.window.content == 1 && f.window.visibility == 1 && f.shell.visibility == 0);
        CHECK(f.window.core.x == 0 && f.window.core.y == 0);
    }

    void fluidCoreOutlivesTheLanding() {
        for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
            Bubble b;
            b.begin(Transition::Open, 1, 3, size);
            advanceFor(b, 0.64);
            const double impact = b.frame(size).window.core.x;
            CHECK(impact > 0);
            advanceFor(b, 0.12);
            CHECK(b.frame(size).window.core.x < 0);  // pressure recoils through the screen plane
            advanceFor(b, 0.02);
            const auto landed = b.frame(size).window;
            CHECK(landed.halfSize.x == size.x / 2 && landed.halfSize.y == size.y / 2);
            CHECK(landed.corner == 0 && landed.depth == 0 && landed.ripple == 0);
            CHECK(landed.content == 1);
            CHECK(std::abs(landed.core.x) + std::abs(landed.core.y) > 0);
            advanceFor(b, 0.10);
            CHECK(b.frame(size).window.core.x >
                  0);  // a smaller rebound after the boundary has stopped
            CHECK(b.frame(size).window.core.x < impact);
            advanceFor(b, 0.10);
            CHECK(std::abs(b.frame(size).window.core.x) + std::abs(b.frame(size).window.core.y) <
                  impact * 0.1);
            advanceFor(b, 0.02);
            CHECK(b.frame(size).window.core.x == 0 && b.frame(size).window.core.y == 0);
            b.begin(Transition::Close, 1, 9, size);
            CHECK(b.frame(size).window.core.x == 0 && b.frame(size).window.core.y == 0);
        }
    }

    void checkSurfaceContinuity(const BubbleSurface &before, const BubbleSurface &after) {
        CHECK(before.halfSize.x == after.halfSize.x && before.halfSize.y == after.halfSize.y);
        CHECK(before.corner == after.corner && before.depth == after.depth);
        CHECK(before.core.x == after.core.x && before.core.y == after.core.y);
        CHECK(before.content == after.content && before.film == after.film);
        CHECK(before.visibility == after.visibility && before.ripple == after.ripple);
        if (before.closing) {
            CHECK(before.clock == after.clock);
            CHECK(before.holeCount == after.holeCount);
            for (std::size_t i = 0; i < before.holeCount; ++i)
                CHECK(before.holes[i].radius == after.holes[i].radius);
        }
    }

    void interruptedCloseIsContinuous() {
        for (double t : {0.0, 0.2, 0.44, 0.55, 0.85}) {
            Bubble b;
            b.begin(Transition::Open, 1, 9, SIZE);
            advanceFor(b, t);
            const auto before = b.frame(SIZE);
            b.begin(Transition::Close, 1, 11, SIZE);
            const auto after = b.frame(SIZE);
            CHECK(b.active() && !b.opening());
            CHECK(before.seed == after.seed);
            checkSurfaceContinuity(before.window, after.window);
            checkSurfaceContinuity(before.shell, after.shell);
            std::array<BubbleParticle, kBubbleMaxParticles> a{}, c{};
            const auto n = bubbleParticles(before.shell, a);
            const auto m = bubbleParticles(after.shell, c);
            CHECK(n == m);
            for (std::size_t i = 0; i < std::min(n, m); ++i) {
                CHECK(a[i].centre.x == c[i].centre.x && a[i].centre.y == c[i].centre.y);
                CHECK(a[i].radius == c[i].radius && a[i].tail.x == c[i].tail.x &&
                      a[i].tail.y == c[i].tail.y);
            }
            if (t == 0.44 || t == 0.55) {
                CHECK(before.window.visibility > 0 && before.shell.holes[0].radius > 0);
                advanceFor(b, 0.05);
                const auto progressing = b.frame(SIZE);
                CHECK(progressing.shell.holes[0].radius >= before.shell.holes[0].radius);
            } else if (t <= 0.2) {
                CHECK(before.window.visibility == 0);
                advanceFor(b, 0.1);
                CHECK(b.frame(SIZE).window.visibility == 0);
            }
        }
    }

    void interruptedResizeKeepsShellCircular() {
        Bubble b;
        b.begin(Transition::Open, 1, 9, SIZE);
        advanceFor(b, 0.44);
        b.begin(Transition::Close, 1, 11, SIZE);
        const auto original = b.frame(SIZE);
        const auto resized = b.frame({SIZE.x * 1.5, SIZE.y * 0.5});
        CHECK(original.shell.visibility > 0 && original.shell.holes[0].radius > 0);
        CHECK(resized.shell.halfSize.x == original.shell.halfSize.x * 0.5);
        CHECK(resized.shell.halfSize.y == original.shell.halfSize.y * 0.5);
        CHECK(resized.shell.corner == original.shell.corner * 0.5);
        CHECK(resized.shell.holes[0].centre.x == original.shell.holes[0].centre.x);
        CHECK(std::abs(resized.shell.holes[0].radius - original.shell.holes[0].radius) < 1e-9);
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
        CHECK(box.x <= size.x / 2 - surface.halfSize.x - std::abs(surface.ripple));
        CHECK(box.y <= size.y / 2 - surface.halfSize.y - std::abs(surface.ripple));
        CHECK(box.x + box.w >= size.x / 2 + surface.halfSize.x + std::abs(surface.ripple));
        CHECK(box.y + box.h >= size.y / 2 + surface.halfSize.y + std::abs(surface.ripple));
    }

    void boundsContainTheSurfaces() {
        for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
            const auto check = [&](Transition direction) {
                Bubble b;
                b.begin(direction, 1, 8, size);
                const auto initial = b.frame(size);
                if (direction == Transition::Open)
                    CHECK(initial.window.visibility == 0 && initial.shell.visibility == 0);
                CHECK(b.bounds(size).w > 0 && b.bounds(size).h > 0);
                for (int i = 0; i < 100; ++i) {
                    const auto f = b.frame(size);
                    const auto box = b.bounds(size);
                    checkSurfaceBounds(box, size, f.window);
                    checkSurfaceBounds(box, size, f.shell);
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

    void sphericalCoverage() {
        constexpr double pi = 3.14159265358979323846;
        for (std::uint64_t seed = 0; seed < 32; ++seed) {
            Bubble b;
            b.begin(Transition::Open, .62, seed, SIZE);
            advanceFor(b, .62 * .55);
            const auto s = b.frame(SIZE).shell;
            for (int y = 0; y <= 20; ++y) {
                const double z = 1 - y * .1;
                const double radial = std::sqrt(std::max(0.0, 1 - z * z));
                for (int x = 0; x < 40; ++x) {
                    const double angle = 2 * pi * x / 40;
                    const Vec3 p{radial * std::cos(angle), radial * std::sin(angle), z};
                    bool covered = false;
                    for (std::size_t i = 0; i < s.holeCount; ++i) {
                        const auto &h = s.holes[i];
                        const double d = std::acos(std::clamp(
                            p.x * h.centre.x + p.y * h.centre.y + p.z * h.centre.z, -1.0, 1.0));
                        covered |= d <= bubbleHoleBoundary(h, bubbleHoleAzimuth(h, p, true));
                    }
                    CHECK(covered);
                }
            }
        }
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
                auto surface = opening ? frame.shell : frame.window;
                std::array<BubbleImpact, kBubbleMaxImpacts> impacts{};
                const auto count = bubbleImpacts(surface, impacts);
                CHECK(count <= (opening ? 3u : 2u));
                if (opening) CHECK(count >= 1);
                for (std::size_t i = 0; i < count; ++i)
                    CHECK(impacts[i].radius >= 1.5 && impacts[i].radius <= 3.5);
                if (!count) continue;
                CHECK(impacts[0].delay > .00002);
                advanceFor(b, impacts[0].delay - .00001);
                frame = b.frame({900, 560});
                surface = opening ? frame.shell : frame.window;
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
                surface = opening ? frame.shell : frame.window;
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
        bool flake = false, burst = false;
        for (int frame = 0; frame < 101; ++frame) {
            const auto s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, current);
            peak = std::max(peak, count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &p = current[i];
                flake |= p.particle.thickness > 0;
                burst |= p.id >= 1000 && p.id < 2000;
                for (std::size_t j = 0; j < oldCount; ++j) {
                    if (previous[j].id != p.id) continue;
                    const auto &q = previous[j].particle;
                    CHECK(std::hypot(p.particle.centre.x - q.centre.x,
                              p.particle.centre.y - q.centre.y) <= s.speed * .01 + 2);
                    if (p.id >= 2000 && p.id < 3000) {
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
        CHECK(flake && burst);
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
                const double boundary = bubbleHoleBoundary(h, phi);
                const Vec3 direction{std::cos(phi), std::sin(phi), 0};
                const Vec3 origin{
                    h.centre.x + boundary * direction.x, h.centre.y + boundary * direction.y, 0};
                const auto expected = bubbleCurlSample(
                    origin, direction, {0, 0, 1}, bubbleCurlSection(s, h, boundary, phi), 1);
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
        b.begin(Transition::Close, .42, 19, SIZE);
        std::array<detail::IdentifiedParticle, kBubbleMaxParticles> previous{}, current{};
        std::size_t oldCount = 0;
        bool contracted = false, substantial = false;
        for (int frame = 0; frame < 100; ++frame) {
            const auto s = b.frame(SIZE).window;
            const auto count = detail::bubbleParticlesWithIds(s, current);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &p = current[i];
                if (p.particle.thickness <= 0) continue;
                substantial |= p.particle.radius >= 5.5;
                CHECK(p.particle.radius <= 14);
                for (std::size_t j = 0; j < oldCount; ++j)
                    if (previous[j].id == p.id && previous[j].particle.thickness > 0) {
                        CHECK(p.particle.radius < previous[j].particle.radius);
                        contracted = true;
                    }
            }
            previous = current;
            oldCount = count;
            b.advance(.0021);
        }
        CHECK(substantial && contracted);
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
    pinchBirthIsContinuous();
    return g_failures == 0 ? 0 : 1;
}
