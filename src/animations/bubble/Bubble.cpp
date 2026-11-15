#include "animations/bubble/Bubble.hpp"

#include <algorithm>
#include <cmath>

namespace liquid_glass {
namespace {

// Reject unrealistically large dimensions as well as non-finite values so all
// derived distances and bounds remain representable.
bool usable(Vec2 size) {
    constexpr double limit = 1.0e9;
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 && size.x <= limit &&
           size.y <= limit;
}

double mix(double a, double b, double t) { return a + (b - a) * t; }

BubbleFrame neutral(Vec2 size) {
    BubbleFrame f;
    f.size = size;
    f.halfSize = {size.x * 0.5, size.y * 0.5};
    return f;
}

} // namespace

void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
    if (!usable(size) || !std::isfinite(duration) || duration <= 0 ||
        (direction != Transition::Open && direction != Transition::Close)) {
        reset();
        return;
    }

    const bool   inherit = direction == Transition::Close && active_ && opening_;
    BubbleFrame  start = inherit ? frame(size) : neutral(size);
    const double materialSeed = inherit ? start.seed : unitDraw(seed);
    const double xMagnitude = 0.16 + 0.12 * unitDraw(seed);
    const double xSign = unitDraw(seed) < 0.5 ? -1.0 : 1.0;
    const double yMagnitude = 0.12 + 0.12 * unitDraw(seed);
    const double ySign = unitDraw(seed) < 0.5 ? -1.0 : 1.0;
    const double x = xMagnitude * xSign, y = yMagnitude * ySign;

    size_ = size;
    duration_ = std::clamp(duration, 0.01, 60.0);
    elapsed_ = 0;
    seed_ = materialSeed;
    initial_ = start;
    rupture_ = {x * start.halfSize.x, y * start.halfSize.y};
    opening_ = direction == Transition::Open;
    active_ = true;
    closed_ = false;
}

void Bubble::advance(double elapsed) {
    if (!active_ || !std::isfinite(elapsed) || elapsed <= 0)
        return;
    // A stalled compositor should not skip the animation in a single frame.
    elapsed_ = std::min(duration_, elapsed_ + std::min(elapsed, kStallSeconds));
    if (elapsed_ >= duration_ - duration_ * 1e-12) {
        elapsed_ = duration_;
        active_ = false;
        closed_ = !opening_;
    }
}

void Bubble::reset() {
    initial_ = {};
    size_ = {};
    duration_ = elapsed_ = seed_ = 0;
    rupture_ = {};
    active_ = opening_ = closed_ = false;
}

bool Bubble::active() const { return active_; }
bool Bubble::opening() const { return active_ && opening_; }
bool Bubble::closed() const { return closed_; }

BubbleFrame Bubble::frame(Vec2 size) const {
    if (!usable(size))
        return {};
    if (!active_) {
        BubbleFrame f = neutral(size);
        if (closed_) {
            f.closing = true;
            f.visibility = 0;
            f.retract = 1;
            f.time = 1;
        }
        return f;
    }

    const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
    if (opening_) {
        BubbleFrame f;
        f.size = size;
        f.time = t;
        f.seed = seed_;
        const double shortSide = std::min(size.x, size.y);
        const double radius = shortSide * mix(0.08, 0.42, smoothstep(0.0, 0.38, t));
        const double flatten = smoothstep(0.38, 0.78, t);
        const double splat = 0.045 * smoothstep(0.62, 0.79, t) * (1.0 - smoothstep(0.79, 1.0, t));
        f.halfSize = {mix(radius, size.x * 0.5, flatten) + size.x * 0.5 * splat,
                      mix(radius, size.y * 0.5, flatten) + size.y * 0.5 * splat};
        f.corner = mix(radius, 0.0, flatten);
        f.depth = radius * (1.0 - smoothstep(0.45, 0.96, t));
        f.content = smoothstep(0.08, 0.90, t);
        f.film = 1.0 - smoothstep(0.56, 1.0, t);
        f.visibility = smoothstep(0.0, 0.13, t);
        f.ripple = shortSide * 0.012 * smoothstep(0.60, 0.76, t) * (1.0 - smoothstep(0.76, 1.0, t));
        return f;
    }

    BubbleFrame  f = initial_;
    const double sx = size.x / size_.x, sy = size.y / size_.y;
    const double scale = std::min(sx, sy);
    f.size = size;
    f.halfSize = {initial_.halfSize.x * sx, initial_.halfSize.y * sy};
    f.corner = initial_.corner * scale;
    f.depth = initial_.depth * scale;
    f.ripple = initial_.ripple * scale * (1.0 - smoothstep(0.0, 0.35, t));
    f.seed = seed_;
    f.closing = true;
    f.time = t;
    f.rupture = {rupture_.x * sx, rupture_.y * sy};
    const double farthest = std::hypot(f.halfSize.x + std::abs(f.rupture.x), f.halfSize.y + std::abs(f.rupture.y));
    // Extra travel covers irregular edge modulation and rim thickness.
    f.tear = farthest * 1.3 * smoothstep(0.10, 0.84, t);
    f.retract = smoothstep(0.12, 0.92, t);
    f.remnant = smoothstep(0.45, 0.68, t) * (1.0 - smoothstep(0.86, 1.0, t));
    return f;
}

Box Bubble::bounds(Vec2 size) const {
    if (!usable(size) || closed_)
        return {};
    const BubbleFrame f = frame(size);
    // The shader displaces the silhouette by less than ripple and places droplets
    // within 1.04 times the short half-axis. Long windows need no tall empty buffer.
    const double margin = 2.0 + std::abs(f.ripple) + (f.closing ? 0.04 * std::min(f.halfSize.x, f.halfSize.y) : 0.0);
    return {size.x * 0.5 - f.halfSize.x - margin, size.y * 0.5 - f.halfSize.y - margin, 2.0 * (f.halfSize.x + margin),
            2.0 * (f.halfSize.y + margin)};
}

} // namespace liquid_glass
