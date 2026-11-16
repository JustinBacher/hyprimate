#include <algorithm>
#include <cmath>

#include "animations/bubble/Bubble.hpp"

namespace hyprimate {
    namespace {

        // Reject unrealistically large dimensions as well as non-finite values so all
        // derived distances and bounds remain representable.
        bool usable(Vec2 size) {
            constexpr double limit = 1.0e9;
            return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
                   size.x <= limit && size.y <= limit;
        }

        double mix(double a, double b, double t) {
            return a + (b - a) * t;
        }

        BubbleFrame neutral(Vec2 size) {
            BubbleFrame f{};
            f.size = size;
            f.window.halfSize = {size.x * .5, size.y * .5};
            f.window.visibility = 1;
            return f;
        }

        void updateRupture(BubbleSurface &surface) {
            for (std::size_t i = 0; i < surface.holeCount; ++i) {
                auto &h = surface.holes[i];
                const double age = std::max(0.0, surface.clock - h.start);
                const double transient = surface.spherical ? .05 : .075;
                h.radius = surface.speed * (age - transient * (1 - std::exp(-age / transient)));
                h.precursor = 1 - smoothstep(h.start, h.start + .018, surface.clock);
                h.precursor *=
                    smoothstep(h.start - (i == 0 ? (surface.spherical ? .10 : .15) : .06),
                        h.start,
                        surface.clock);
                h.rim = std::clamp(
                    1.2 + .12 * std::sqrt(h.radius * (surface.spherical ? surface.halfSize.x : 1)),
                    1.2,
                    4.5);
                const double fingers = smoothstep(.012, .045, age);
                for (std::size_t m = 0; m < h.amplitude.size(); ++m)
                    h.amplitude[m] = fingers * (.014 - .002 * m);
            }
        }

        void startRupture(
            BubbleSurface &s, Vec2 spot, std::uint64_t seed, double duration, bool sphere) {
            s.closing = true;
            s.spherical = sphere;
            s.clock = 0;
            s.rate = 1 / duration;
            s.debris = seed;
            s.aureole = 1;
            s.holeCount = 3;
            if (!sphere && s.thickness <= 0) s.thickness = 380 + 270 * unitDraw(seed);
            const double x = spot.x, y = spot.y;
            if (sphere) {
                s.holes[0].centre = {x, -.55, std::sqrt(std::max(0.0, 1 - x * x - .55 * .55))};
                const double turn = (unitDraw(seed) - .5) * .5;
                s.holes[1].centre = {-.5 * std::cos(turn) - .45 * std::sin(turn),
                    -.5 * std::sin(turn) + .45 * std::cos(turn),
                    -.74};
                s.holes[2].centre = {.55 * std::cos(turn) - .35 * std::sin(turn),
                    .55 * std::sin(turn) + .35 * std::cos(turn),
                    -.76};
                for (auto &h : s.holes) {
                    const double n = std::sqrt(h.centre.x * h.centre.x + h.centre.y * h.centre.y +
                                               h.centre.z * h.centre.z);
                    if (n > 0) h.centre = {h.centre.x / n, h.centre.y / n, h.centre.z / n};
                }
                s.speed = 12.5;
            } else {
                s.holes[0].centre = {x * s.halfSize.x, -std::abs(y) * s.halfSize.y, 0};
                const double first = .72 + .04 * unitDraw(seed),
                             second = .72 + .04 * unitDraw(seed);
                const double cross1 = .5 + .1 * unitDraw(seed), cross2 = .5 + .1 * unitDraw(seed);
                if (s.halfSize.x >= s.halfSize.y) {
                    s.holes[1].centre = {-s.halfSize.x * first, s.halfSize.y * cross1, 0};
                    s.holes[2].centre = {s.halfSize.x * second, s.halfSize.y * cross2, 0};
                } else {
                    s.holes[1].centre = {s.halfSize.x * cross1, s.halfSize.y * first, 0};
                    s.holes[2].centre = {-s.halfSize.x * cross2, -s.halfSize.y * second, 0};
                }
                const double farthest = std::hypot(s.halfSize.x + std::abs(s.holes[0].centre.x),
                    s.halfSize.y + std::abs(s.holes[0].centre.y));
                s.speed = farthest * 2.5;
            }
            s.holes[0].start = sphere ? .31 : .15;
            s.holes[1].start = sphere ? .38 : .25;
            s.holes[2].start = sphere ? .42 : .31;
            for (std::size_t i = 0; i < s.holeCount; ++i)
                for (double &phase : s.holes[i].phase) phase = 6.283185307179586 * unitDraw(seed);
            updateRupture(s);
        }

    }  // namespace

    void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
        if ((direction != Transition::Open && direction != Transition::Close) ||
            !std::isfinite(duration) || duration <= 0 || !usable(size)) {
            reset();
            return;
        }
        const bool inherit = direction == Transition::Close && active_ && opening_;
        const auto start = inherit ? frame(size) : neutral(size);
        const double materialSeed = inherit ? start.seed : unitDraw(seed);
        const double x = (.16 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
        const double y = (.12 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
        initial_ = start;
        size_ = size;
        duration_ = std::clamp(duration, .01, 60.0);
        elapsed_ = 0;
        seed_ = materialSeed;
        rupture_ = {x, y};
        if (direction == Transition::Close && initial_.window.visibility > 0 &&
            !initial_.window.closing)
            startRupture(initial_.window,
                rupture_,
                static_cast<std::uint64_t>(seed_ * 0x1p53),
                duration_,
                false);
        active_ = true;
        opening_ = direction == Transition::Open;
        closed_ = false;
    }

    void Bubble::advance(double elapsed) {
        if (!active_ || !std::isfinite(elapsed) || elapsed <= 0) return;
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

    bool Bubble::active() const {
        return active_;
    }
    bool Bubble::opening() const {
        return active_ && opening_;
    }
    bool Bubble::closed() const {
        return closed_;
    }

    BubbleFrame Bubble::frame(Vec2 size) const {
        if (!usable(size)) return {};
        BubbleFrame f = neutral(size);
        if (!active_) {
            if (closed_) f.window.visibility = 0;
            return f;
        }
        const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
        f.seed = seed_;
        if (opening_) {
            const double shortSide = std::min(size.x, size.y);
            auto &shell = f.shell;
            const double radius = shortSide * mix(.08, .46, smoothstep(0, .30, t));
            shell.halfSize = {radius, radius};
            shell.corner = shell.depth = radius;
            shell.content = .55 * smoothstep(.04, .26, t);
            shell.film = 1;
            shell.visibility = smoothstep(0, .13, t);
            shell.spherical = true;
            shell.thickness = std::min(1500.0, 450 * std::pow(shortSide * .46 / radius, 2));
            shell.flow = elapsed_ * 2;
            startRupture(
                shell, rupture_, static_cast<std::uint64_t>(seed_ * 0x1p53), duration_, true);
            shell.clock = t;
            updateRupture(shell);
            if (t >= .55) shell.visibility = 0;

            // The window is a second membrane behind the rupturing, still round shell.
            auto &window = f.window;
            const double windowRadius = shortSide * mix(.08, .40, smoothstep(.36, .52, t));
            const double flatten = smoothstep(.52, .68, t);
            const double splat = .045 * smoothstep(.55, .68, t) * (1 - smoothstep(.68, .76, t));
            window.halfSize = {mix(windowRadius, size.x * .5, flatten) + size.x * .5 * splat,
                mix(windowRadius, size.y * .5, flatten) + size.y * .5 * splat};
            window.corner = mix(windowRadius, 0, flatten);
            window.depth = windowRadius * (1 - smoothstep(.52, .68, t));
            const double settle = std::clamp((t - .64) / .36, 0.0, 1.0);
            const double envelope = shortSide * .20 * smoothstep(.48, .64, t) *
                                    std::exp(-2.0 * settle) * (1 - smoothstep(.92, 1.0, t));
            constexpr double pi = 3.14159265358979323846;
            window.core = {
                envelope * (std::cos(3 * pi * settle) + 2 * std::sin(3 * pi * settle) / (3 * pi)),
                envelope * .35 * std::sin(5 * pi * settle)};
            window.film = 1 - smoothstep(.56, 1.0, t);
            // The inner film stays clear while the outer bubble bursts, so no image appears with
            // the pop; the window's imagery develops as the film flattens and lands.
            window.content = smoothstep(.50, .78, t);
            window.thickness = 450;
            window.flow = elapsed_ * 2;
            window.visibility = smoothstep(.36, .42, t);
            window.ripple =
                shortSide * .012 * smoothstep(.56, .68, t) * (1 - smoothstep(.68, .76, t));
            window.time = t;
            return f;
        }
        f = initial_;
        f.size = size;
        f.seed = seed_;
        const double sx = size.x / size_.x, sy = size.y / size_.y;
        const double scale = std::min(sx, sy);
        const auto close = [&](BubbleSurface &surface, bool spherical) {
            const double xScale = spherical ? scale : sx, yScale = spherical ? scale : sy;
            surface.halfSize = {surface.halfSize.x * xScale, surface.halfSize.y * yScale};
            surface.corner *= scale;
            surface.depth *= scale;
            const double damping = scale * (1 - smoothstep(0, .5, t));
            surface.core = {surface.core.x * damping, surface.core.y * damping};
            surface.ripple *= scale * (1 - smoothstep(0, .35, t));
            if (!surface.closing) {
                if (surface.visibility <= 0) return;
                startRupture(surface,
                    rupture_,
                    static_cast<std::uint64_t>(seed_ * 0x1p53),
                    duration_,
                    spherical);
            } else {
                for (std::size_t i = 0; i < surface.holeCount; ++i) {
                    auto &h = surface.holes[i];
                    if (!spherical) {
                        h.centre.x *= xScale;
                        h.centre.y *= yScale;
                    }
                }
                // Flat radii use the larger scale to preserve coverage after an anisotropic resize.
                if (!spherical) surface.speed *= std::max(sx, sy);
            }
            surface.clock += elapsed_ * surface.rate;
            surface.flow += elapsed_;
            updateRupture(surface);
        };
        close(f.window, false);
        close(f.shell, true);
        return f;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || closed_) return {};
        const auto f = frame(size);
        const auto extent = [](const BubbleSurface &surface) {
            const double margin =
                8 + std::abs(surface.ripple) +
                (surface.closing ? .45 * std::min(surface.halfSize.x, surface.halfSize.y) : 0);
            return Vec2{surface.halfSize.x + margin, surface.halfSize.y + margin};
        };
        const auto window = extent(f.window), shell = extent(f.shell);
        const Vec2 half{std::max(window.x, shell.x), std::max(window.y, shell.y)};
        return {size.x * .5 - half.x, size.y * .5 - half.y, half.x * 2, half.y * 2};
    }

}  // namespace hyprimate
