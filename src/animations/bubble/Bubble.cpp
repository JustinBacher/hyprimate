#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {

        // Reject unrealistically large dimensions as well as non-finite values so all
        // derived distances and bounds remain representable.
        bool usable(Vec2 size) {
            constexpr double LIMIT = 1.0e9;
            return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
                   size.x <= LIMIT && size.y <= LIMIT;
        }

        double mix(double a, double b, double t) {
            return a + ((b - a) * t);
        }

        BubbleFrame neutral(Vec2 size) {
            BubbleFrame f{};
            f.size = size;
            f.window.halfSize = {.x = size.x * .5, .y = size.y * .5};
            f.window.visibility = 1;
            return f;
        }

        void updateRupture(BubbleSurface &surface) {
            for (std::size_t i = 0; i < surface.holeCount; ++i) {
                auto &h = surface.holes[i];
                const double AGE = std::max(0.0, surface.clock - h.start);
                const double TRANSIENT = surface.spherical ? .05 : .075;
                h.radius = surface.speed * (AGE - (TRANSIENT * (1 - std::exp(-AGE / TRANSIENT))));
                h.precursor = 1 - smoothstep(h.start, h.start + .018, surface.clock);
                h.precursor *=
                    smoothstep(h.start - (i == 0 ? (surface.spherical ? .10 : .15) : .06),
                        h.start,
                        surface.clock);
                h.rim = std::clamp(
                    1.2 +
                        (.12 * std::sqrt(h.radius * (surface.spherical ? surface.halfSize.x : 1))),
                    1.2,
                    4.5);
                const double FINGERS = smoothstep(.012, .045, AGE);
                for (std::size_t m = 0; m < h.amplitude.size(); ++m) {
                    h.amplitude[m] = FINGERS * (.014 - (.002 * m));
                }
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
            s.ruptureVisibility = s.visibility;
            s.holeCount = 3;
            if (!sphere && s.thickness <= 0) {
                s.thickness = 380 + (270 * unit_draw(seed));
            }
            const double x = spot.x;
            const double y = spot.y;
            if (sphere) {
                s.holes[0].centre = {
                    .x = x, .y = -.55, .z = std::sqrt(std::max(0.0, 1 - x * x - .55 * .55))};
                const double TURN = (unit_draw(seed) - .5) * .5;
                s.holes[1].centre = {.x = (-.5 * std::cos(TURN)) - (.45 * std::sin(TURN)),
                    .y = (-.5 * std::sin(TURN)) + (.45 * std::cos(TURN)),
                    .z = -.74};
                s.holes[2].centre = {.x = (.55 * std::cos(TURN)) - (.35 * std::sin(TURN)),
                    .y = (.55 * std::sin(TURN)) + (.35 * std::cos(TURN)),
                    .z = -.76};
                for (auto &h : s.holes) {
                    const double n =
                        std::sqrt((h.centre.x * h.centre.x) + (h.centre.y * h.centre.y) +
                                  (h.centre.z * h.centre.z));
                    if (n > 0) {
                        h.centre = {.x = h.centre.x / n, .y = h.centre.y / n, .z = h.centre.z / n};
                    }
                }
                s.speed = 12.5;
            } else {
                s.holes[0].centre = {
                    .x = x * s.halfSize.x, .y = -std::abs(y) * s.halfSize.y, .z = 0};
                const double FIRST = .72 + (.04 * unit_draw(seed));
                const double SECOND = .72 + (.04 * unit_draw(seed));
                const double CROSS1 = .5 + (.1 * unit_draw(seed));
                const double CROSS2 = .5 + (.1 * unit_draw(seed));
                if (s.halfSize.x >= s.halfSize.y) {
                    s.holes[1].centre = {
                        .x = -s.halfSize.x * FIRST, .y = s.halfSize.y * CROSS1, .z = 0};
                    s.holes[2].centre = {
                        .x = s.halfSize.x * SECOND, .y = s.halfSize.y * CROSS2, .z = 0};
                } else {
                    s.holes[1].centre = {
                        .x = s.halfSize.x * CROSS1, .y = s.halfSize.y * FIRST, .z = 0};
                    s.holes[2].centre = {
                        .x = -s.halfSize.x * CROSS2, .y = -s.halfSize.y * SECOND, .z = 0};
                }
                const double FARTHEST = std::hypot(s.halfSize.x + std::abs(s.holes[0].centre.x),
                    s.halfSize.y + std::abs(s.holes[0].centre.y));
                s.speed = FARTHEST * 2.5;
            }
            s.holes[0].start = sphere ? .31 : .15;
            s.holes[1].start = sphere ? .38 : .25;
            s.holes[2].start = sphere ? .42 : .31;
            for (std::size_t i = 0; i < s.holeCount; ++i) {
                for (double &phase : s.holes[i].phase) {
                    phase = 6.283185307179586 * unit_draw(seed);
                }
            }
            updateRupture(s);
        }

    }  // namespace

    void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
        if ((direction != Transition::Open && direction != Transition::Close) ||
            !std::isfinite(duration) || duration <= 0 || !usable(size)) {
            reset();
            return;
        }
        const bool FROM_OPENING = direction == Transition::Close && active_ && opening_;
        // A close takes over the film as it is: mid-opening, or still deformed by movement.
        const bool INHERIT = FROM_OPENING || (direction == Transition::Close && motion_.stirred());
        const auto START = INHERIT ? frame(size) : neutral(size);
        const double MATERIAL_SEED = FROM_OPENING ? START.seed : unit_draw(seed);
        const double x = (.16 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        const double y = (.12 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        initial_ = START;
        size_ = size;
        duration_ = std::clamp(duration, .01, 60.0);
        elapsed_ = 0;
        seed_ = MATERIAL_SEED;
        rupture_ = {.x = x, .y = y};
        flow_ = 0;
        // A closing window no longer moves; its deformation now lives in the close's start frame.
        if (direction == Transition::Close) {
            motion_.reset();
        }
        if (direction == Transition::Close && initial_.window.visibility > 0 &&
            !initial_.window.closing) {
            startRupture(initial_.window,
                rupture_,
                static_cast<std::uint64_t>(seed_ * 0x1p53),
                duration_,
                false);
        }
        active_ = true;
        opening_ = direction == Transition::Open;
        closed_ = false;
    }

    void Bubble::advance(double elapsed) {
        if (!active_ || !std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        // A stalled compositor should not skip the animation in a single frame.
        elapsed_ = std::min(duration_, elapsed_ + std::min(elapsed, STALL_SECONDS));
        if (opening_) {
            flow_ = elapsed_ * 2;
        }
        if (elapsed_ >= duration_ - (duration_ * 1e-12)) {
            elapsed_ = duration_;
            active_ = false;
            closed_ = !opening_;
        }
    }

    void Bubble::advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        const bool SETTLING = opening();
        advance(elapsed);
        if (closed_ || (active_ && !opening_)) {
            return;
        }
        transparency_ = std::clamp(params.transparency, 0.0, 1.0);
        motion_.advance(elapsed, size, body, params);
        // Once landed, the film keeps flowing at the pace the movement stirs it.
        if (!SETTLING) {
            flow_ += std::min(elapsed, STALL_SECONDS) * (.6 + std::min(motion_.speed() / 800, 3.0));
        }
    }

    void Bubble::reset() {
        initial_ = {};
        size_ = {};
        duration_ = elapsed_ = seed_ = 0;
        rupture_ = {};
        active_ = opening_ = closed_ = false;
    }

    void Bubble::drive(Vec2 displacement, bool carried) {
        motion_.drive(displacement, carried);
    }

    void Bubble::seedMotion(std::uint64_t seed) {
        motion_.seed(seed);
    }

    void Bubble::stopMotion() {
        motion_.reset();
    }

    bool Bubble::landed() const {
        return active_ && opening_ && elapsed_ >= .68 * duration_;
    }

    bool Bubble::stirred() const {
        return motion_.stirred();
    }

    const BubbleMotion &Bubble::motion() const {
        return motion_;
    }

    void Bubble::clearEmissions() {
        motion_.clearEmissions();
    }

    void Bubble::applyMotion(BubbleFrame &f) const {
        if (!motion_.stirred()) {
            return;
        }
        auto &window = f.window;
        window.motion = motion_.deformation();
        window.transparency = transparency_;
        const Vec2 CORE = motion_.core();
        window.core = {.x = window.core.x + CORE.x, .y = window.core.y + CORE.y};
        if (window.thickness <= 0) {
            window.thickness = 450;
        }
        window.flow = flow_;
        f.microCount = motion_.microbubbles(f.micro);
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
        if (!usable(size)) {
            return {};
        }
        BubbleFrame f = neutral(size);
        if (!active_) {
            if (closed_) {
                f.window.visibility = 0;
            } else {
                applyMotion(f);
            }
            return f;
        }
        const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
        f.seed = seed_;
        if (opening_) {
            const double SHORT_SIDE = std::min(size.x, size.y);
            auto &shell = f.shell;
            const double RADIUS = SHORT_SIDE * mix(.08, .46, smoothstep(0, .30, t));
            shell.halfSize = {.x = RADIUS, .y = RADIUS};
            shell.corner = shell.depth = RADIUS;
            shell.content = .55 * smoothstep(.04, .26, t);
            shell.film = 1;
            shell.visibility = smoothstep(0, .13, t);
            shell.spherical = true;
            shell.thickness = std::min(1500.0, 450 * std::pow(SHORT_SIDE * .46 / RADIUS, 2));
            shell.flow = elapsed_ * 2;
            startRupture(
                shell, rupture_, static_cast<std::uint64_t>(seed_ * 0x1p53), duration_, true);
            shell.clock = t;
            updateRupture(shell);
            if (t >= .55) {
                shell.visibility = 0;
            }

            // The window is a second membrane behind the rupturing, still round shell.
            auto &window = f.window;
            const double WINDOW_RADIUS = SHORT_SIDE * mix(.08, .40, smoothstep(.36, .52, t));
            const double FLATTEN = smoothstep(.52, .68, t);
            const double SPLAT = .045 * smoothstep(.55, .68, t) * (1 - smoothstep(.68, .76, t));
            window.halfSize = {
                .x = mix(WINDOW_RADIUS, size.x * .5, FLATTEN) + (size.x * .5 * SPLAT),
                .y = mix(WINDOW_RADIUS, size.y * .5, FLATTEN) + (size.y * .5 * SPLAT)};
            window.corner = mix(WINDOW_RADIUS, 0, FLATTEN);
            window.depth = WINDOW_RADIUS * (1 - smoothstep(.52, .68, t));
            const double SETTLE = std::clamp((t - .64) / .36, 0.0, 1.0);
            const double ENVELOPE = SHORT_SIDE * .20 * smoothstep(.48, .64, t) *
                                    std::exp(-2.0 * SETTLE) * (1 - smoothstep(.92, 1.0, t));
            constexpr double pi = 3.14159265358979323846;
            window.core = {.x = ENVELOPE * (std::cos(3 * pi * SETTLE) +
                                               (2 * std::sin(3 * pi * SETTLE) / (3 * pi))),
                .y = ENVELOPE * .35 * std::sin(5 * pi * SETTLE)};
            window.film = 1 - smoothstep(.56, 1.0, t);
            // The inner film stays clear while the outer bubble bursts, so no image appears with
            // the pop; the window's imagery develops as the film flattens and lands.
            window.content = smoothstep(.50, .78, t);
            window.thickness = 450;
            window.flow = elapsed_ * 2;
            window.visibility = smoothstep(.36, .42, t);
            window.ripple =
                SHORT_SIDE * .012 * smoothstep(.56, .68, t) * (1 - smoothstep(.68, .76, t));
            window.time = t;
            applyMotion(f);
            return f;
        }
        f = initial_;
        f.size = size;
        f.seed = seed_;
        const double sx = size.x / size_.x;
        const double sy = size.y / size_.y;
        const double SCALE = std::min(sx, sy);
        const auto CLOSE = [&](BubbleSurface &surface, bool spherical) {
            const double X_SCALE = spherical ? SCALE : sx;
            const double Y_SCALE = spherical ? SCALE : sy;
            surface.halfSize = {
                .x = surface.halfSize.x * X_SCALE, .y = surface.halfSize.y * Y_SCALE};
            surface.corner *= SCALE;
            surface.depth *= SCALE;
            const double DAMPING = SCALE * (1 - smoothstep(0, .5, t));
            surface.core = {.x = surface.core.x * DAMPING, .y = surface.core.y * DAMPING};
            surface.ripple *= SCALE * (1 - smoothstep(0, .35, t));
            // The film relaxes from the movement's deformation as it starts to tear.
            auto &motion = surface.motion;
            motion.drag = {.x = motion.drag.x * DAMPING, .y = motion.drag.y * DAMPING};
            for (std::size_t i = 0; i < motion.waveCos.size(); ++i) {
                motion.waveCos[i] *= DAMPING;
                motion.waveSin[i] *= DAMPING;
            }
            motion.sheen *= 1 - smoothstep(0, .5, t);
            // A bubble that closes while contracted bursts that way: its shape is the film being
            // torn.
            motion.extent = {.x = motion.extent.x * X_SCALE, .y = motion.extent.y * Y_SCALE};
            if (!surface.closing) {
                if (surface.visibility <= 0) {
                    return;
                }
                startRupture(surface,
                    rupture_,
                    static_cast<std::uint64_t>(seed_ * 0x1p53),
                    duration_,
                    spherical);
            } else {
                for (std::size_t i = 0; i < surface.holeCount; ++i) {
                    auto &h = surface.holes[i];
                    if (!spherical) {
                        h.centre.x *= X_SCALE;
                        h.centre.y *= Y_SCALE;
                    }
                }
                // Flat radii use the larger scale to preserve coverage after an anisotropic resize.
                if (!spherical) {
                    surface.speed *= std::max(sx, sy);
                }
            }
            surface.clock += elapsed_ * surface.rate;
            surface.flow += elapsed_;
            updateRupture(surface);
        };
        CLOSE(f.window, false);
        CLOSE(f.shell, true);
        // Microbubbles on the window's film burst with it.
        for (std::size_t i = 0; i < f.microCount; ++i) {
            auto &m = f.micro[i];
            m.centre = {.x = m.centre.x * sx, .y = m.centre.y * sy};
            m.anchor = {.x = m.anchor.x * sx, .y = m.anchor.y * sy};
            m.collapse = std::max(m.collapse, smoothstep(0, .25, t));
        }
        return f;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || closed_) {
            return {};
        }
        const auto f = frame(size);
        // Each membrane's box around where it is drawn; a carried film is offset from the window.
        const auto EXTENT = [](const BubbleSurface &surface) {
            double reach = std::hypot(surface.motion.drag.x, surface.motion.drag.y);
            for (std::size_t i = 0; i < surface.motion.waveCos.size(); ++i) {
                reach += std::hypot(surface.motion.waveCos[i], surface.motion.waveSin[i]);
            }
            // A contracted membrane may reach past its rectangle where its shape bulges; one
            // overshooting back swells slightly past it everywhere.
            const Vec2 ROUND = bubbleRoundHalf(surface.halfSize, surface.motion);
            reach += std::max(0.0, -surface.motion.round) *
                     std::max(surface.halfSize.x, surface.halfSize.y);
            const Vec2 BASE{.x = std::max(surface.halfSize.x, ROUND.x),
                .y = std::max(surface.halfSize.y, ROUND.y)};
            const double MARGIN = 8 + std::abs(surface.ripple) + reach +
                                  (surface.closing ? 1.2 * std::min(BASE.x, BASE.y) : 0);
            const Vec2 HALF{.x = BASE.x + MARGIN, .y = BASE.y + MARGIN};
            const Vec2 at = surface.motion.carry;
            return std::array<double, 4>{
                at.x - HALF.x, at.y - HALF.y, at.x + HALF.x, at.y + HALF.y};
        };
        auto box = EXTENT(f.window);
        const auto SHELL = EXTENT(f.shell);
        for (std::size_t i = 0; i < 2; ++i) {
            box[i] = std::min(box[i], SHELL[i]);
            box[i + 2] = std::max(box[i + 2], SHELL[i + 2]);
        }
        for (std::size_t i = 0; i < f.microCount; ++i) {
            const auto &m = f.micro[i];
            const double r = (m.radius * 1.4) + 3;
            box = {std::min(box[0], m.centre.x - r),
                std::min(box[1], m.centre.y - r),
                std::max(box[2], m.centre.x + r),
                std::max(box[3], m.centre.y + r)};
        }
        return {.x = (size.x * .5) + box[0],
            .y = (size.y * .5) + box[1],
            .w = box[2] - box[0],
            .h = box[3] - box[1]};
    }

}  // namespace hyprimate
