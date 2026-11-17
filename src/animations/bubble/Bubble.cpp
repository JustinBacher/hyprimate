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
            s.ruptureVisibility = s.visibility;
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

    void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size,
        const BubbleFormationParams &formation) {
        if ((direction != Transition::Open && direction != Transition::Close) ||
            !std::isfinite(duration) || duration <= 0 || !usable(size)) {
            reset();
            return;
        }
        const bool fromOpening = direction == Transition::Close && active_ && opening_;
        // A close takes over the film as it is: mid-opening, or still deformed by movement.
        const bool inherit = fromOpening || (direction == Transition::Close && motion_.stirred());
        const auto start = inherit ? frame(size) : neutral(size);
        const double materialSeed = fromOpening ? start.seed : unitDraw(seed);
        const double x = (.16 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
        const double y = (.12 + .12 * unitDraw(seed)) * (unitDraw(seed) < .5 ? -1 : 1);
        initial_ = start;
        size_ = size;
        duration_ = std::clamp(duration, .01, 60.0);
        elapsed_ = 0;
        seed_ = materialSeed;
        rupture_ = {x, y};
        flow_ = 0;
        // A closing window no longer moves; its deformation now lives in the close's start frame.
        if (direction == Transition::Close) motion_.reset();
        if (direction == Transition::Close && initial_.window.visibility > 0 &&
            !initial_.window.closing)
            startRupture(initial_.window,
                rupture_,
                static_cast<std::uint64_t>(seed_ * 0x1p53),
                duration_,
                false);
        active_ = true;
        opening_ = direction == Transition::Open;
        if (opening_) formation_.begin(size, seed, formation);
        closed_ = false;
    }

    void Bubble::advance(double elapsed) {
        if (!active_ || !std::isfinite(elapsed) || elapsed <= 0) return;
        // A stalled compositor should not skip the animation in a single frame.
        elapsed_ = std::min(duration_, elapsed_ + std::min(elapsed, kStallSeconds));
        if (opening_) {
            formation_.advance(elapsed_ / duration_, size_);
            flow_ = elapsed_ * 2;
        }
        if (elapsed_ >= duration_ - duration_ * 1e-12) {
            elapsed_ = duration_;
            active_ = false;
            closed_ = !opening_;
        }
    }

    void Bubble::advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
        if (active_ && opening_ && usable(size)) size_ = size;
        const bool settling = opening();
        advance(elapsed);
        if (closed_ || (active_ && !opening_)) return;
        transparency_ = std::clamp(params.transparency, 0.0, 1.0);
        motion_.advance(elapsed, size, body, params);
        // Once landed, the film keeps flowing at the pace the movement stirs it.
        if (!settling)
            flow_ += std::min(elapsed, kStallSeconds) * (.6 + std::min(motion_.speed() / 800, 3.0));
    }

    void Bubble::reset() {
        initial_ = {};
        formation_ = {};
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
        return active_ && opening_ && formation_.landed();
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
        if (!motion_.stirred()) return;
        auto &window = f.window;
        const auto moving = motion_.deformation();
        auto &shape = window.motion;
        shape.drag = moving.drag;
        shape.sheen = std::max(shape.sheen, moving.sheen);
        shape.round = moving.round;
        shape.extent = moving.extent;
        shape.carry.x += moving.carry.x;
        shape.carry.y += moving.carry.y;
        for (std::size_t i = 0; i < shape.waveCos.size(); ++i) {
            shape.waveCos[i] += moving.waveCos[i];
            shape.waveSin[i] += moving.waveSin[i];
        }
        shape.buds = moving.buds;
        for (std::size_t i = 0; i < f.formationCount; ++i) {
            auto &satellite = f.formation[i];
            if (satellite.visibility > 0)
                satellite.centre = satellite.anchor =
                    bubbleMotionPoint(window.halfSize, moving, satellite.centre);
        }
        window.transparency = transparency_;
        const Vec2 core = motion_.core();
        window.core = {window.core.x + core.x, window.core.y + core.y};
        if (window.thickness <= 0) window.thickness = 450;
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
        if (!usable(size)) return {};
        BubbleFrame f = neutral(size);
        if (!active_) {
            if (closed_)
                f.window.visibility = 0;
            else
                applyMotion(f);
            return f;
        }
        f.seed = seed_;
        if (opening_) {
            formation_.writeFrame(f);
            f.window.transparency = transparency_;
            // Movement joins a physically landed film without replacing its residual modes.
            if (formation_.landed()) applyMotion(f);
            return f;
        }
        const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
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
            // The film relaxes from the movement's deformation as it starts to tear.
            auto &motion = surface.motion;
            motion.drag = {motion.drag.x * damping, motion.drag.y * damping};
            for (std::size_t i = 0; i < motion.waveCos.size(); ++i) {
                motion.waveCos[i] *= damping;
                motion.waveSin[i] *= damping;
            }
            motion.sheen *= 1 - smoothstep(0, .5, t);
            // A bubble that closes while contracted bursts that way: its shape is the film being
            // torn.
            motion.extent = {motion.extent.x * xScale, motion.extent.y * yScale};
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
        for (std::size_t i = 0; i < f.formationCount; ++i) {
            auto &s = f.formation[i];
            s.centre = {s.centre.x * sx, s.centre.y * sy};
            s.anchor = {s.anchor.x * sx, s.anchor.y * sy};
            s.radius *= scale;
            // A close cancels the air supply: companions rupture without depositing screen debris.
            s.collapse = std::max(s.collapse, smoothstep(0, .25, t));
            s.visibility *= 1 - smoothstep(.15, .30, t);
        }
        // Microbubbles on the window's film burst with it.
        for (std::size_t i = 0; i < f.microCount; ++i) {
            auto &m = f.micro[i];
            m.centre = {m.centre.x * sx, m.centre.y * sy};
            m.anchor = {m.anchor.x * sx, m.anchor.y * sy};
            m.collapse = std::max(m.collapse, smoothstep(0, .25, t));
        }
        return f;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || closed_) return {};
        const auto f = frame(size);
        // Each membrane's box around where it is drawn; a carried film is offset from the window.
        const auto extent = [](const BubbleSurface &surface) {
            double reach = std::hypot(surface.motion.drag.x, surface.motion.drag.y);
            for (std::size_t i = 0; i < surface.motion.waveCos.size(); ++i)
                reach += std::hypot(surface.motion.waveCos[i], surface.motion.waveSin[i]);
            double swell = 0;
            for (const auto &bud : surface.motion.buds) swell = std::max(swell, bud.height);
            reach += swell;
            // A contracted membrane may reach past its rectangle where its shape bulges; one
            // overshooting back swells slightly past it everywhere.
            const Vec2 round = bubbleRoundHalf(surface.halfSize, surface.motion);
            reach += std::max(0.0, -surface.motion.round) *
                     std::max(surface.halfSize.x, surface.halfSize.y);
            const Vec2 base{
                std::max(surface.halfSize.x, round.x), std::max(surface.halfSize.y, round.y)};
            const double margin = 8 + std::abs(surface.ripple) + reach +
                                  (surface.closing ? 1.2 * std::min(base.x, base.y) : 0);
            const Vec2 half{base.x + margin, base.y + margin};
            const Vec2 at = surface.motion.carry;
            return std::array<double, 4>{
                at.x - half.x, at.y - half.y, at.x + half.x, at.y + half.y};
        };
        auto box = extent(f.window);
        const auto shell = extent(f.shell);
        for (std::size_t i = 0; i < 2; ++i) {
            box[i] = std::min(box[i], shell[i]);
            box[i + 2] = std::max(box[i + 2], shell[i + 2]);
        }
        const auto small = [&](const BubbleMicro &m) {
            if (m.visibility <= 0) return;
            const double r = m.radius * (1.4 + std::abs(m.squash.x) + std::abs(m.squash.y)) + 3;
            box = {std::min(box[0], m.centre.x - r),
                std::min(box[1], m.centre.y - r),
                std::max(box[2], m.centre.x + r),
                std::max(box[3], m.centre.y + r)};
        };
        for (std::size_t i = 0; i < f.formationCount; ++i) small(f.formation[i]);
        for (std::size_t i = 0; i < f.microCount; ++i) small(f.micro[i]);
        return {size.x * .5 + box[0], size.y * .5 + box[1], box[2] - box[0], box[3] - box[1]};
    }

}  // namespace hyprimate
