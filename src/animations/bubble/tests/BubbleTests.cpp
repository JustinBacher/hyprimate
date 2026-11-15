#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
using namespace liquid_glass;
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
        CHECK(f.shell.visibility > 0 && f.shell.tear > 0);
        CHECK(f.window.visibility > 0);
        CHECK(f.window.halfSize.x == f.window.halfSize.y && f.window.corner == f.window.halfSize.x);
        CHECK(f.window.halfSize.x < f.shell.halfSize.x);
        CHECK(f.shell.halfSize.x > shellRadius);

        for (int i = 0; i < 20; ++i) {
            f = b.frame(size);
            if (f.shell.visibility > 0) {
                CHECK(f.shell.halfSize.x == f.shell.halfSize.y);
                CHECK(f.shell.corner == f.shell.halfSize.x && f.shell.ripple == 0);
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
    CHECK(f.window.corner == 0 && f.window.depth == 0 && f.window.film == 0 && f.window.ripple == 0);
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
        CHECK(b.frame(size).window.core.x < 0); // pressure recoils through the screen plane
        advanceFor(b, 0.02);
        const auto landed = b.frame(size).window;
        CHECK(landed.halfSize.x == size.x / 2 && landed.halfSize.y == size.y / 2);
        CHECK(landed.corner == 0 && landed.depth == 0 && landed.ripple == 0);
        CHECK(landed.content == 1);
        CHECK(std::abs(landed.core.x) + std::abs(landed.core.y) > 0);
        advanceFor(b, 0.10);
        CHECK(b.frame(size).window.core.x > 0); // a smaller rebound after the boundary has stopped
        CHECK(b.frame(size).window.core.x < impact);
        advanceFor(b, 0.10);
        CHECK(std::abs(b.frame(size).window.core.x) + std::abs(b.frame(size).window.core.y) < impact * 0.1);
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
    if (before.tear > 0) {
        CHECK(before.rupture.x == after.rupture.x && before.rupture.y == after.rupture.y);
        CHECK(before.tear == after.tear && before.retract == after.retract && before.remnant == after.remnant);
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
        if (t == 0.44 || t == 0.55) {
            CHECK(before.shell.visibility > 0 && before.window.visibility > 0 && before.shell.tear > 0);
            advanceFor(b, 0.05);
            const auto progressing = b.frame(SIZE);
            CHECK(progressing.shell.tear >= before.shell.tear);
            CHECK(progressing.shell.retract >= before.shell.retract);
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
    CHECK(original.shell.visibility > 0 && original.shell.tear > 0);
    CHECK(resized.shell.halfSize.x == original.shell.halfSize.x * 0.5);
    CHECK(resized.shell.halfSize.y == original.shell.halfSize.y * 0.5);
    CHECK(resized.shell.corner == original.shell.corner * 0.5);
    CHECK(resized.shell.rupture.x == original.shell.rupture.x * 0.5);
    CHECK(resized.shell.rupture.y == original.shell.rupture.y * 0.5);
    CHECK(std::abs(resized.shell.tear - original.shell.tear * 0.5) < 1e-9);
    CHECK(resized.window.halfSize.x == original.window.halfSize.x * 1.5);
    CHECK(resized.window.halfSize.y == original.window.halfSize.y * 0.5);
}

void ruptureCoversCorners() {
    for (Vec2 size : {Vec2{1800, 200}, Vec2{200, 1800}, SIZE}) {
        Bubble b;
        b.begin(Transition::Close, 1, 19, size);
        auto f = b.frame(size).window;
        CHECK(f.visibility == 1 && b.frame(size).shell.visibility == 0);
        CHECK(f.rupture.x != 0 && f.rupture.y != 0);
        CHECK(f.tear == 0);
        advanceFor(b, 0.9);
        f = b.frame(size).window;
        const double farthest = std::hypot(size.x / 2 + std::abs(f.rupture.x), size.y / 2 + std::abs(f.rupture.y));
        CHECK(f.tear >= farthest);
        CHECK(f.retract > 0 && f.retract <= 1);
        advanceFor(b, 0.2);
        CHECK(b.closed() && !b.active());
        CHECK(b.frame(size).window.visibility == 0 && b.frame(size).shell.visibility == 0);
        const Box bounds = b.bounds(size);
        CHECK(bounds.w == 0 && bounds.h == 0);
    }
}

void checkSurfaceBounds(const Box &box, Vec2 size, const BubbleSurface &surface) {
    if (surface.visibility <= 0)
        return;
    CHECK(box.x <= size.x / 2 - surface.halfSize.x - std::abs(surface.ripple));
    CHECK(box.y <= size.y / 2 - surface.halfSize.y - std::abs(surface.ripple));
    CHECK(box.x + box.w >= size.x / 2 + surface.halfSize.x + std::abs(surface.ripple));
    CHECK(box.y + box.h >= size.y / 2 + surface.halfSize.y + std::abs(surface.ripple));
}

void boundsContainTheSurfaces() {
    for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
        Bubble b;
        b.begin(Transition::Open, 1, 8, size);
        const auto initial = b.frame(size);
        CHECK(initial.window.visibility == 0 && initial.shell.visibility == 0);
        CHECK(b.bounds(size).w > 0 && b.bounds(size).h > 0);
        for (int i = 0; i < 100; ++i) {
            const auto f = b.frame(size);
            const auto box = b.bounds(size);
            checkSurfaceBounds(box, size, f.window);
            checkSurfaceBounds(box, size, f.shell);
            b.advance(0.01);
        }
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

void deterministicAndInvalidInput() {
    Bubble a, b;
    a.begin(Transition::Open, 1, 55, SIZE);
    b.begin(Transition::Open, 1, 55, SIZE);
    advanceFor(a, 0.84, 0.01);
    advanceFor(b, 0.84, 0.02);
    auto x = a.frame(SIZE), y = b.frame(SIZE);
    CHECK(std::abs(x.window.halfSize.x - y.window.halfSize.x) < 1e-9);
    CHECK(std::abs(x.window.depth - y.window.depth) < 1e-9 && x.seed == y.seed);
    CHECK(std::abs(x.window.core.x - y.window.core.x) < 1e-9 && std::abs(x.window.core.y - y.window.core.y) < 1e-9);
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
} // namespace

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
    return g_failures == 0 ? 0 : 1;
}
