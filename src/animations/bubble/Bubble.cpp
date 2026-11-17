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

        // A ray meets the rounded box, not its bounding rectangle. Contact points move
        // continuously from the initial sphere onto the window's sides and corners.
        Vec2 contactPoint(const BubbleSurface &s, double angle) {
            const Vec2 d{std::cos(angle), std::sin(angle)};
            const Vec2 a{std::abs(d.x), std::abs(d.y)};
            const double r = std::min({s.corner, s.halfSize.x, s.halfSize.y});
            double distance = std::min(s.halfSize.x / std::max(a.x, 1e-12),
                s.halfSize.y / std::max(a.y, 1e-12));
            const Vec2 c{s.halfSize.x - r, s.halfSize.y - r};
            if (a.x * distance > c.x && a.y * distance > c.y) {
                const double dot = a.x * c.x + a.y * c.y;
                distance = dot + std::sqrt(std::max(0.0,
                                     dot * dot - c.x * c.x - c.y * c.y + r * r));
            }
            return bubbleMotionPoint(s.halfSize, s.motion, {d.x * distance, d.y * distance});
        }

        void formation(BubbleFrame &f, double t, double seed, double flow) {
            constexpr double tau = 6.283185307179586;
            const double shortSide = std::min(f.size.x, f.size.y);
            // Geometry biases where air feeds the cluster, rather than stretching bubbles.
            const double aspect = std::log(f.size.x / f.size.y);
            const double bias = std::tanh(aspect);
            std::uint64_t random = static_cast<std::uint64_t>(seed * 0x1p53);
            std::array<double, 8> angles{}, radii{}, birth{}, life{}, phases{}, compression{};
            std::array<Vec2, 8> centres{}, anchors{};
            for (std::size_t i = 0; i < radii.size(); ++i) {
                phases[i] = tau * unitDraw(random);
                birth[i] = .008 + .11 * unitDraw(random);
                const double age = std::max(0.0, t - birth[i]);
                const double grow = smoothstep(0, .10 + .07 * unitDraw(random), age);
                // Three late films stay attached while the fluid core is relaxing.
                life[i] = i >= 5 ? .93 + .025 * (i - 5) : .43 + .24 * unitDraw(random);
                const double drain = 1 - smoothstep(life[i] - .20, life[i], t);
                radii[i] = shortSide * (.035 + .045 * unitDraw(random)) * grow * drain;
                const double initial = phases[i] + .35 * std::sin(phases[i] * 2.3);
                const double axis = std::atan2(std::sin(initial) * std::exp(-.55 * bias),
                    std::cos(initial) * std::exp(.55 * bias));
                // Low-frequency pressure disturbances and buoyant sliding, never frame noise.
                angles[i] = axis + .28 * std::sin(age * (5 + unitDraw(random) * 4) + phases[i]) *
                                             smoothstep(0, .12, age) -
                            .65 * age * std::cos(axis);
            }
            const auto place = [&](std::size_t i) {
                anchors[i] = contactPoint(f.window, angles[i]);
                const double c = std::cos(angles[i]), s = std::sin(angles[i]);
                const double merge = i < 3 ? smoothstep(life[i] - .16, life[i], t) : 0;
                const double detach = i == 3 || i == 4 ?
                                          shortSide * .10 * smoothstep(.35, life[i], t) :
                                          0;
                const double neck = radii[i] * (.72 - .9 * merge) + detach;
                centres[i] = {anchors[i].x + c * neck, anchors[i].y + s * neck};
            };
            // Quasi-static overdamped contacts. A bounded angular relaxation lets neighbours
            // slide along the growing parent instead of bouncing or passing through each other.
            // Sampling from time makes resize, replay and missed frames history-independent.
            for (int pass = 0; pass < 5; ++pass) {
                for (std::size_t i = 0; i < radii.size(); ++i) place(i);
                for (std::size_t i = 0; i < radii.size(); ++i) {
                    if (radii[i] <= 0) continue;
                    for (std::size_t j = i + 1; j < radii.size(); ++j) {
                        if (radii[j] <= 0) continue;
                        const Vec2 delta{centres[i].x - centres[j].x, centres[i].y - centres[j].y};
                        const double distance = std::hypot(delta.x, delta.y);
                        const double overlap = std::max(0.0, .92 * (radii[i] + radii[j]) - distance);
                        if (overlap <= 0) continue;
                        const double force = .22 * overlap / std::max(distance, shortSide * .001);
                        const double reachI = std::hypot(anchors[i].x - f.window.motion.carry.x,
                                                  anchors[i].y - f.window.motion.carry.y) +
                                              radii[i];
                        const double reachJ = std::hypot(anchors[j].x - f.window.motion.carry.x,
                                                  anchors[j].y - f.window.motion.carry.y) +
                                              radii[j];
                        angles[i] += force * (-std::sin(angles[i]) * delta.x +
                                                std::cos(angles[i]) * delta.y) /
                                     std::max(reachI, shortSide * .01);
                        angles[j] -= force * (-std::sin(angles[j]) * delta.x +
                                                std::cos(angles[j]) * delta.y) /
                                     std::max(reachJ, shortSide * .01);
                        compression[i] = std::max(compression[i], overlap / (radii[i] + radii[j]));
                        compression[j] = std::max(compression[j], overlap / (radii[i] + radii[j]));
                    }
                }
            }
            for (std::size_t i = 0; i < radii.size(); ++i) {
                if (radii[i] <= 0) continue;
                place(i);
                auto &s = f.formation[f.formationCount++];
                const double r = radii[i];
                const double wobble = .035 * std::sin((t - birth[i]) * 19 + phases[i]);
                s.halfSize = {r * (1 + wobble), r * (1 - wobble)};
                s.corner = std::min(s.halfSize.x, s.halfSize.y);
                s.depth = r;
                s.motion.carry = centres[i];
                // The contact stress excites the same capillary modes as the primary film.
                const double strain = r * (.025 + .09 * std::min(compression[i], .6));
                s.motion.waveCos[0] = -strain * std::cos(2 * angles[i]);
                s.motion.waveSin[0] = -strain * std::sin(2 * angles[i]);
                s.film = s.content = 1;
                s.visibility = smoothstep(0, .035, t - birth[i]) *
                               (1 - smoothstep(life[i] - .06, life[i], t));
                s.thickness = 380 + 220 * (.5 + .5 * std::sin(phases[i])) +
                              160 * (1 - smoothstep(birth[i], birth[i] + .18, t));
                s.flow = flow * (1 + .2 * std::sin(phases[i])) + phases[i];
            }
        }

    }  // namespace

    void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
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
        closed_ = false;
    }

    void Bubble::advance(double elapsed) {
        if (!active_ || !std::isfinite(elapsed) || elapsed <= 0) return;
        // A stalled compositor should not skip the animation in a single frame.
        elapsed_ = std::min(duration_, elapsed_ + std::min(elapsed, kStallSeconds));
        if (opening_) flow_ = elapsed_ * 2;
        if (elapsed_ >= duration_ - duration_ * 1e-12) {
            elapsed_ = duration_;
            active_ = false;
            closed_ = !opening_;
        }
    }

    void Bubble::advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
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
        if (!motion_.stirred()) return;
        auto &window = f.window;
        window.motion = motion_.deformation();
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
        const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
        f.seed = seed_;
        if (opening_) {
            const double shortSide = std::min(size.x, size.y);
            // One membrane receives the air and becomes the window; no shell/window swap.
            auto &window = f.window;
            const double inflation = smoothstep(.03, .52, t);
            const double windowRadius = shortSide * mix(.012, .42, inflation);
            const double flatten = smoothstep(.34, .68, t);
            const double splat = .045 * smoothstep(.55, .68, t) * (1 - smoothstep(.68, .76, t));
            window.halfSize = {mix(windowRadius, size.x * .5, flatten) + size.x * .5 * splat,
                mix(windowRadius, size.y * .5, flatten) + size.y * .5 * splat};
            window.corner = std::min(window.halfSize.x, window.halfSize.y) * (1 - flatten);
            window.depth = windowRadius * (1 - smoothstep(.48, .68, t));
            const double tension = windowRadius * .045 * (1 - smoothstep(.45, .68, t));
            window.motion.waveCos[0] = tension * std::sin(t * 17 + seed_ * 6.28);
            window.motion.waveSin[1] = tension * .45 * std::sin(t * 23 + seed_ * 11);
            window.motion.carry = {shortSide * .035 * std::sin(seed_ * 17) * (1 - flatten),
                shortSide * .08 * (1 - flatten)};
            const double settle = std::clamp((t - .64) / .36, 0.0, 1.0);
            const double envelope = shortSide * .20 * smoothstep(.48, .64, t) *
                                    std::exp(-2.0 * settle) * (1 - smoothstep(.92, 1.0, t));
            constexpr double pi = 3.14159265358979323846;
            window.core = {
                envelope * (std::cos(3 * pi * settle) + 2 * std::sin(3 * pi * settle) / (3 * pi)),
                envelope * .35 * std::sin(5 * pi * settle)};
            window.film = 1 - smoothstep(.56, 1.0, t);
            // Imagery develops through the same curved film; scene transmission recedes as
            // its dome flattens. The established core relaxation below remains uninterrupted.
            window.content = smoothstep(.12, .78, t);
            window.thickness = mix(720, 450, inflation);
            window.flow = elapsed_ * 2;
            window.visibility = smoothstep(0, .045, t);
            window.ripple =
                shortSide * .012 * smoothstep(.56, .68, t) * (1 - smoothstep(.68, .76, t));
            window.time = t;
            applyMotion(f);
            formation(f, t, seed_, elapsed_ * 2);
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
            s.motion.carry = {s.motion.carry.x * sx, s.motion.carry.y * sy};
            close(s, false);
            // A close cancels the feed; small films drain without depositing screen debris.
            s.visibility *= 1 - smoothstep(0, .30, t);
            s.holeCount = 0;
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
        for (std::size_t i = 0; i < f.formationCount; ++i) {
            if (f.formation[i].visibility <= 0) continue;
            const auto satellite = extent(f.formation[i]);
            for (std::size_t j = 0; j < 2; ++j) {
                box[j] = std::min(box[j], satellite[j]);
                box[j + 2] = std::max(box[j + 2], satellite[j + 2]);
            }
        }
        for (std::size_t i = 0; i < f.microCount; ++i) {
            const auto &m = f.micro[i];
            const double r = m.radius * 1.4 + 3;
            box = {std::min(box[0], m.centre.x - r),
                std::min(box[1], m.centre.y - r),
                std::max(box[2], m.centre.x + r),
                std::max(box[3], m.centre.y + r)};
        }
        return {size.x * .5 + box[0], size.y * .5 + box[1], box[2] - box[0], box[3] - box[1]};
    }

}  // namespace hyprimate
