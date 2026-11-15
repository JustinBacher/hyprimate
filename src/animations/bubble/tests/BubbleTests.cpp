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

void roundFormation() {
    for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
        Bubble b;
        b.begin(Transition::Open, 1, 42, size);
        advanceFor(b, 0.25);
        const auto f = b.frame(size);
        CHECK(f.halfSize.x == f.halfSize.y);
        CHECK(f.corner == f.halfSize.x);
        CHECK(f.depth > 0 && f.film > 0 && f.content < 1);
        CHECK(f.content > 0.08); // imagery must be visible while the silhouette is still spherical
        CHECK(f.windowMix == 0); // the spherical object must not reveal the destination window
        CHECK(f.visibility > 0 && f.visibility <= 1);
    }
}

void windowAppearsDuringFlattening() {
    Bubble b;
    b.begin(Transition::Open, 1, 42, SIZE);
    advanceFor(b, 0.38);
    CHECK(b.frame(SIZE).windowMix == 0);
    advanceFor(b, 0.22);
    CHECK(b.frame(SIZE).windowMix > 0 && b.frame(SIZE).windowMix < 1);
    advanceFor(b, 0.36);
    CHECK(b.frame(SIZE).windowMix == 1);
    b.reset();
    b.begin(Transition::Close, 1, 42, SIZE);
    CHECK(b.frame(SIZE).windowMix == 1); // an ordinary close still ruptures window content
}

void flatEndpointAndSplat() {
    Bubble b;
    b.begin(Transition::Open, 1, 3, SIZE);
    bool overshoot = false;
    for (int i = 0; i < 100; ++i) {
        b.advance(0.01);
        const auto f = b.frame(SIZE);
        overshoot |= f.halfSize.x > SIZE.x / 2 && f.halfSize.y > SIZE.y / 2;
        CHECK(f.halfSize.x <= SIZE.x * 0.54 && f.halfSize.y <= SIZE.y * 0.54);
    }
    const auto f = b.frame(SIZE);
    CHECK(overshoot);
    CHECK(!b.active() && !b.closed());
    CHECK(f.halfSize.x == SIZE.x / 2 && f.halfSize.y == SIZE.y / 2);
    CHECK(f.corner == 0 && f.depth == 0 && f.film == 0 && f.ripple == 0);
    CHECK(f.content == 1 && f.visibility == 1 && f.windowMix == 1);
    CHECK(f.core.x == 0 && f.core.y == 0);
}

void fluidCoreOutlivesTheLanding() {
    for (Vec2 size : {Vec2{1400, 300}, Vec2{300, 1400}, SIZE}) {
        Bubble b;
        b.begin(Transition::Open, 1, 3, size);
        advanceFor(b, 0.64);
        const double impact = b.frame(size).core.x;
        CHECK(impact > 0);
        advanceFor(b, 0.12);
        CHECK(b.frame(size).core.x < 0); // pressure recoils through the screen plane
        advanceFor(b, 0.02);
        const auto landed = b.frame(size);
        CHECK(landed.halfSize.x == size.x / 2 && landed.halfSize.y == size.y / 2);
        CHECK(landed.corner == 0 && landed.depth == 0 && landed.ripple == 0);
        CHECK(landed.content == 1 && landed.windowMix == 1);
        CHECK(std::abs(landed.core.x) + std::abs(landed.core.y) > 0);
        advanceFor(b, 0.10);
        CHECK(b.frame(size).core.x > 0); // a smaller rebound after the boundary has stopped
        CHECK(b.frame(size).core.x < impact);
        advanceFor(b, 0.10);
        CHECK(std::abs(b.frame(size).core.x) + std::abs(b.frame(size).core.y) < impact * 0.1);
        advanceFor(b, 0.02);
        CHECK(b.frame(size).core.x == 0 && b.frame(size).core.y == 0);
        b.begin(Transition::Close, 1, 9, size);
        CHECK(b.frame(size).core.x == 0 && b.frame(size).core.y == 0);
    }
}

void interruptedCloseIsContinuous() {
    for (double t : {0.0, 0.2, 0.55, 0.85}) {
        Bubble b;
        b.begin(Transition::Open, 1, 9, SIZE);
        advanceFor(b, t);
        auto before = b.frame(SIZE);
        b.begin(Transition::Close, 1, 11, SIZE);
        auto after = b.frame(SIZE);
        CHECK(after.closing && b.active() && !b.opening());
        CHECK(before.halfSize.x == after.halfSize.x && before.halfSize.y == after.halfSize.y);
        CHECK(before.corner == after.corner && before.depth == after.depth);
        CHECK(before.core.x == after.core.x && before.core.y == after.core.y);
        const auto scaled = b.frame({SIZE.x * 1.25, SIZE.y * 1.25});
        CHECK(scaled.core.x == before.core.x * 1.25 && scaled.core.y == before.core.y * 1.25);
        CHECK(before.content == after.content && before.film == after.film);
        CHECK(before.windowMix == after.windowMix);
        CHECK(before.seed == after.seed);
        CHECK(before.visibility == after.visibility && before.ripple == after.ripple);
        CHECK(after.tear == 0 && after.retract == 0);
        advanceFor(b, 0.01);
        after = b.frame(SIZE);
        CHECK(std::abs(before.halfSize.x - after.halfSize.x) < 1.0);
        CHECK(after.halfSize.x <= before.halfSize.x && after.halfSize.y <= before.halfSize.y);
        CHECK(before.windowMix == after.windowMix);
    }
}

void ruptureCoversCorners() {
    for (Vec2 size : {Vec2{1800, 200}, Vec2{200, 1800}, SIZE}) {
        Bubble b;
        b.begin(Transition::Close, 1, 19, size);
        auto f = b.frame(size);
        CHECK(f.rupture.x != 0 && f.rupture.y != 0);
        CHECK(f.tear == 0);
        advanceFor(b, 0.9);
        f = b.frame(size);
        const double farthest = std::hypot(size.x / 2 + std::abs(f.rupture.x), size.y / 2 + std::abs(f.rupture.y));
        CHECK(f.tear >= farthest);
        CHECK(f.retract > 0 && f.retract <= 1);
        advanceFor(b, 0.2);
        CHECK(b.closed() && !b.active());
        const Box bounds = b.bounds(size);
        CHECK(bounds.w == 0 && bounds.h == 0);
    }
}

void boundsContainTheSurface() {
    Bubble b;
    b.begin(Transition::Open, 1, 8, SIZE);
    for (int i = 0; i < 100; ++i) {
        const auto f = b.frame(SIZE);
        const auto box = b.bounds(SIZE);
        CHECK(box.x <= SIZE.x / 2 - f.halfSize.x - std::abs(f.ripple));
        CHECK(box.y <= SIZE.y / 2 - f.halfSize.y - std::abs(f.ripple));
        CHECK(box.x + box.w >= SIZE.x / 2 + f.halfSize.x + std::abs(f.ripple));
        CHECK(box.y + box.h >= SIZE.y / 2 + f.halfSize.y + std::abs(f.ripple));
        b.advance(0.01);
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
    CHECK(std::abs(x.halfSize.x - y.halfSize.x) < 1e-9);
    CHECK(std::abs(x.depth - y.depth) < 1e-9 && x.seed == y.seed);
    CHECK(std::abs(x.core.x - y.core.x) < 1e-9 && std::abs(x.core.y - y.core.y) < 1e-9);
    a.advance(-1);
    a.advance(std::numeric_limits<double>::quiet_NaN());
    CHECK(a.frame(SIZE).halfSize.x == x.halfSize.x);
    a.advance(30);
    CHECK(std::isfinite(a.frame(SIZE).halfSize.x));
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
    roundFormation();
    windowAppearsDuringFlattening();
    flatEndpointAndSplat();
    fluidCoreOutlivesTheLanding();
    interruptedCloseIsContinuous();
    ruptureCoversCorners();
    boundsContainTheSurface();
    wideWindowsKeepTightVerticalBounds();
    deterministicAndInvalidInput();
    return g_failures == 0 ? 0 : 1;
}
