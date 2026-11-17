#include <algorithm>
#include <cmath>

#include "animations/bubble/BubbleMotion.hpp"

namespace hyprimate {
    namespace {

        constexpr double kPi = 3.14159265358979323846, kTau = 2 * kPi;
        constexpr std::array<int, 4> kModes{2, 3, 4, 5};
        // The trailing fluid: one damped mode, underdamped enough to overshoot once when the window
        // stops (about 16%) and quiet again within roughly half a second.
        constexpr double kLagFrequency = kTau * 1.9, kLagDamping = .5;
        // The leading face meets the air head on: its middle flattens back by this share of the
        // trailing film's lag, so the leading side compresses while the trailing side stretches.
        constexpr double kLeadFlatten = .3;
        // Capillary waves on the outline: n = 2 rings at this frequency; higher modes follow the
        // dispersion of a 2D drop, w_n^2 ~ n^3 - n, and lose energy faster.
        constexpr double kWaveFrequency = kTau * 2.3;
        // Interior pressure, the mode the opening's fluid core settles through.
        constexpr double kPressureFrequency = kTau * 2.6, kPressureDamping = .32;
        // Pointer-driven positions arrive unevenly; two smoothing stages keep that jitter out of
        // the acceleration.
        constexpr double kVelocitySmoothing = .03;
        constexpr double kMaxSpeed = 6000;
        // Below this speed the film draws out no strands.
        constexpr double kTrailSpeed = 140;
        // Below this speed a moving window counts as paused: its shape holds briefly, then spreads.
        constexpr double kPauseSpeed = 30;
        // The film follows a layout-animated window critically damped: it eases out of its old
        // place and arrives within about half a second, whatever the layout's own curve.
        constexpr double kCarryFrequency = 9;
        // Beyond this the film is not left behind: a farther jump moves it mostly with the window.
        constexpr double kMaxCarry = 2400;
        // The pillow: how far its outline is drawn toward the inscribed ellipse at full
        // contraction, and that ellipse's size relative to the rectangle. Its sides stay nearly
        // straight, its corners round off, and the whole film draws in by a few percent.
        constexpr double kPillowRound = .5, kPillowExtent = .92;
        // How quickly the side the film trails from turns after the motion: a change of direction
        // swings the strands and the bubbles' combing round instead of flipping them.
        constexpr double kBehindTurn = .12;
        // Share of the window's velocity film and bubbles keep once they are left on the screen.
        constexpr double kFilmInherit = .3;

        Vec2 add(Vec2 a, Vec2 b) {
            return {a.x + b.x, a.y + b.y};
        }
        Vec2 sub(Vec2 a, Vec2 b) {
            return {a.x - b.x, a.y - b.y};
        }
        Vec2 mul(Vec2 a, double s) {
            return {a.x * s, a.y * s};
        }
        double dot(Vec2 a, Vec2 b) {
            return a.x * b.x + a.y * b.y;
        }
        double length(Vec2 a) {
            return std::hypot(a.x, a.y);
        }
        bool finite(Vec2 a) {
            return std::isfinite(a.x) && std::isfinite(a.y);
        }
        // The outline quadrupole (cos 2phi, sin 2phi) that elongates a bubble along unit n.
        Vec2 mode(Vec2 n) {
            return {n.x * n.x - n.y * n.y, 2 * n.x * n.y};
        }
        // Magnitude-preserving soft limit: linear for small values, never beyond limit.
        Vec2 soften(Vec2 v, double limit) {
            const double m = length(v);
            if (!(limit > 0) || !(m > 1e-12)) return {};
            return mul(v, limit * std::tanh(m / limit) / m);
        }
        double modeFrequency(std::size_t i) {
            const double n = kModes[i];
            return kWaveFrequency * std::sqrt((n * n * n - n) / 6.0);
        }
        double modeDecay(std::size_t i) {
            return 3.5 + 1.6 * double(i);
        }
        // 0..1 how sharply the window is turning: acceleration across its direction of travel.
        double turning(Vec2 velocity, double speed, Vec2 acceleration) {
            if (!(speed > 1e-6)) return 0;
            const Vec2 dir = mul(velocity, 1 / speed);
            return std::clamp(
                std::abs(dir.x * acceleration.y - dir.y * acceleration.x) / 12000, 0.0, 1.0);
        }
        // Outward direction of the rounded membrane (half size h, roundness `round`) at q: a
        // rounded rectangle's normal turning into an ellipse's. Not normalised; zero at the centre.
        Vec2 outward(Vec2 h, double round, Vec2 q) {
            const Vec2 u{q.x / h.x, q.y / h.y};
            const Vec2 square{u.x * u.x * u.x / h.x, u.y * u.y * u.y / h.y}, circle{u.x / h.x, u.y / h.y};
            const double s = length(square), c = length(circle);
            if (!(s > 1e-12) || !(c > 1e-12)) return {};
            return add(mul(square, (1 - round) / s), mul(circle, round / c));
        }
        // Advances an outline quadrupole oscillator toward target; stable for any step.
        void ring(Vec2 &squash, Vec2 &rate, Vec2 target, double frequency, double damping, double dt) {
            const int steps = std::max(1, int(std::ceil(frequency * dt / .2)));
            const double h = dt / steps;
            for (int i = 0; i < steps; ++i) {
                rate = add(rate,
                    mul(add(mul(sub(squash, target), -frequency * frequency), mul(rate, -2 * damping * frequency)),
                        h));
                squash = add(squash, mul(rate, h));
            }
        }

    }  // namespace

    bool BubbleDeformation::still() const noexcept {
        return drag.x == 0 && drag.y == 0 && sheen == 0 && round == 0 && carry.x == 0 &&
               carry.y == 0 && std::ranges::all_of(waveCos, [](double v) { return v == 0; }) &&
               std::ranges::all_of(waveSin, [](double v) { return v == 0; }) &&
               std::ranges::all_of(buds, [](const BubbleBud &b) { return b.height == 0; });
    }

    Vec2 bubbleRoundHalf(Vec2 halfSize, const BubbleDeformation &m) {
        const Vec2 h{std::max(halfSize.x, 1.0), std::max(halfSize.y, 1.0)};
        const Vec2 e{std::max(m.extent.x, 1.0), std::max(m.extent.y, 1.0)};
        return {h.x + (e.x - h.x) * m.round, h.y + (e.y - h.y) * m.round};
    }

    Vec2 bubbleRoundPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        if (m.round == 0) return q;
        const Vec2 h{std::max(halfSize.x, 1.0), std::max(halfSize.y, 1.0)};
        const double x = std::clamp(q.x / h.x, -1.5, 1.5), y = std::clamp(q.y / h.y, -1.5, 1.5);
        const Vec2 disc{m.extent.x * x * std::sqrt(std::max(0.0, 1 - y * y * .5)),
            m.extent.y * y * std::sqrt(std::max(0.0, 1 - x * x * .5))};
        return add(q, mul(sub(disc, q), m.round));
    }

    Vec2 bubbleMotionPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        const Vec2 p = bubbleRoundPoint(halfSize, m, q);
        return add(add(p, bubbleMotionOffset(bubbleRoundHalf(halfSize, m), m, p)), m.carry);
    }

    Vec2 bubbleMotionOffset(Vec2 halfSize, const BubbleDeformation &m, Vec2 p) {
        const Vec2 h{std::max(halfSize.x, 1.0), std::max(halfSize.y, 1.0)};
        Vec2 offset{};
        const double drag = length(m.drag);
        if (drag > 1e-6) {
            const Vec2 dir = mul(m.drag, 1 / drag);
            const double reach = std::abs(dir.x) * h.x + std::abs(dir.y) * h.y;
            const double breadth = std::abs(dir.y) * h.x + std::abs(dir.x) * h.y;
            const double along = dot(p, dir);
            const double w = smoothstep(-1.0, 1.0, std::clamp(along / reach, -1.0, 1.0));
            const double lead = smoothstep(0.0, 1.0, std::clamp(-along / reach, 0.0, 1.0));
            const Vec2 across = sub(p, mul(dir, along));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame; the middle of the leading face is pressed back.
            const double side = std::min(1.0, dot(across, across) / (breadth * breadth));
            offset = sub(mul(m.drag, w * (1 - .55 * side) + kLeadFlatten * lead * (1 - side)),
                mul(across, .35 * drag / reach * w));
        }
        // Ripples run normal to the outline: a rounded rectangle's, turning into a circle's.
        const double round = std::clamp(m.round, 0.0, 1.0);
        const Vec2 u{p.x / h.x, p.y / h.y};
        const Vec2 u2{u.x * u.x, u.y * u.y};
        const double rho4 = dot(u2, u2);
        if (rho4 > 1e-12) {
            const double rho = std::min(
                std::sqrt(std::sqrt(rho4)) * (1 - round) + std::sqrt(dot(u, u)) * round, 1.25);
            const double phi = std::atan2(u.y, u.x);
            double radial = 0;
            for (std::size_t i = 0; i < kModes.size(); ++i)
                radial += m.waveCos[i] * std::cos(kModes[i] * phi) +
                          m.waveSin[i] * std::sin(kModes[i] * phi);
            const Vec2 g = outward(h, round, p);
            const double gl = length(g);
            if (gl > 0) offset = add(offset, mul(g, radial * rho * rho * rho / gl));
        }
        // Buds: the film swells outward around each foot, along the outline's normal there.
        for (const auto &bud : m.buds) {
            if (!(bud.height > 0) || !(bud.width > 0)) continue;
            const Vec2 d = sub(p, bud.foot);
            const double f = std::exp(-dot(d, d) / (bud.width * bud.width));
            if (f < 1e-4) continue;
            const Vec2 n = outward(h, round, bud.foot);
            const double nl = length(n);
            if (nl > 0) offset = add(offset, mul(n, bud.height * f / nl));
        }
        return offset;
    }

    void BubbleMotion::drive(Vec2 displacement, bool carried) noexcept {
        if (!finite(displacement)) return;
        pending_ = add(pending_, displacement);
        if (!carried) return;
        carry_ = sub(carry_, displacement);
        const double c = length(carry_);
        if (c > kMaxCarry) carry_ = mul(carry_, kMaxCarry / c);
    }

    void BubbleMotion::seed(std::uint64_t seed) noexcept {
        random_ = seed;
        for (double &phase : phase_) phase = kTau * draw01();
        threshold_ = -std::log(1 - draw01());
    }

    void BubbleMotion::reset() noexcept {
        const auto random = random_;
        const auto phase = phase_;
        const auto threshold = threshold_;
        const auto strand = nextStrand_;
        *this = BubbleMotion{};
        random_ = random;
        phase_ = phase;
        threshold_ = threshold;
        nextStrand_ = strand;
    }

    double BubbleMotion::draw01() noexcept {
        return unitDraw(random_);
    }

    double BubbleMotion::gauss() noexcept {
        // Irwin–Hall: close enough to normal for size spreads, and bounded.
        return (draw01() + draw01() + draw01() + draw01() - 2) * std::sqrt(3.0);
    }

    double BubbleMotion::reference() const noexcept {
        return std::max(1.0, std::min(size_.x, size_.y));
    }

    double BubbleMotion::dragLimit() const noexcept {
        return params_.strength * std::clamp(.09 * reference(), 6.0, 64.0);
    }

    bool BubbleMotion::stirred() const noexcept {
        // A translation not yet advanced already counts: the window must keep stepping to feel it.
        return stirred_ || pending_.x != 0 || pending_.y != 0;
    }

    double BubbleMotion::speed() const noexcept {
        return speed_;
    }

    BubbleDeformation BubbleMotion::deformation() const noexcept {
        return shape_;
    }

    Vec2 BubbleMotion::core() const noexcept {
        if (!stirred_) return {};
        const double amount = .035 * reference() * params_.strength;
        return {amount * std::tanh(pressure_.x), .5 * amount * std::tanh(pressure_.y)};
    }

    void BubbleMotion::reshape() noexcept {
        shape_ = {};
        if (!stirred_) return;
        shape_.drag = soften(lag_, dragLimit());
        const double cap = std::clamp(.035 * reference(), 1.0, 24.0);
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            const Vec2 w = soften(wave_[i], cap);
            shape_.waveCos[i] = w.x;
            shape_.waveSin[i] = w.y;
        }
        shape_.sheen = sheen_;
        shape_.carry = carry_;
        // Each shape is shown short of where its square-to-ellipse map would fold the corners: the
        // pillow keeps the rectangle's straight run along each side and draws in slightly, the
        // ellipse and disc nearly lose it. A spring overshooting back past the rectangle is
        // softened to a small outward swell.
        const double r = round_ < 0 ? -.1 * std::tanh(-round_ / .1) : std::min(round_, 1.06);
        const Vec2 half{size_.x * .5, size_.y * .5};
        switch (params_.shape) {
            case BubbleShape::Pillow:
                shape_.round = kPillowRound * r;
                shape_.extent = mul(half, kPillowExtent);
                break;
            case BubbleShape::Ellipse:
                shape_.round = .92 * r;
                shape_.extent = mul(half, 1.1);
                break;
            case BubbleShape::Circle: {
                shape_.round = .92 * r;
                const double radius = .37 * std::sqrt(size_.x * size_.y);
                shape_.extent = {radius, radius};
                break;
            }
        }
        // The bubbles pressing hardest on the film swell it; the rest are too small to show.
        std::array<BubbleBud, kBubbleMicroCapacity> buds{};
        for (std::size_t i = 0; i < attachedCount_; ++i) buds[i] = bud(attached_[i]);
        std::partial_sort(buds.begin(),
            buds.begin() + kBubbleBuds,
            buds.end(),
            [](const BubbleBud &a, const BubbleBud &b) { return a.height > b.height; });
        std::copy_n(buds.begin(), kBubbleBuds, shape_.buds.begin());
    }

    void BubbleMotion::advance(
        double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
        if (!(std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0)) {
            reset();
            return;
        }
        size_ = size;
        body_ = isUsable(body) ? body : Box{-size.x * .5, -size.y * .5, size.x, size.y};
        params_ = {std::max(0.0, params.strength),
            std::clamp(params.microbubbles, 0.0, 1.0),
            std::clamp(params.trail, 0.0, 1.0),
            std::clamp(params.roundness, 0.0, 1.0),
            params.shape,
            std::max(params.shapeSpeed, 100.0)};
        const double dt = std::min(elapsed, kStallSeconds);
        // The film behind a layout-animated window closes in on it; what the window is seen to do
        // is its own translation plus how far the film moved toward it.
        if (carry_.x != 0 || carry_.y != 0 || carryRate_.x != 0 || carryRate_.y != 0) {
            const double w = kCarryFrequency, e = std::exp(-w * dt);
            const Vec2 c = add(carryRate_, mul(carry_, w));
            carry_ = mul(add(carry_, mul(c, dt)), e);
            carryRate_ = mul(sub(carryRate_, mul(c, w * dt)), e);
        }
        Vec2 observed = mul(add(pending_, sub(carry_, carryMark_)), 1 / dt);
        carryMark_ = carry_;
        pending_ = {};
        if (!finite(observed)) observed = {};
        if (length(observed) > kMaxSpeed) observed = mul(observed, kMaxSpeed / length(observed));
        if (!stirred_ && length(observed) == 0 && carry_.x == 0 && carry_.y == 0) return;
        stirred_ = true;

        const double blend = 1 - std::exp(-dt / kVelocitySmoothing);
        const Vec2 previous = velocity_;
        const double previousSpeed = speed_;
        response_ = add(response_, mul(sub(observed, response_), blend));
        velocity_ = add(velocity_, mul(sub(response_, velocity_), blend));
        speed_ = length(velocity_);
        const Vec2 acceleration = mul(sub(velocity_, previous), 1 / dt);
        acceleration_ = acceleration;
        const double speedRate = (speed_ - previousSpeed) / dt;
        // The side the film trails from turns after the motion with a little delay, swinging
        // round through the sides rather than shrinking through nothing on a reversal; at rest it
        // keeps the last one.
        if (speed_ > 20) {
            const Vec2 back = mul(velocity_, -1 / speed_);
            if (length(behind_) < .5)
                behind_ = back;
            else {
                const double turn =
                    std::atan2(behind_.x * back.y - behind_.y * back.x, dot(behind_, back)) *
                    (1 - std::exp(-dt / kBehindTurn));
                const double c = std::cos(turn), s = std::sin(turn);
                behind_ = {behind_.x * c - behind_.y * s, behind_.x * s + behind_.y * c};
            }
        }

        // The faster the window travels the further it contracts. Slowing down lets it relax as
        // it goes; a pause only lets it spread out once it has lasted, so brief stops in a drag do
        // not flicker between shapes.
        const double ask =
            params_.strength > 0
                ? params_.roundness * smoothstep(kPauseSpeed, params_.shapeSpeed, speed_)
                : 0.0;
        if (ask >= hold_) {
            hold_ = ask;
            idle_ = 0;
        } else if (speed_ > kPauseSpeed) {
            hold_ += (ask - hold_) * (1 - std::exp(-dt / .3));
            idle_ = 0;
        } else {
            idle_ += dt;
            if (idle_ > .16) hold_ = std::max(ask, hold_ - dt / .22);
        }

        substep_ += dt;
        while (substep_ >= kSubstepSeconds) {
            step(kSubstepSeconds, acceleration, speedRate);
            substep_ -= kSubstepSeconds;
        }
        clock_ += dt;

        // Stress brings colour into the film quickly; it drains away more slowly once released.
        const double limit = dragLimit();
        double waves = 0;
        for (const auto &w : wave_) waves += length(w);
        const double stress = (limit > 0 ? length(lag_) / limit : 0) +
                              .35 * waves / std::max(1.0, .02 * reference()) +
                              .3 * std::abs(pressure_.x);
        const double target = params_.strength > 0 ? .6 * std::clamp(stress, 0.0, 1.0) : 0.0;
        sheen_ += (target - sheen_) * (1 - std::exp(-dt / (target > sheen_ ? .06 : .3)));

        reshape();
        nucleate(dt, acceleration);
        migrate(dt, acceleration);
        emitTrail(dt, acceleration);
        reshape();

        double waveRest = 0, waveRate = 0;
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            waveRest = std::max(waveRest, length(wave_[i]));
            waveRate = std::max(waveRate, length(waveRate_[i]));
        }
        const bool strands =
            std::ranges::any_of(strands_, [](const Strand &s) { return s.live; });
        const bool quiet = speed_ < 2 && length(response_) < 2 && length(lag_) < .05 &&
                           length(lagRate_) < .5 && waveRest < .02 && waveRate < .5 &&
                           length(pressure_) < 1e-3 && length(pressureRate_) < 1e-2 &&
                           sheen_ < .003 && attachedCount_ == 0 && !strands && hold_ == 0 &&
                           std::abs(round_) < 1e-3 && std::abs(roundRate_) < 1e-2 &&
                           length(carry_) < .05 && length(carryRate_) < .5;
        if (quiet) {
            // Every remaining disturbance is far below a pixel: come to rest exactly.
            const auto trail = trail_;
            const auto trailCount = trailCount_;
            const auto releases = releases_;
            const auto releaseCount = releaseCount_;
            reset();
            trail_ = trail;
            trailCount_ = trailCount;
            releases_ = releases;
            releaseCount_ = releaseCount;
        }
    }

    void BubbleMotion::step(double h, Vec2 acceleration, double speedRate) {
        const double strength = params_.strength, L = reference();
        // The trailing fluid: dragged along by the film at a steady speed, thrown back by
        // acceleration and carried forward by its own momentum when the window stops.
        Vec2 target{};
        if (speed_ > 1e-6)
            target = mul(velocity_,
                -strength * std::clamp(.05 * L, 4.0, 32.0) * std::tanh(speed_ / 1600) / speed_);
        const double w2 = kLagFrequency * kLagFrequency;
        const Vec2 lagForce =
            sub(sub(mul(sub(lag_, target), -w2), mul(lagRate_, 2 * kLagDamping * kLagFrequency)),
                mul(acceleration, .45 * strength));
        lagRate_ = add(lagRate_, mul(lagForce, h));
        lag_ = add(lag_, mul(lagRate_, h));

        // The sloshing interior pushes on the outline in its own direction; air buffets a moving
        // film off resonance, so sustained travel keeps a faint, irregular ripple.
        const double slosh = length(lagRate_);
        const double theta = std::atan2(lagRate_.y, lagRate_.x);
        const double scale = strength * L / 600;
        const double buffet = scale * std::clamp(speed_ / 1600, 0.0, 1.5);
        constexpr std::array<double, 4> gain{8.0, 21.0, 30.0, 40.0};
        constexpr std::array<double, 4> gust{150.0, 340.0, 460.0, 520.0};
        const double time = clock_ + substep_;
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            const double n = kModes[i], w = modeFrequency(i), decay = modeDecay(i);
            const double drift = .43 * w * time + phase_[i];
            const Vec2 force{
                gain[i] * scale * slosh * std::cos(n * theta) + buffet * gust[i] * std::sin(drift),
                gain[i] * scale * slosh * std::sin(n * theta) +
                    buffet * gust[i] * std::cos(1.13 * drift + phase_[(i + 1) % 4])};
            const Vec2 accel = sub(sub(force, mul(wave_[i], w * w)), mul(waveRate_[i], 2 * decay));
            waveRate_[i] = add(waveRate_[i], mul(accel, h));
            wave_[i] = add(wave_[i], mul(waveRate_[i], h));
        }

        // The fluid compresses while the window gathers speed and rebounds when it sheds it.
        const double drive = std::clamp(speedRate / 9000, -1.5, 1.5);
        const double wp = kPressureFrequency, wr = 1.7 * kPressureFrequency;
        pressureRate_.x +=
            (-wp * wp * (pressure_.x - drive) - 2 * kPressureDamping * wp * pressureRate_.x) * h;
        pressureRate_.y +=
            (-wr * wr * (pressure_.y - .5 * drive) - 2 * .4 * wr * pressureRate_.y) * h;
        pressure_ = add(pressure_, mul(pressureRate_, h));

        // Surface tension draws the moving film in after a brief hesitation and nearly without
        // overshoot; spreading back into the frame is softer and swells past it once, slightly,
        // before the rectangle settles.
        const bool contracting = hold_ > round_;
        const double wc = kTau * (contracting ? 1.6 : 1.3), zc = contracting ? .75 : .6;
        roundRate_ += (-wc * wc * (round_ - hold_) - 2 * zc * wc * roundRate_) * h;
        round_ += roundRate_ * h;
    }

    BubbleMotion::Outline BubbleMotion::outline(double angle) const noexcept {
        const Vec2 centre{body_.x + body_.w * .5, body_.y + body_.h * .5};
        const Vec2 half{body_.w * .5, body_.h * .5};
        const Vec2 membrane{size_.x * .5, size_.y * .5};
        const auto at = [&](double a) {
            const double c = std::cos(a), s = std::sin(a);
            // A steep superellipse: the body's straight sides with softly rounded corners.
            const double k =
                std::pow(std::pow(std::abs(c), 12.0) + std::pow(std::abs(s), 12.0), -1.0 / 12.0);
            const Vec2 p = add(centre, Vec2{half.x * c * k, half.y * s * k});
            return bubbleMotionPoint(membrane, shape_, p);
        };
        constexpr double epsilon = .01;
        Outline o;
        o.point = at(angle);
        const Vec2 before = at(angle - epsilon), after = at(angle + epsilon);
        const Vec2 span = sub(after, before);
        const double l = length(span);
        o.stretch = l / (2 * epsilon);
        o.tangent = l > 0 ? mul(span, 1 / l) : Vec2{-std::sin(angle), std::cos(angle)};
        o.normal = {o.tangent.y, -o.tangent.x};
        if (dot(o.normal, sub(o.point, add(centre, shape_.carry))) < 0)
            o.normal = mul(o.normal, -1);
        // Turning of the tangent per px of outline.
        const Vec2 bend = sub(add(after, before), mul(o.point, 2));
        o.curvature = o.stretch > 1e-6 ? length(bend) / (o.stretch * o.stretch * epsilon * epsilon) : 0;
        return o;
    }

    double BubbleMotion::exposure(double angle) const noexcept {
        if (length(behind_) < .5) return 0;
        return std::max(0.0, dot(outline(angle).normal, behind_));
    }

    double BubbleMotion::trailingAngle() const noexcept {
        const Vec2 d = length(behind_) > .5 ? behind_ : Vec2{1, 0};
        return std::atan2(d.y / std::max(body_.h, 1.0), d.x / std::max(body_.w, 1.0));
    }

    BubbleBud BubbleMotion::bud(const Attached &b) const noexcept {
        const Vec2 centre{body_.x + body_.w * .5, body_.y + body_.h * .5};
        const Vec2 half{body_.w * .5, body_.h * .5};
        const double c = std::cos(b.angle), s = std::sin(b.angle);
        const double k =
            std::pow(std::pow(std::abs(c), 12.0) + std::pow(std::abs(s), 12.0), -1.0 / 12.0);
        BubbleBud out;
        out.foot = bubbleRoundPoint(
            {size_.x * .5, size_.y * .5}, shape_, add(centre, Vec2{half.x * c * k, half.y * s * k}));
        out.width = 1.25 * b.radius;
        // The film first swells into a dome; as the cap rounds into a bubble the dome subsides to
        // the slight lift a hanging bubble keeps on its film, and a pinching neck pulls it out.
        const double formed = .22 * b.radius;
        double height = 0;
        switch (b.stage) {
            case Stage::Bud:
                height = b.radius * (.5 * smoothstep(0, .45, b.growth) - .28 * smoothstep(.45, 1, b.growth));
                break;
            case Stage::Ride:
                height = formed;
                break;
            case Stage::Pinch:
                height = formed + .3 * b.radius * b.pinch;
                break;
            case Stage::Merge:
                height = b.radius * b.growth * (.22 + .5 * (1 - b.growth));
                break;
            case Stage::Pop:
                height = formed;
                break;
        }
        out.height = std::clamp(height * (1 - b.collapse), 0.0, .5 * out.width);
        return out;
    }

    BubbleMicro BubbleMotion::draw(const Attached &b) const noexcept {
        const auto o = outline(b.angle);
        const double e = smoothstep(0, 1, b.growth);
        const double r = b.radius * (.35 + .65 * e);
        // A forming bubble begins as a cap of the swelling film and rides out on its neck; a
        // pinching one is drawn out further on a neck that narrows.
        BubbleMicro m;
        m.centre = add(o.point, mul(o.normal, r * (-.55 + 1.4 * e + .9 * b.pinch)));
        m.radius = r;
        m.growth = b.growth;
        m.anchor = o.point;
        m.attach = (1 - b.collapse) * (1 - .8 * b.pinch);
        m.collapse = b.collapse;
        m.thickness = b.thickness;
        m.seed = b.seed;
        // Before the cap has risen the swelling is the film's own; the bubble appears out of it.
        m.visibility = b.stage == Stage::Bud     ? smoothstep(.2, .6, b.growth)
                       : b.stage == Stage::Merge ? smoothstep(0, .5, b.growth)
                                                 : 1.0;
        m.squash = add(b.squash, mul(mode(o.normal), .12 * b.pinch));
        return m;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t count = std::min(attachedCount_, out.size());
        for (std::size_t i = 0; i < count; ++i) out[i] = draw(attached_[i]);
        return count;
    }

    void BubbleMotion::nucleate(double dt, Vec2 acceleration) {
        if (params_.microbubbles <= 0 || params_.strength <= 0) return;
        const double limit = dragLimit();
        const double stretch = limit > 0 ? std::clamp(length(lag_) / limit, 0.0, 1.0) : 0;
        const double travel = std::clamp((speed_ - 120) / 1100, 0.0, 1.5);
        // A jolt thins the film too, even before the window has gathered speed.
        const double jolt = speed_ > 60 ? std::clamp(length(acceleration) / 14000, 0.0, 1.0) : 0;
        if (travel <= 0 && jolt <= 0) return;
        const double turn = turning(velocity_, speed_, acceleration);
        // Stretched, fast-moving, turning film is where it thins and buds.
        spawn_ += params_.microbubbles * 40 * (travel * (.35 + stretch + turn) + .8 * jolt) * dt;
        while (spawn_ >= threshold_) {
            spawn_ -= threshold_;
            threshold_ = -std::log(1 - draw01());
            if (attachedCount_ >= attached_.size()) continue;
            Attached b;
            b.radius = std::clamp(
                13 * std::exp(.45 * gauss()) * std::clamp(reference() / 600, .85, 1.5), 4.0, 34.0);
            // Most buds form where the film faces back into the air or bends round a corner;
            // a few anywhere.
            double best = 0, total = 0;
            for (int i = 0; i < 6; ++i) {
                const double a = kTau * draw01();
                const double x = exposure(a);
                const double weight =
                    .12 + 1.6 * x * x + .5 * std::pow(std::abs(std::sin(2 * a)), 8.0);
                total += weight;
                if (draw01() * total < weight) best = a;
            }
            b.angle = best;
            // A bud needs room: the film there is already taken up by a neighbour's foot.
            const auto here = outline(b.angle);
            const bool crowded = std::any_of(attached_.begin(),
                attached_.begin() + std::ptrdiff_t(attachedCount_),
                [&](const Attached &o) {
                    return std::abs(std::remainder(o.angle - b.angle, kTau)) * here.stretch <
                           .8 * (o.radius + b.radius);
                });
            if (crowded) continue;
            b.grow = (.16 + .3 * draw01()) * std::pow(b.radius / 8, .3);
            b.patience = .35 + 1.2 * draw01();
            b.hold = .25 + .85 * draw01();
            b.pinchRate = 1 / (.1 + .14 * draw01());
            b.thickness = 300 + 550 * draw01();
            b.seed = draw01();
            b.pops = draw01() < .35;
            attached_[attachedCount_++] = b;
        }
    }

    void BubbleMotion::release(const Attached &b) {
        if (releaseCount_ >= releases_.size()) return;
        const auto o = outline(b.angle);
        BubbleRelease r;
        r.micro = draw(b);
        // It keeps part of the window's motion, the speed it slid at and a push off the film.
        r.velocity = add(add(mul(velocity_, .5 + .3 * draw01()), mul(o.tangent, b.slip * o.stretch)),
            mul(o.normal, 25 + 35 * draw01()));
        r.squashRate = add(mul(b.squashRate, 1.0), mul(mode(o.normal), -2.5));
        r.life = .5 + .9 * draw01();
        releases_[releaseCount_++] = r;
        // The film the neck held snaps back: a capillary kick where it let go.
        const auto foot = bud(b).foot;
        const Vec2 h = bubbleRoundHalf({size_.x * .5, size_.y * .5}, shape_);
        const double theta = std::atan2(foot.y / std::max(h.y, 1.0), foot.x / std::max(h.x, 1.0));
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            const double amp = -5 * b.radius * params_.strength / double(i + 1);
            waveRate_[i] = add(waveRate_[i],
                Vec2{amp * std::cos(kModes[i] * theta), amp * std::sin(kModes[i] * theta)});
        }
        // A thread of film follows it off the window for a moment.
        startStrand(b.angle, std::max(1.0, .22 * b.radius), 380 + 200 * draw01(), 20 + 40 * draw01());
    }

    void BubbleMotion::migrate(double dt, Vec2 acceleration) {
        const double turn = turning(velocity_, speed_, acceleration);
        const double slow = speed_ < 80 ? 1 : 0;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < attachedCount_; ++i) {
            auto b = attached_[i];
            b.age += dt;
            const auto o = outline(b.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back toward the trailing side. Where the outline
            // bends more sharply than the bubble is wide (the corners) it settles into the bend.
            const Vec2 force = sub(mul(acceleration, -.05), mul(velocity_, 1.1));
            const double trap = 3.2 + 40 * std::min(1.0, o.curvature * b.radius);
            b.slip += dot(force, o.tangent) / std::max(o.stretch, 1.0) * dt;
            b.slip = std::clamp(b.slip * std::exp(-trap * dt), -3.0, 3.0);
            b.angle += b.slip * dt;

            // It sits flattened on the film and stretches out along the way it slides.
            const double sliding = std::clamp(b.slip * o.stretch / 900, -.1, .1);
            const Vec2 target = add(mul(mode(o.tangent), std::abs(sliding)), mul(mode(o.normal), -.05));
            const double frequency = kTau * std::clamp(12 * std::sqrt(8 / std::max(b.radius, 1.0)), 6.0, 22.0);
            ring(b.squash, b.squashRate, target, frequency, .3, dt);

            bool gone = false;
            switch (b.stage) {
                case Stage::Bud:
                    b.growth = std::min(1.0, b.age / b.grow);
                    if (b.growth >= 1) b.stage = Stage::Ride;
                    break;
                case Stage::Ride: {
                    // A riding bubble resolves once the motion has calmed long enough: it sinks
                    // back into the film or pops.
                    b.patience -= dt * (slow ? 2.5 : .5);
                    if (b.patience <= 0) {
                        b.stage = b.pops ? Stage::Pop : Stage::Merge;
                        break;
                    }
                    // Its hold gives way once the air it catches, the jolts and the turns outweigh
                    // it: larger bubbles facing back into fast motion let go first.
                    const double instability =
                        speed_ / 700 * std::sqrt(b.radius / 8) * (.25 + exposure(b.angle)) +
                        length(acceleration) / 9000 + .5 * turn;
                    if (instability > b.hold && draw01() < 9 * (instability - b.hold) * dt)
                        b.stage = Stage::Pinch;
                    break;
                }
                case Stage::Pinch:
                    b.pinch = std::min(1.0, b.pinch + b.pinchRate * dt);
                    if (b.pinch >= 1) {
                        release(b);
                        gone = true;
                    }
                    break;
                case Stage::Merge:
                    b.growth -= dt / .28;
                    gone = b.growth <= 0;
                    break;
                case Stage::Pop:
                    b.collapse += dt / .09;
                    gone = b.collapse >= 1;
                    break;
            }
            if (!gone) attached_[kept++] = b;
        }
        attachedCount_ = kept;

        // Neighbours on the film push apart along it; touching formed bubbles may coalesce,
        // the smaller one emptying into the larger.
        for (std::size_t i = 0; i < attachedCount_; ++i) {
            auto &a = attached_[i];
            if (a.stage != Stage::Ride && a.stage != Stage::Bud) continue;
            for (std::size_t j = i + 1; j < attachedCount_; ++j) {
                auto &b = attached_[j];
                if (b.stage != Stage::Ride && b.stage != Stage::Bud) continue;
                const double gap = std::remainder(b.angle - a.angle, kTau);
                const double stretch = std::max(outline(a.angle).stretch, 1.0);
                const double ra = a.radius * (.35 + .65 * a.growth), rb = b.radius * (.35 + .65 * b.growth);
                const double overlap = .95 * (ra + rb) - std::abs(gap) * stretch;
                if (overlap <= 0) continue;
                if (a.stage == Stage::Ride && b.stage == Stage::Ride &&
                    draw01() < 4 * dt * overlap / (ra + rb)) {
                    auto &big = a.radius >= b.radius ? a : b;
                    auto &small = &big == &a ? b : a;
                    big.radius = std::max(big.radius,
                        std::min(40.0, std::cbrt(big.radius * big.radius * big.radius +
                                           small.radius * small.radius * small.radius)));
                    big.squashRate = add(big.squashRate, mul(mode(outline(big.angle).tangent), 3));
                    small.stage = Stage::Merge;
                    continue;
                }
                const double push = 6 * overlap / stretch * dt;
                a.slip -= std::copysign(push, gap);
                b.slip += std::copysign(push, gap);
            }
        }
    }

    bool BubbleMotion::startStrand(double angle, double halfWidth, double thickness, double reach) {
        for (auto &s : strands_) {
            if (s.live) continue;
            s = {};
            s.id = nextStrand_++;
            s.angle = angle;
            s.halfWidth = halfWidth;
            s.thickness = thickness;
            s.reach = reach;
            s.live = true;
            s.thread = halfWidth < 6;
            return true;
        }
        return false;
    }

    void BubbleMotion::emitTrail(double dt, Vec2 acceleration) {
        const bool drawing = params_.trail > 0 && params_.strength > 0;
        const double travel = std::clamp((speed_ - kTrailSpeed) / 1100, 0.0, 1.5);
        const double limit = dragLimit();
        const double stretch = limit > 0 ? std::clamp(length(lag_) / limit, 0.0, 1.0) : 0;
        const double turn = turning(velocity_, speed_, acceleration);
        const double density = std::clamp(
            2 * params_.trail * std::pow(std::clamp((speed_ - kTrailSpeed) / 2200, 0.0, 1.0), .8) *
                (.7 + .5 * stretch + .6 * turn),
            0.0,
            1.0);
        if (drawing && travel > 0) {
            // A lamella is drawn out of the trailing face as soon as the window is moving fast
            // enough to stretch one: as wide as the face allows, more of it the faster it goes.
            const bool sheet =
                std::ranges::any_of(strands_, [](const Strand &s) { return s.live && !s.thread; });
            if (!sheet && density > .04 && draw01() < 12 * dt) {
                const Vec2 d = length(behind_) > .5 ? behind_ : Vec2{1, 0};
                const Vec2 extent = bubbleRoundHalf({size_.x * .5, size_.y * .5}, shape_);
                const double across = std::abs(extent.x * d.y) + std::abs(extent.y * d.x);
                startStrand(trailingAngle(),
                    std::max(6.0, across * (.03 + .07 * density) * (.7 + .6 * draw01())),
                    480 + 380 * density,
                    (260 + 420 * draw01()) * (.6 + .8 * density));
            }
            // Threads pull out of the trailing corners, where the film's borders meet.
            threadHazard_ += params_.trail * 2.4 * travel * (1 + turn) * dt;
            if (draw01() < threadHazard_ * dt * 8) {
                threadHazard_ = 0;
                const Vec2 d = length(behind_) > .5 ? behind_ : Vec2{1, 0};
                const Vec2 u{d.x / std::max(body_.w, 1.0), d.y / std::max(body_.h, 1.0)};
                // The two body corners furthest back; one of them, at random.
                const double sx = u.x >= 0 ? 1 : -1, sy = u.y >= 0 ? 1 : -1;
                const bool flip = std::abs(u.x) > std::abs(u.y);
                const double pick = draw01() < .5 ? 1 : -1;
                const double cx = flip ? sx : pick, cy = flip ? pick : sy;
                startStrand(std::atan2(cy, cx),
                    1.2 + 2.5 * draw01(),
                    350 + 400 * draw01(),
                    50 + 200 * draw01());
            }
        }

        const double trailing = trailingAngle();
        for (auto &s : strands_) {
            if (!s.live) continue;
            // A lamella's root slides round the outline with the trailing side; threads stay at
            // their corner or where their bubble let go.
            if (!s.thread)
                s.angle += std::remainder(trailing - s.angle, kTau) * (1 - std::exp(-dt / .15));
            const auto o = outline(s.angle);
            const double moved = speed_ * dt;
            s.travel += moved;
            s.length += moved;
            s.slack = speed_ < .6 * kTrailSpeed ? s.slack + dt : 0;
            // It pinches off once drawn out as far as it can go, once the window no longer draws
            // it on, or once the motion has turned away from its root.
            const bool last = !drawing || s.length >= s.reach || s.slack > .08 ||
                              dot(o.normal, behind_) < .1;
            const double spacing = std::clamp(speed_ * .012, 6.0, 14.0);
            const bool fixed = s.travel >= spacing || last;
            if (fixed) s.travel = 0;
            if (trailCount_ < trail_.size()) {
                BubbleTrailPoint p;
                // Rooted just inside the outline, so it comes out from under the film's edge.
                p.position = sub(o.point, mul(o.normal, std::min(2.0, s.halfWidth)));
                p.velocity = mul(velocity_, kFilmInherit);
                // A lamella necks in as it is drawn out toward the length where it pinches off.
                p.halfWidth = s.halfWidth * (1 - (s.thread ? .2 : .45) * std::min(1.0, s.length / s.reach));
                p.thickness = s.thickness;
                p.strand = s.id;
                p.provisional = !fixed;
                p.last = last;
                trail_[trailCount_++] = p;
            }
            if (last) s.live = false;
        }
    }

    std::span<const BubbleTrailPoint> BubbleMotion::trail() const noexcept {
        return std::span(trail_).first(trailCount_);
    }

    std::span<const BubbleRelease> BubbleMotion::releases() const noexcept {
        return std::span(releases_).first(releaseCount_);
    }

    void BubbleMotion::clearEmissions() noexcept {
        trailCount_ = releaseCount_ = 0;
    }

}  // namespace hyprimate
