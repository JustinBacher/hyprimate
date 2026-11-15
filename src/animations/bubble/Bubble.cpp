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
    BubbleFrame f{};
    f.size = size;
    f.window.halfSize = {size.x * .5, size.y * .5};
    f.window.visibility = 1;
    return f;
}

void pop(BubbleSurface &surface, double progress) {
    surface.closing = true;
    surface.time = progress;
    const double corner = std::clamp(surface.corner, 0.0, std::min(surface.halfSize.x, surface.halfSize.y));
    const double farthest = std::hypot(surface.halfSize.x - corner + std::abs(surface.rupture.x),
                                       surface.halfSize.y - corner + std::abs(surface.rupture.y)) +
                            corner;
    surface.tear = farthest * 1.3 * smoothstep(.10, .84, progress);
    surface.retract = smoothstep(.12, .92, progress);
    surface.remnant = smoothstep(.45, .68, progress) * (1 - smoothstep(.86, 1.0, progress));
    if (progress >= 1)
        surface.visibility = 0;
}

} // namespace

void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
    if ((direction != Transition::Open && direction != Transition::Close) || !std::isfinite(duration) ||
        duration <= 0 || !usable(size)) {
        reset();
        return;
    }
    const bool   inherit = direction == Transition::Close && active_ && opening_;
    const auto   start = inherit ? frame(size) : neutral(size);
    const double materialSeed = inherit ? start.seed : unitDraw(seed);
    const double x = (.16 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
    const double y = (.12 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
    initial_ = start;
    size_ = size;
    duration_ = std::clamp(duration, .01, 60.0);
    elapsed_ = 0;
    seed_ = materialSeed;
    rupture_ = {x, y};
    active_ = true;
    opening_ = direction == Transition::Open;
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
    BubbleFrame f = neutral(size);
    if (!active_) {
        if (closed_)
            f.window.visibility = 0;
        return f;
    }
    const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
    f.seed = seed_;
    if (opening_) {
        const double shortSide = std::min(size.x, size.y);
        auto        &shell = f.shell;
        const double radius = shortSide * mix(.08, .46, smoothstep(0, .30, t));
        shell.halfSize = {radius, radius};
        shell.corner = shell.depth = radius;
        shell.content = .55 * smoothstep(.04, .26, t);
        shell.film = 1;
        shell.visibility = smoothstep(0, .13, t);
        shell.rupture = {rupture_.x * radius, rupture_.y * radius};
        if (t > .30)
            pop(shell, std::clamp((t - .30) / .36, 0.0, 1.0));

        // The window is a second membrane behind the rupturing, still round shell.
        auto        &window = f.window;
        const double windowRadius = shortSide * mix(.08, .40, smoothstep(.36, .52, t));
        const double flatten = smoothstep(.52, .68, t);
        const double splat = .045 * smoothstep(.55, .68, t) * (1 - smoothstep(.68, .76, t));
        window.halfSize = {mix(windowRadius, size.x * .5, flatten) + size.x * .5 * splat,
                           mix(windowRadius, size.y * .5, flatten) + size.y * .5 * splat};
        window.corner = mix(windowRadius, 0, flatten);
        window.depth = windowRadius * (1 - smoothstep(.52, .68, t));
        const double settle = std::clamp((t - .64) / .36, 0.0, 1.0);
        const double envelope =
            shortSide * .20 * smoothstep(.48, .64, t) * std::exp(-2.0 * settle) * (1 - smoothstep(.92, 1.0, t));
        constexpr double pi = 3.14159265358979323846;
        window.core = {envelope * (std::cos(3 * pi * settle) + 2 * std::sin(3 * pi * settle) / (3 * pi)),
                       envelope * .35 * std::sin(5 * pi * settle)};
        window.film = 1 - smoothstep(.56, 1.0, t);
        window.visibility = smoothstep(.36, .42, t);
        window.ripple = shortSide * .012 * smoothstep(.56, .68, t) * (1 - smoothstep(.68, .76, t));
        window.time = t;
        return f;
    }
    f = initial_;
    f.size = size;
    f.seed = seed_;
    const double sx = size.x / size_.x, sy = size.y / size_.y;
    const double scale = std::min(sx, sy);
    const auto   close = [&](BubbleSurface &surface, bool spherical) {
        const double xScale = spherical ? scale : sx, yScale = spherical ? scale : sy;
        surface.halfSize = {surface.halfSize.x * xScale, surface.halfSize.y * yScale};
        surface.corner *= scale;
        surface.depth *= scale;
        const double damping = scale * (1 - smoothstep(0, .5, t));
        surface.core = {surface.core.x * damping, surface.core.y * damping};
        surface.ripple *= scale * (1 - smoothstep(0, .35, t));
        const double start = surface.closing ? surface.time : 0;
        if (surface.closing)
            surface.rupture = {surface.rupture.x * xScale, surface.rupture.y * yScale};
        else
            surface.rupture = {rupture_.x * surface.halfSize.x, rupture_.y * surface.halfSize.y};
        pop(surface, mix(start, 1, t));
    };
    close(f.window, false);
    close(f.shell, true);
    return f;
}

Box Bubble::bounds(Vec2 size) const {
    if (!usable(size) || closed_)
        return {};
    const auto f = frame(size);
    const auto extent = [](const BubbleSurface &surface) {
        const double margin = 2 + std::abs(surface.ripple) +
                              (surface.closing ? .04 * std::min(surface.halfSize.x, surface.halfSize.y) : 0);
        return Vec2{surface.halfSize.x + margin, surface.halfSize.y + margin};
    };
    const auto window = extent(f.window), shell = extent(f.shell);
    const Vec2 half{std::max(window.x, shell.x), std::max(window.y, shell.y)};
    return {size.x * .5 - half.x, size.y * .5 - half.y, half.x * 2, half.y * 2};
}

} // namespace liquid_glass
