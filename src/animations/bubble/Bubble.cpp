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
            BubbleFrame frame{};
            frame.size = size;
            frame.window.halfSize = {.x = size.x * .5, .y = size.y * .5};
            frame.window.visibility = 1;
            return frame;
        }

        void updateRupture(BubbleSurface &surface) {
            for (std::size_t i = 0; i < surface.holeCount; ++i) {
                auto &hole = surface.holes[i];
                const double AGE = std::max(0.0, surface.clock - hole.start);
                const double TRANSIENT = surface.spherical ? .05 : .075;
                hole.radius =
                    surface.speed * (AGE - (TRANSIENT * (1 - std::exp(-AGE / TRANSIENT))));
                hole.precursor = 1 - smoothstep(hole.start, hole.start + .018, surface.clock);
                hole.precursor *=
                    smoothstep(hole.start - (i == 0 ? (surface.spherical ? .10 : .15) : .06),
                        hole.start,
                        surface.clock);
                hole.rim = std::clamp(
                    1.2 + (.12 * std::sqrt(
                                     hole.radius * (surface.spherical ? surface.halfSize.x : 1))),
                    1.2,
                    4.5);
                const double FINGERS = smoothstep(.012, .045, AGE);
                for (std::size_t mode = 0; mode < hole.amplitude.size(); ++mode) {
                    hole.amplitude[mode] = FINGERS * (.014 - (.002 * mode));
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
            const double SPOT_X = spot.x;
            const double SPOT_Y = spot.y;
            if (sphere) {
                s.holes[0].centre = {.x = SPOT_X,
                    .y = -.55,
                    .z = std::sqrt(std::max(0.0, 1 - SPOT_X * SPOT_X - .55 * .55))};
                const double TURN = (unit_draw(seed) - .5) * .5;
                s.holes[1].centre = {.x = (-.5 * std::cos(TURN)) - (.45 * std::sin(TURN)),
                    .y = (-.5 * std::sin(TURN)) + (.45 * std::cos(TURN)),
                    .z = -.74};
                s.holes[2].centre = {.x = (.55 * std::cos(TURN)) - (.35 * std::sin(TURN)),
                    .y = (.55 * std::sin(TURN)) + (.35 * std::cos(TURN)),
                    .z = -.76};
                for (auto &hole : s.holes) {
                    const double NORM = std::sqrt((hole.centre.x * hole.centre.x) +
                                                  (hole.centre.y * hole.centre.y) +
                                                  (hole.centre.z * hole.centre.z));
                    if (NORM > 0) {
                        hole.centre = {.x = hole.centre.x / NORM,
                            .y = hole.centre.y / NORM,
                            .z = hole.centre.z / NORM};
                    }
                }
                s.speed = 12.5;
            } else {
                s.holes[0].centre = {
                    .x = SPOT_X * s.halfSize.x, .y = -std::abs(SPOT_Y) * s.halfSize.y, .z = 0};
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
        const double SPOT_X = (.16 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        const double SPOT_Y = (.12 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        initial_ = START;
        size_ = size;
        duration_ = std::clamp(duration, .01, 60.0);
        elapsed_ = 0;
        seed_ = MATERIAL_SEED;
        rupture_ = {.x = SPOT_X, .y = SPOT_Y};
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
        BubbleFrame resultFrame = neutral(size);
        if (!active_) {
            if (closed_) {
                resultFrame.window.visibility = 0;
            } else {
                applyMotion(resultFrame);
            }
            return resultFrame;
        }
        const double PROGRESS = std::clamp(elapsed_ / duration_, 0.0, 1.0);
        resultFrame.seed = seed_;
        if (opening_) {
            const double SHORT_SIDE = std::min(size.x, size.y);
            auto &shell = resultFrame.shell;
            const double RADIUS = SHORT_SIDE * mix(.08, .46, smoothstep(0, .30, PROGRESS));
            shell.halfSize = {.x = RADIUS, .y = RADIUS};
            shell.corner = shell.depth = RADIUS;
            shell.content = .55 * smoothstep(.04, .26, PROGRESS);
            shell.film = 1;
            shell.visibility = smoothstep(0, .13, PROGRESS);
            shell.spherical = true;
            shell.thickness = std::min(1500.0, 450 * std::pow(SHORT_SIDE * .46 / RADIUS, 2));
            shell.flow = elapsed_ * 2;
            startRupture(
                shell, rupture_, static_cast<std::uint64_t>(seed_ * 0x1p53), duration_, true);
            shell.clock = PROGRESS;
            updateRupture(shell);
            if (PROGRESS >= .55) {
                shell.visibility = 0;
            }

            // The window is a second membrane behind the rupturing, still round shell.
            auto &window = resultFrame.window;
            const double WINDOW_RADIUS = SHORT_SIDE * mix(.08, .40, smoothstep(.36, .52, PROGRESS));
            const double FLATTEN = smoothstep(.52, .68, PROGRESS);
            const double SPLAT =
                .045 * smoothstep(.55, .68, PROGRESS) * (1 - smoothstep(.68, .76, PROGRESS));
            window.halfSize = {
                .x = mix(WINDOW_RADIUS, size.x * .5, FLATTEN) + (size.x * .5 * SPLAT),
                .y = mix(WINDOW_RADIUS, size.y * .5, FLATTEN) + (size.y * .5 * SPLAT)};
            window.corner = mix(WINDOW_RADIUS, 0, FLATTEN);
            window.depth = WINDOW_RADIUS * (1 - smoothstep(.52, .68, PROGRESS));
            const double SETTLE = std::clamp((PROGRESS - .64) / .36, 0.0, 1.0);
            const double ENVELOPE = SHORT_SIDE * .20 * smoothstep(.48, .64, PROGRESS) *
                                    std::exp(-2.0 * SETTLE) * (1 - smoothstep(.92, 1.0, PROGRESS));
            constexpr double PI_VALUE = 3.14159265358979323846;
            window.core = {
                .x = ENVELOPE * (std::cos(3 * PI_VALUE * SETTLE) +
                                    (2 * std::sin(3 * PI_VALUE * SETTLE) / (3 * PI_VALUE))),
                .y = ENVELOPE * .35 * std::sin(5 * PI_VALUE * SETTLE)};
            window.film = 1 - smoothstep(.56, 1.0, PROGRESS);
            // The inner film stays clear while the outer bubble bursts, so no image appears with
            // the pop; the window's imagery develops as the film flattens and lands.
            window.content = smoothstep(.50, .78, PROGRESS);
            window.thickness = 450;
            window.flow = elapsed_ * 2;
            window.visibility = smoothstep(.36, .42, PROGRESS);
            window.ripple = SHORT_SIDE * .012 * smoothstep(.56, .68, PROGRESS) *
                            (1 - smoothstep(.68, .76, PROGRESS));
            window.time = PROGRESS;
            applyMotion(resultFrame);
            return resultFrame;
        }
        resultFrame = initial_;
        resultFrame.size = size;
        resultFrame.seed = seed_;
        const double SCALE_X = size.x / size_.x;
        const double SCALE_Y = size.y / size_.y;
        const double SCALE = std::min(SCALE_X, SCALE_Y);
        const auto CLOSE = [&](BubbleSurface &surface, bool spherical) {
            const double X_SCALE = spherical ? SCALE : SCALE_X;
            const double Y_SCALE = spherical ? SCALE : SCALE_Y;
            surface.halfSize = {
                .x = surface.halfSize.x * X_SCALE, .y = surface.halfSize.y * Y_SCALE};
            surface.corner *= SCALE;
            surface.depth *= SCALE;
            const double DAMPING = SCALE * (1 - smoothstep(0, .5, PROGRESS));
            surface.core = {.x = surface.core.x * DAMPING, .y = surface.core.y * DAMPING};
            surface.ripple *= SCALE * (1 - smoothstep(0, .35, PROGRESS));
            // The film relaxes from the movement's deformation as it starts to tear.
            auto &motion = surface.motion;
            motion.drag = {.x = motion.drag.x * DAMPING, .y = motion.drag.y * DAMPING};
            for (std::size_t i = 0; i < motion.waveCos.size(); ++i) {
                motion.waveCos[i] *= DAMPING;
                motion.waveSin[i] *= DAMPING;
            }
            motion.sheen *= 1 - smoothstep(0, .5, PROGRESS);
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
                    auto &hole = surface.holes[i];
                    if (!spherical) {
                        hole.centre.x *= X_SCALE;
                        hole.centre.y *= Y_SCALE;
                    }
                }
                // Flat radii use the larger scale to preserve coverage after an anisotropic resize.
                if (!spherical) {
                    surface.speed *= std::max(SCALE_X, SCALE_Y);
                }
            }
            surface.clock += elapsed_ * surface.rate;
            surface.flow += elapsed_;
            updateRupture(surface);
        };
        CLOSE(resultFrame.window, false);
        CLOSE(resultFrame.shell, true);
        // Microbubbles on the window's film burst with it.
        for (std::size_t i = 0; i < resultFrame.microCount; ++i) {
            auto &microbubble = resultFrame.micro[i];
            microbubble.centre = {
                .x = microbubble.centre.x * SCALE_X, .y = microbubble.centre.y * SCALE_Y};
            microbubble.anchor = {
                .x = microbubble.anchor.x * SCALE_X, .y = microbubble.anchor.y * SCALE_Y};
            microbubble.collapse = std::max(microbubble.collapse, smoothstep(0, .25, PROGRESS));
        }
        return resultFrame;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || closed_) {
            return {};
        }
        const auto FRAME = frame(size);
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
            const Vec2 CARRY = surface.motion.carry;
            return std::array<double, 4>{
                CARRY.x - HALF.x, CARRY.y - HALF.y, CARRY.x + HALF.x, CARRY.y + HALF.y};
        };
        auto box = EXTENT(FRAME.window);
        const auto SHELL = EXTENT(FRAME.shell);
        for (std::size_t i = 0; i < 2; ++i) {
            box[i] = std::min(box[i], SHELL[i]);
            box[i + 2] = std::max(box[i + 2], SHELL[i + 2]);
        }
        for (std::size_t i = 0; i < FRAME.microCount; ++i) {
            const auto &microbubble = FRAME.micro[i];
            const double RADIUS = (microbubble.radius * 1.4) + 3;
            box = {std::min(box[0], microbubble.centre.x - RADIUS),
                std::min(box[1], microbubble.centre.y - RADIUS),
                std::max(box[2], microbubble.centre.x + RADIUS),
                std::max(box[3], microbubble.centre.y + RADIUS)};
        }
        return {.x = (size.x * .5) + box[0],
            .y = (size.y * .5) + box[1],
            .w = box[2] - box[0],
            .h = box[3] - box[1]};
    }

}  // namespace hyprimate
