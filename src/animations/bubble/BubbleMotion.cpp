#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/bubble/BubbleMotion.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {

        constexpr double kPi = 3.14159265358979323846, kTau = 2 * kPi;
        constexpr std::array<int, 4> kModes{2, 3, 4, 5};
        // The trailing fluid: one damped mode, underdamped enough to overshoot once when the window
        // stops (about 16%) and quiet again within roughly half a second.
        constexpr double kLagFrequency = kTau * 1.9, kLagDamping = .5;
        // Capillary waves on the outline: n = 2 rings at this frequency; higher modes follow the
        // dispersion of a 2D drop, w_n^2 ~ n^3 - n, and lose energy faster.
        constexpr double kWaveFrequency = kTau * 2.3;
        // Interior pressure, the mode the opening's fluid core settles through.
        constexpr double kPressureFrequency = kTau * 2.6, kPressureDamping = .32;
        // Pointer-driven positions arrive unevenly; two smoothing stages keep that jitter out of
        // the acceleration.
        constexpr double kVelocitySmoothing = .03;
        constexpr double kMaxSpeed = 6000;
        // Below this speed the film neither sheds microbubbles nor leaves a trail.
        constexpr double kTrailSpeed = 140, kDetachSpeed = 220;
        // Below this speed a moving window counts as paused: its shape holds briefly, then spreads.
        constexpr double kPauseSpeed = 30;
        // The film follows a layout-animated window critically damped: it eases out of its old
        // place and arrives within about half a second, whatever the layout's own curve.
        constexpr double kCarryFrequency = 9;
        // Beyond this the film is not left behind: a farther jump moves it mostly with the window.
        constexpr double kMaxCarry = 2400;

        Vec2 add(Vec2 a, Vec2 b) {
            return {.x = a.x + b.x, .y = a.y + b.y};
        }
        Vec2 sub(Vec2 a, Vec2 b) {
            return {.x = a.x - b.x, .y = a.y - b.y};
        }
        Vec2 mul(Vec2 a, double s) {
            return {.x = a.x * s, .y = a.y * s};
        }
        double dot(Vec2 a, Vec2 b) {
            return (a.x * b.x) + (a.y * b.y);
        }
        double length(Vec2 a) {
            return std::hypot(a.x, a.y);
        }
        bool finite(Vec2 a) {
            return std::isfinite(a.x) && std::isfinite(a.y);
        }
        // Magnitude-preserving soft limit: linear for small values, never beyond limit.
        Vec2 soften(Vec2 v, double limit) {
            const double m = length(v);
            if (!(limit > 0) || !(m > 1e-12)) {
                return {};
            }
            return mul(v, limit * std::tanh(m / limit) / m);
        }
        double modeFrequency(std::size_t i) {
            const double n = kModes[i];
            return kWaveFrequency * std::sqrt(((n * n * n) - n) / 6.0);
        }
        double modeDecay(std::size_t i) {
            return 3.5 + (1.6 * static_cast<double>(i));
        }
        // 0..1 how sharply the window is turning: acceleration across its direction of travel.
        double turning(Vec2 velocity, double speed, Vec2 acceleration) {
            if (!(speed > 1e-6)) {
                return 0;
            }
            const Vec2 dir = mul(velocity, 1 / speed);
            return std::clamp(
                std::abs((dir.x * acceleration.y) - (dir.y * acceleration.x)) / 12000, 0.0, 1.0);
        }

    }  // namespace

    bool BubbleDeformation::still() const noexcept {
        return drag.x == 0 && drag.y == 0 && sheen == 0 && round == 0 && carry.x == 0 &&
               carry.y == 0 && std::ranges::all_of(waveCos, [](double v) { return v == 0; }) &&
               std::ranges::all_of(waveSin, [](double v) { return v == 0; });
    }

    Vec2 bubbleRoundHalf(Vec2 halfSize, const BubbleDeformation &m) {
        const Vec2 h{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        const Vec2 e{.x = std::max(m.extent.x, 1.0), .y = std::max(m.extent.y, 1.0)};
        return {.x = h.x + ((e.x - h.x) * m.round), .y = h.y + ((e.y - h.y) * m.round)};
    }

    Vec2 bubbleRoundPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        if (m.round == 0) {
            return q;
        }
        const Vec2 h{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        const double x = std::clamp(q.x / h.x, -1.5, 1.5);
        const double y = std::clamp(q.y / h.y, -1.5, 1.5);
        const Vec2 disc{.x = m.extent.x * x * std::sqrt(std::max(0.0, 1 - y * y * .5)),
            .y = m.extent.y * y * std::sqrt(std::max(0.0, 1 - x * x * .5))};
        return add(q, mul(sub(disc, q), m.round));
    }

    Vec2 bubbleMotionPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        const Vec2 p = bubbleRoundPoint(halfSize, m, q);
        return add(add(p, bubbleMotionOffset(bubbleRoundHalf(halfSize, m), m, p)), m.carry);
    }

    Vec2 bubbleMotionOffset(Vec2 halfSize, const BubbleDeformation &m, Vec2 p) {
        const Vec2 h{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        Vec2 offset{};
        const double drag = length(m.drag);
        if (drag > 1e-6) {
            const Vec2 dir = mul(m.drag, 1 / drag);
            const double reach = (std::abs(dir.x) * h.x) + (std::abs(dir.y) * h.y);
            const double breadth = (std::abs(dir.y) * h.x) + (std::abs(dir.x) * h.y);
            const double along = dot(p, dir);
            const double w = smoothstep(-1.0, 1.0, std::clamp(along / reach, -1.0, 1.0));
            const Vec2 across = sub(p, mul(dir, along));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame.
            const double side = std::min(1.0, dot(across, across) / (breadth * breadth));
            offset = sub(mul(m.drag, w * (1 - (.55 * side))), mul(across, .35 * drag / reach * w));
        }
        // Ripples run normal to the outline: a rounded rectangle's, turning into a circle's.
        const double round = std::clamp(m.round, 0.0, 1.0);
        const Vec2 u{.x = p.x / h.x, .y = p.y / h.y};
        const Vec2 u2{.x = u.x * u.x, .y = u.y * u.y};
        const double rho4 = dot(u2, u2);
        if (rho4 > 1e-12) {
            const double rho = std::min(
                (std::sqrt(std::sqrt(rho4)) * (1 - round)) + (std::sqrt(dot(u, u)) * round), 1.25);
            const double phi = std::atan2(u.y, u.x);
            double radial = 0;
            for (std::size_t i = 0; i < kModes.size(); ++i) {
                radial += (m.waveCos[i] * std::cos(kModes[i] * phi)) +
                          (m.waveSin[i] * std::sin(kModes[i] * phi));
            }
            const Vec2 square{.x = u2.x * u.x / h.x, .y = u2.y * u.y / h.y};
            const Vec2 circle{.x = u.x / h.x, .y = u.y / h.y};
            const Vec2 g = add(mul(square, (1 - round) / std::max(length(square), 1e-12)),
                mul(circle, round / std::max(length(circle), 1e-12)));
            const double gl = length(g);
            if (gl > 0) {
                offset = add(offset, mul(g, radial * rho * rho * rho / gl));
            }
        }
        return offset;
    }

    void BubbleMotion::drive(Vec2 displacement, bool carried) noexcept {
        if (!finite(displacement)) {
            return;
        }
        pending_ = add(pending_, displacement);
        if (!carried) {
            return;
        }
        carry_ = sub(carry_, displacement);
        const double c = length(carry_);
        if (c > kMaxCarry) {
            carry_ = mul(carry_, kMaxCarry / c);
        }
    }

    void BubbleMotion::seed(std::uint64_t seed) noexcept {
        random_ = seed;
        for (double &phase : phase_) {
            phase = kTau * draw01();
        }
        threshold_ = -std::log(1 - draw01());
    }

    void BubbleMotion::reset() noexcept {
        const auto random = random_;
        const auto phase = phase_;
        const auto threshold = threshold_;
        *this = BubbleMotion{};
        random_ = random;
        phase_ = phase;
        threshold_ = threshold;
    }

    double BubbleMotion::draw01() noexcept {
        return unit_draw(random_);
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
        if (!stirred_) {
            return {};
        }
        const double amount = .035 * reference() * params_.strength;
        return {.x = amount * std::tanh(pressure_.x), .y = .5 * amount * std::tanh(pressure_.y)};
    }

    void BubbleMotion::reshape() noexcept {
        shape_ = {};
        if (!stirred_) {
            return;
        }
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
        // pillow keeps the rectangle's straight run along each side, the ellipse and disc nearly
        // lose it. A spring overshooting back past the rectangle is softened to a small outward
        // swell.
        const double r = round_ < 0 ? -.1 * std::tanh(-round_ / .1) : std::min(round_, 1.06);
        const Vec2 half{.x = size_.x * .5, .y = size_.y * .5};
        switch (params_.shape) {
            case BubbleShape::Pillow:
                shape_.round = .55 * r;
                shape_.extent = mul(half, 1.1);
                break;
            case BubbleShape::Ellipse:
                shape_.round = .92 * r;
                shape_.extent = mul(half, 1.1);
                break;
            case BubbleShape::Circle: {
                shape_.round = .92 * r;
                const double radius = .37 * std::sqrt(size_.x * size_.y);
                shape_.extent = {.x = radius, .y = radius};
                break;
            }
        }
    }

    void BubbleMotion::advance(
        double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        if (!(std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0)) {
            reset();
            return;
        }
        size_ = size;
        body_ = is_usable(body)
                    ? body
                    : Box{.x = -size.x * .5, .y = -size.y * .5, .w = size.x, .h = size.y};
        params_ = {.strength = std::max(0.0, params.strength),
            .microbubbles = std::clamp(params.microbubbles, 0.0, 1.0),
            .trail = std::clamp(params.trail, 0.0, 1.0),
            .roundness = std::clamp(params.roundness, 0.0, 1.0),
            .shape = params.shape,
            .shapeSpeed = std::max(params.shapeSpeed, 100.0)};
        const double dt = std::min(elapsed, STALL_SECONDS);
        // The film behind a layout-animated window closes in on it; what the window is seen to do
        // is its own translation plus how far the film moved toward it.
        if (carry_.x != 0 || carry_.y != 0 || carryRate_.x != 0 || carryRate_.y != 0) {
            const double w = kCarryFrequency;
            const double e = std::exp(-w * dt);
            const Vec2 c = add(carryRate_, mul(carry_, w));
            carry_ = mul(add(carry_, mul(c, dt)), e);
            carryRate_ = mul(sub(carryRate_, mul(c, w * dt)), e);
        }
        Vec2 observed = mul(add(pending_, sub(carry_, carryMark_)), 1 / dt);
        carryMark_ = carry_;
        pending_ = {};
        if (!finite(observed)) {
            observed = {};
        }
        if (length(observed) > kMaxSpeed) {
            observed = mul(observed, kMaxSpeed / length(observed));
        }
        if (!stirred_ && length(observed) == 0 && carry_.x == 0 && carry_.y == 0) {
            return;
        }
        stirred_ = true;

        const double blend = 1 - std::exp(-dt / kVelocitySmoothing);
        const Vec2 previous = velocity_;
        const double previousSpeed = speed_;
        response_ = add(response_, mul(sub(observed, response_), blend));
        velocity_ = add(velocity_, mul(sub(response_, velocity_), blend));
        speed_ = length(velocity_);
        const Vec2 acceleration = mul(sub(velocity_, previous), 1 / dt);
        const double speedRate = (speed_ - previousSpeed) / dt;

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
            if (idle_ > .16) {
                hold_ = std::max(ask, hold_ - (dt / .22));
            }
        }

        substep_ += dt;
        while (substep_ >= SUBSTEP_SECONDS) {
            step(SUBSTEP_SECONDS, acceleration, speedRate);
            substep_ -= SUBSTEP_SECONDS;
        }
        clock_ += dt;

        // Stress brings colour into the film quickly; it drains away more slowly once released.
        const double limit = dragLimit();
        double waves = 0;
        for (const auto &w : wave_) {
            waves += length(w);
        }
        const double stress = (limit > 0 ? length(lag_) / limit : 0) +
                              (.35 * waves / std::max(1.0, .02 * reference())) +
                              (.3 * std::abs(pressure_.x));
        const double target = params_.strength > 0 ? .6 * std::clamp(stress, 0.0, 1.0) : 0.0;
        sheen_ += (target - sheen_) * (1 - std::exp(-dt / (target > sheen_ ? .06 : .3)));

        reshape();
        nucleate(dt, acceleration);
        migrate(dt, acceleration);
        emitTrail(dt, acceleration);

        double waveRest = 0;
        double waveRate = 0;
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            waveRest = std::max(waveRest, length(wave_[i]));
            waveRate = std::max(waveRate, length(waveRate_[i]));
        }
        const bool quiet = speed_ < 2 && length(response_) < 2 && length(lag_) < .05 &&
                           length(lagRate_) < .5 && waveRest < .02 && waveRate < .5 &&
                           length(pressure_) < 1e-3 && length(pressureRate_) < 1e-2 &&
                           sheen_ < .003 && attachedCount_ == 0 && hold_ == 0 &&
                           std::abs(round_) < 1e-3 && std::abs(roundRate_) < 1e-2 &&
                           length(carry_) < .05 && length(carryRate_) < .5;
        if (quiet) {
            // Every remaining disturbance is far below a pixel: come to rest exactly.
            const auto random = random_;
            const auto phase = phase_;
            const auto threshold = threshold_;
            const auto trail = trail_;
            const auto trailCount = trailCount_;
            const auto releases = releases_;
            const auto releaseCount = releaseCount_;
            *this = BubbleMotion{};
            random_ = random;
            phase_ = phase;
            threshold_ = threshold;
            trail_ = trail;
            trailCount_ = trailCount;
            releases_ = releases;
            releaseCount_ = releaseCount;
        }
    }

    void BubbleMotion::step(double h, Vec2 acceleration, double speedRate) {
        const double strength = params_.strength;
        const double L = reference();
        // The trailing fluid: dragged along by the film at a steady speed, thrown back by
        // acceleration and carried forward by its own momentum when the window stops.
        Vec2 target{};
        if (speed_ > 1e-6) {
            target = mul(velocity_,
                -strength * std::clamp(.05 * L, 4.0, 32.0) * std::tanh(speed_ / 1600) / speed_);
        }
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
            const double n = kModes[i];
            const double w = modeFrequency(i);
            const double decay = modeDecay(i);
            const double drift = (.43 * w * time) + phase_[i];
            const Vec2 force{.x = (gain[i] * scale * slosh * std::cos(n * theta)) +
                                  (buffet * gust[i] * std::sin(drift)),
                .y = (gain[i] * scale * slosh * std::sin(n * theta)) +
                     (buffet * gust[i] * std::cos((1.13 * drift) + phase_[(i + 1) % 4]))};
            const Vec2 accel = sub(sub(force, mul(wave_[i], w * w)), mul(waveRate_[i], 2 * decay));
            waveRate_[i] = add(waveRate_[i], mul(accel, h));
            wave_[i] = add(wave_[i], mul(waveRate_[i], h));
        }

        // The fluid compresses while the window gathers speed and rebounds when it sheds it.
        const double drive = std::clamp(speedRate / 9000, -1.5, 1.5);
        const double wp = kPressureFrequency;
        const double wr = 1.7 * kPressureFrequency;
        pressureRate_.x +=
            ((-wp * wp * (pressure_.x - drive)) - (2 * kPressureDamping * wp * pressureRate_.x)) *
            h;
        pressureRate_.y +=
            ((-wr * wr * (pressure_.y - (.5 * drive))) - (2 * .4 * wr * pressureRate_.y)) * h;
        pressure_ = add(pressure_, mul(pressureRate_, h));

        // Surface tension pulls the moving film into a sphere briskly and nearly without
        // overshoot; spreading back into the frame is softer and overshoots a little before the
        // rectangle settles.
        const bool contracting = hold_ > round_;
        const double wc = kTau * (contracting ? 1.6 : 1.3);
        const double zc = contracting ? .75 : .5;
        roundRate_ += ((-wc * wc * (round_ - hold_)) - (2 * zc * wc * roundRate_)) * h;
        round_ += roundRate_ * h;
    }

    BubbleMotion::Outline BubbleMotion::outline(double angle) const noexcept {
        const Vec2 centre{.x = body_.x + (body_.w * .5), .y = body_.y + (body_.h * .5)};
        const Vec2 half{.x = body_.w * .5, .y = body_.h * .5};
        const Vec2 membrane{.x = size_.x * .5, .y = size_.y * .5};
        const auto at = [&](double a) {
            const double c = std::cos(a);
            const double s = std::sin(a);
            // A steep superellipse: the body's straight sides with softly rounded corners.
            const double k =
                std::pow(std::pow(std::abs(c), 12.0) + std::pow(std::abs(s), 12.0), -1.0 / 12.0);
            const Vec2 p = add(centre, Vec2{.x = half.x * c * k, .y = half.y * s * k});
            return bubbleMotionPoint(membrane, shape_, p);
        };
        constexpr double epsilon = .01;
        Outline o;
        o.point = at(angle);
        const Vec2 span = sub(at(angle + epsilon), at(angle - epsilon));
        const double l = length(span);
        o.stretch = l / (2 * epsilon);
        o.tangent = l > 0 ? mul(span, 1 / l) : Vec2{.x = -std::sin(angle), .y = std::cos(angle)};
        o.normal = {.x = o.tangent.y, .y = -o.tangent.x};
        if (dot(o.normal, sub(o.point, add(centre, shape_.carry))) < 0) {
            o.normal = mul(o.normal, -1);
        }
        return o;
    }

    double BubbleMotion::trailingAngle() const noexcept {
        Vec2 d{.x = 1, .y = 0};
        if (speed_ > 20) {
            d = mul(velocity_, -1 / speed_);
        } else if (length(lag_) > 1e-3) {
            d = mul(lag_, 1 / length(lag_));
        }
        return std::atan2(d.y / std::max(body_.h, 1.0), d.x / std::max(body_.w, 1.0));
    }

    BubbleMicro BubbleMotion::draw(const Attached &b) const noexcept {
        const auto o = outline(b.angle);
        const double e = smoothstep(0, 1, b.growth);
        const double r = b.radius * (.35 + (.65 * e));
        // A forming bubble begins as a swelling half sunk in the film and rides out on its neck.
        BubbleMicro m;
        m.centre = add(o.point, mul(o.normal, r * (-.55 + (1.25 * e))));
        m.radius = r;
        m.growth = b.growth;
        m.anchor = o.point;
        m.attach = 1 - b.collapse;
        m.collapse = b.collapse;
        m.thickness = b.thickness;
        m.seed = b.seed;
        return m;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t count = std::min(attachedCount_, out.size());
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = draw(attached_[i]);
        }
        return count;
    }

    void BubbleMotion::nucleate(double dt, Vec2 acceleration) {
        if (params_.microbubbles <= 0 || params_.strength <= 0) {
            return;
        }
        const double limit = dragLimit();
        const double stretch = limit > 0 ? std::clamp(length(lag_) / limit, 0.0, 1.0) : 0;
        const double travel = std::clamp((speed_ - 150) / 1200, 0.0, 1.5);
        if (travel <= 0) {
            return;
        }
        const double turn = turning(velocity_, speed_, acceleration);
        // Stretched, fast-moving film is where it thins and sheds bubbles.
        spawn_ += params_.microbubbles * 12 * travel * (.35 + stretch + turn) * dt;
        while (spawn_ >= threshold_) {
            spawn_ -= threshold_;
            threshold_ = -std::log(1 - draw01());
            if (attachedCount_ >= attached_.size()) {
                continue;
            }
            Attached b;
            const double trailing = trailingAngle();
            b.angle = draw01() < .75 ? trailing + ((draw01() + draw01() + draw01() - 1.5) * 1.1)
                                     : kTau * draw01();
            const double r = draw01();
            b.radius = (4 + (9 * r * r)) * std::clamp(reference() / 700, .8, 1.35);
            b.grow = .14 + (.2 * draw01());
            b.life = .45 + (1.1 * draw01());
            b.thickness = 320 + (520 * draw01());
            b.seed = draw01();
            const double fate = draw01();
            b.fate = fate < .5 ? Fate::Detach : fate < .72 ? Fate::Merge : Fate::Collapse;
            attached_[attachedCount_++] = b;
        }
    }

    void BubbleMotion::migrate(double dt, Vec2 acceleration) {
        const double trailing = trailingAngle();
        std::size_t kept = 0;
        for (std::size_t i = 0; i < attachedCount_; ++i) {
            auto b = attached_[i];
            b.age += dt;
            const auto o = outline(b.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back toward the trailing side.
            const Vec2 force = sub(mul(acceleration, -.05), mul(velocity_, 1.1));
            b.slip += dot(force, o.tangent) / std::max(o.stretch, 1.0) * dt;
            b.slip = std::clamp(b.slip * std::exp(-3.2 * dt), -3.0, 3.0);
            b.angle += b.slip * dt;

            const bool expired = b.age >= b.life;
            if (b.merging || (expired && b.fate == Fate::Merge && b.collapse == 0)) {
                b.merging = true;
                b.growth -= dt / .3;
                if (b.growth <= 0) {
                    continue;
                }
            } else if (b.collapse > 0 || expired) {
                b.collapse += dt / .09;
                if (b.collapse >= 1) {
                    continue;
                }
            } else {
                b.growth = std::min(1.0, b.age / b.grow);
                const double off = std::remainder(b.angle - trailing, kTau);
                if (b.fate == Fate::Detach && b.growth >= 1 && speed_ > kDetachSpeed &&
                    std::abs(off) < .6) {
                    if (releaseCount_ < releases_.size()) {
                        const auto here = outline(b.angle);
                        BubbleRelease r;
                        r.micro = draw(b);
                        r.micro.attach = 0;
                        r.micro.anchor = r.micro.centre;
                        r.velocity = add(add(mul(velocity_, .55 + (.3 * draw01())),
                                             mul(here.tangent, b.slip * here.stretch)),
                            mul(here.normal, 20 + (25 * draw01())));
                        r.life = .35 + (.55 * draw01());
                        releases_[releaseCount_++] = r;
                    }
                    continue;
                }
            }
            attached_[kept++] = b;
        }
        attachedCount_ = kept;
    }

    void BubbleMotion::emitTrail(double dt, Vec2 acceleration) {
        if (params_.trail <= 0 || params_.strength <= 0 || speed_ < kTrailSpeed) {
            trailing_ = false;
            travel_ = 0;
            return;
        }
        travel_ += speed_ * dt;
        if (trailing_ && travel_ < std::clamp(speed_ * .02, 12.0, 40.0)) {
            return;
        }
        travel_ = 0;
        if (trailCount_ >= trail_.size()) {
            return;
        }
        const Vec2 dir = mul(velocity_, 1 / speed_);
        const auto o = outline(trailingAngle());
        const double across = std::abs(body_.w * .5 * dir.y) + std::abs(body_.h * .5 * dir.x);
        // A contracted bubble sheds film as wide as it now is.
        const double reshaped =
            std::abs(shape_.extent.x * dir.y) + std::abs(shape_.extent.y * dir.x);
        const double width = across + ((reshaped - across) * std::clamp(shape_.round, 0.0, 1.0));
        // Sharp turns and a stretched film shed more of it.
        const double turn = turning(velocity_, speed_, acceleration);
        const double limit = dragLimit();
        const double stretch = limit > 0 ? std::clamp(length(lag_) / limit, 0.0, 1.0) : 0;
        const double density = std::clamp(
            2 * params_.trail * std::pow(std::clamp((speed_ - kTrailSpeed) / 2200, 0.0, 1.0), .8) *
                (.7 + (.5 * stretch) + (.6 * turn)),
            0.0,
            1.0);
        trail_[trailCount_++] = {.position = o.point,
            .direction = dir,
            .halfWidth = width * (.22 + (.3 * density)),
            .density = density,
            .start = !trailing_};
        trailing_ = true;
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
