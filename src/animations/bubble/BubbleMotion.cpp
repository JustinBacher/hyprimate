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
            const double SPEED = length(v);
            if (!(limit > 0) || !(SPEED > 1e-12)) {
                return {};
            }
            return mul(v, limit * std::tanh(SPEED / limit) / SPEED);
        }
        double modeFrequency(std::size_t i) {
            const double MODE = kModes[i];
            return kWaveFrequency * std::sqrt(((MODE * MODE * MODE) - MODE) / 6.0);
        }
        double modeDecay(std::size_t i) {
            return 3.5 + (1.6 * static_cast<double>(i));
        }
        // 0..1 how sharply the window is turning: acceleration across its direction of travel.
        double turning(Vec2 velocity, double speed, Vec2 acceleration) {
            if (!(speed > 1e-6)) {
                return 0;
            }
            const Vec2 DIR = mul(velocity, 1 / speed);
            return std::clamp(
                std::abs((DIR.x * acceleration.y) - (DIR.y * acceleration.x)) / 12000, 0.0, 1.0);
        }

    }  // namespace

    bool BubbleDeformation::still() const noexcept {
        return drag.x == 0 && drag.y == 0 && sheen == 0 && round == 0 && carry.x == 0 &&
               carry.y == 0 && std::ranges::all_of(waveCos, [](double v) { return v == 0; }) &&
               std::ranges::all_of(waveSin, [](double v) { return v == 0; });
    }

    Vec2 bubbleRoundHalf(Vec2 halfSize, const BubbleDeformation &m) {
        const Vec2 HALF{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        const Vec2 EXTENT{.x = std::max(m.extent.x, 1.0), .y = std::max(m.extent.y, 1.0)};
        return {.x = HALF.x + ((EXTENT.x - HALF.x) * m.round),
            .y = HALF.y + ((EXTENT.y - HALF.y) * m.round)};
    }

    Vec2 bubbleRoundPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        if (m.round == 0) {
            return q;
        }
        const Vec2 HALF{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        const double NORM_X = std::clamp(q.x / HALF.x, -1.5, 1.5);
        const double NORM_Y = std::clamp(q.y / HALF.y, -1.5, 1.5);
        const Vec2 DISC{
            .x = m.extent.x * NORM_X * std::sqrt(std::max(0.0, 1 - NORM_Y * NORM_Y * .5)),
            .y = m.extent.y * NORM_Y * std::sqrt(std::max(0.0, 1 - NORM_X * NORM_X * .5))};
        return add(q, mul(sub(DISC, q), m.round));
    }

    Vec2 bubbleMotionPoint(Vec2 halfSize, const BubbleDeformation &m, Vec2 q) {
        const Vec2 ROUNDED = bubbleRoundPoint(halfSize, m, q);
        return add(
            add(ROUNDED, bubbleMotionOffset(bubbleRoundHalf(halfSize, m), m, ROUNDED)), m.carry);
    }

    Vec2 bubbleMotionOffset(Vec2 halfSize, const BubbleDeformation &m, Vec2 p) {
        const Vec2 HALF{.x = std::max(halfSize.x, 1.0), .y = std::max(halfSize.y, 1.0)};
        Vec2 offset{};
        const double DRAG = length(m.drag);
        if (DRAG > 1e-6) {
            const Vec2 DIR = mul(m.drag, 1 / DRAG);
            const double REACH = (std::abs(DIR.x) * HALF.x) + (std::abs(DIR.y) * HALF.y);
            const double BREADTH = (std::abs(DIR.y) * HALF.x) + (std::abs(DIR.x) * HALF.y);
            const double ALONG = dot(p, DIR);
            const double WEIGHT = smoothstep(-1.0, 1.0, std::clamp(ALONG / REACH, -1.0, 1.0));
            const Vec2 ACROSS = sub(p, mul(DIR, ALONG));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame.
            const double SIDE = std::min(1.0, dot(ACROSS, ACROSS) / (BREADTH * BREADTH));
            offset = sub(
                mul(m.drag, WEIGHT * (1 - (.55 * SIDE))), mul(ACROSS, .35 * DRAG / REACH * WEIGHT));
        }
        // Ripples run normal to the outline: a rounded rectangle's, turning into a circle's.
        const double ROUND = std::clamp(m.round, 0.0, 1.0);
        const Vec2 NORMALIZED{.x = p.x / HALF.x, .y = p.y / HALF.y};
        const Vec2 SQUARED{.x = NORMALIZED.x * NORMALIZED.x, .y = NORMALIZED.y * NORMALIZED.y};
        const double RHO4 = dot(SQUARED, SQUARED);
        if (RHO4 > 1e-12) {
            const double RHO = std::min((std::sqrt(std::sqrt(RHO4)) * (1 - ROUND)) +
                                            (std::sqrt(dot(NORMALIZED, NORMALIZED)) * ROUND),
                1.25);
            const double PHI = std::atan2(NORMALIZED.y, NORMALIZED.x);
            double radial = 0;
            for (std::size_t i = 0; i < kModes.size(); ++i) {
                radial += (m.waveCos[i] * std::cos(kModes[i] * PHI)) +
                          (m.waveSin[i] * std::sin(kModes[i] * PHI));
            }
            const Vec2 SQUARE{
                .x = SQUARED.x * NORMALIZED.x / HALF.x, .y = SQUARED.y * NORMALIZED.y / HALF.y};
            const Vec2 CIRCLE{.x = NORMALIZED.x / HALF.x, .y = NORMALIZED.y / HALF.y};
            const Vec2 GRADIENT = add(mul(SQUARE, (1 - ROUND) / std::max(length(SQUARE), 1e-12)),
                mul(CIRCLE, ROUND / std::max(length(CIRCLE), 1e-12)));
            const double GRADIENT_LENGTH = length(GRADIENT);
            if (GRADIENT_LENGTH > 0) {
                offset = add(offset, mul(GRADIENT, radial * RHO * RHO * RHO / GRADIENT_LENGTH));
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
        const double CARRY_LENGTH = length(carry_);
        if (CARRY_LENGTH > kMaxCarry) {
            carry_ = mul(carry_, kMaxCarry / CARRY_LENGTH);
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
        const auto RANDOM = random_;
        const auto PHASE = phase_;
        const auto THRESHOLD = threshold_;
        *this = BubbleMotion{};
        random_ = RANDOM;
        phase_ = PHASE;
        threshold_ = THRESHOLD;
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
        const double AMOUNT = .035 * reference() * params_.strength;
        return {.x = AMOUNT * std::tanh(pressure_.x), .y = .5 * AMOUNT * std::tanh(pressure_.y)};
    }

    void BubbleMotion::reshape() noexcept {
        shape_ = {};
        if (!stirred_) {
            return;
        }
        shape_.drag = soften(lag_, dragLimit());
        const double CAP = std::clamp(.035 * reference(), 1.0, 24.0);
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            const Vec2 WAVE = soften(wave_[i], CAP);
            shape_.waveCos[i] = WAVE.x;
            shape_.waveSin[i] = WAVE.y;
        }
        shape_.sheen = sheen_;
        shape_.carry = carry_;
        // Each shape is shown short of where its square-to-ellipse map would fold the corners: the
        // pillow keeps the rectangle's straight run along each side, the ellipse and disc nearly
        // lose it. A spring overshooting back past the rectangle is softened to a small outward
        // swell.
        const double ROUNDNESS =
            round_ < 0 ? -.1 * std::tanh(-round_ / .1) : std::min(round_, 1.06);
        const Vec2 HALF{.x = size_.x * .5, .y = size_.y * .5};
        switch (params_.shape) {
            case BubbleShape::Pillow:
                shape_.round = .55 * ROUNDNESS;
                shape_.extent = mul(HALF, 1.1);
                break;
            case BubbleShape::Ellipse:
                shape_.round = .92 * ROUNDNESS;
                shape_.extent = mul(HALF, 1.1);
                break;
            case BubbleShape::Circle: {
                shape_.round = .92 * ROUNDNESS;
                const double RADIUS = .37 * std::sqrt(size_.x * size_.y);
                shape_.extent = {.x = RADIUS, .y = RADIUS};
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
        const double DELTA_TIME = std::min(elapsed, STALL_SECONDS);
        // The film behind a layout-animated window closes in on it; what the window is seen to do
        // is its own translation plus how far the film moved toward it.
        if (carry_.x != 0 || carry_.y != 0 || carryRate_.x != 0 || carryRate_.y != 0) {
            const double FREQUENCY = kCarryFrequency;
            const double DECAY = std::exp(-FREQUENCY * DELTA_TIME);
            const Vec2 CARRY_TERM = add(carryRate_, mul(carry_, FREQUENCY));
            carry_ = mul(add(carry_, mul(CARRY_TERM, DELTA_TIME)), DECAY);
            carryRate_ = mul(sub(carryRate_, mul(CARRY_TERM, FREQUENCY * DELTA_TIME)), DECAY);
        }
        Vec2 observed = mul(add(pending_, sub(carry_, carryMark_)), 1 / DELTA_TIME);
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

        const double BLEND = 1 - std::exp(-DELTA_TIME / kVelocitySmoothing);
        const Vec2 PREVIOUS = velocity_;
        const double PREVIOUS_SPEED = speed_;
        response_ = add(response_, mul(sub(observed, response_), BLEND));
        velocity_ = add(velocity_, mul(sub(response_, velocity_), BLEND));
        speed_ = length(velocity_);
        const Vec2 ACCELERATION = mul(sub(velocity_, PREVIOUS), 1 / DELTA_TIME);
        const double SPEED_RATE = (speed_ - PREVIOUS_SPEED) / DELTA_TIME;

        // The faster the window travels the further it contracts. Slowing down lets it relax as
        // it goes; a pause only lets it spread out once it has lasted, so brief stops in a drag do
        // not flicker between shapes.
        const double ASK =
            params_.strength > 0
                ? params_.roundness * smoothstep(kPauseSpeed, params_.shapeSpeed, speed_)
                : 0.0;
        if (ASK >= hold_) {
            hold_ = ASK;
            idle_ = 0;
        } else if (speed_ > kPauseSpeed) {
            hold_ += (ASK - hold_) * (1 - std::exp(-DELTA_TIME / .3));
            idle_ = 0;
        } else {
            idle_ += DELTA_TIME;
            if (idle_ > .16) {
                hold_ = std::max(ASK, hold_ - (DELTA_TIME / .22));
            }
        }

        substep_ += DELTA_TIME;
        while (substep_ >= SUBSTEP_SECONDS) {
            step(SUBSTEP_SECONDS, ACCELERATION, SPEED_RATE);
            substep_ -= SUBSTEP_SECONDS;
        }
        clock_ += DELTA_TIME;

        // Stress brings colour into the film quickly; it drains away more slowly once released.
        const double LIMIT = dragLimit();
        double waves = 0;
        for (const auto &wave : wave_) {
            waves += length(wave);
        }
        const double STRESS = (LIMIT > 0 ? length(lag_) / LIMIT : 0) +
                              (.35 * waves / std::max(1.0, .02 * reference())) +
                              (.3 * std::abs(pressure_.x));
        const double TARGET = params_.strength > 0 ? .6 * std::clamp(STRESS, 0.0, 1.0) : 0.0;
        sheen_ += (TARGET - sheen_) * (1 - std::exp(-DELTA_TIME / (TARGET > sheen_ ? .06 : .3)));

        reshape();
        nucleate(DELTA_TIME, ACCELERATION);
        migrate(DELTA_TIME, ACCELERATION);
        emitTrail(DELTA_TIME, ACCELERATION);

        double waveRest = 0;
        double waveRate = 0;
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            waveRest = std::max(waveRest, length(wave_[i]));
            waveRate = std::max(waveRate, length(waveRate_[i]));
        }
        const bool QUIET = speed_ < 2 && length(response_) < 2 && length(lag_) < .05 &&
                           length(lagRate_) < .5 && waveRest < .02 && waveRate < .5 &&
                           length(pressure_) < 1e-3 && length(pressureRate_) < 1e-2 &&
                           sheen_ < .003 && attachedCount_ == 0 && hold_ == 0 &&
                           std::abs(round_) < 1e-3 && std::abs(roundRate_) < 1e-2 &&
                           length(carry_) < .05 && length(carryRate_) < .5;
        if (QUIET) {
            // Every remaining disturbance is far below a pixel: come to rest exactly.
            const auto RANDOM = random_;
            const auto PHASE = phase_;
            const auto THRESHOLD = threshold_;
            const auto TRAIL = trail_;
            const auto TRAIL_COUNT = trailCount_;
            const auto RELEASES = releases_;
            const auto RELEASE_COUNT = releaseCount_;
            *this = BubbleMotion{};
            random_ = RANDOM;
            phase_ = PHASE;
            threshold_ = THRESHOLD;
            trail_ = TRAIL;
            trailCount_ = TRAIL_COUNT;
            releases_ = RELEASES;
            releaseCount_ = RELEASE_COUNT;
        }
    }

    void BubbleMotion::step(double h, Vec2 acceleration, double speedRate) {
        const double STRENGTH = params_.strength;
        const double REFERENCE = reference();
        // The trailing fluid: dragged along by the film at a steady speed, thrown back by
        // acceleration and carried forward by its own momentum when the window stops.
        Vec2 target{};
        if (speed_ > 1e-6) {
            target = mul(velocity_,
                -STRENGTH * std::clamp(.05 * REFERENCE, 4.0, 32.0) * std::tanh(speed_ / 1600) /
                    speed_);
        }
        const double LAG_FREQUENCY_SQUARED = kLagFrequency * kLagFrequency;
        const Vec2 LAG_FORCE = sub(sub(mul(sub(lag_, target), -LAG_FREQUENCY_SQUARED),
                                       mul(lagRate_, 2 * kLagDamping * kLagFrequency)),
            mul(acceleration, .45 * STRENGTH));
        lagRate_ = add(lagRate_, mul(LAG_FORCE, h));
        lag_ = add(lag_, mul(lagRate_, h));

        // The sloshing interior pushes on the outline in its own direction; air buffets a moving
        // film off resonance, so sustained travel keeps a faint, irregular ripple.
        const double SLOSH = length(lagRate_);
        const double THETA = std::atan2(lagRate_.y, lagRate_.x);
        const double SCALE = STRENGTH * REFERENCE / 600;
        const double BUFFET = SCALE * std::clamp(speed_ / 1600, 0.0, 1.5);
        constexpr std::array<double, 4> GAIN{8.0, 21.0, 30.0, 40.0};
        constexpr std::array<double, 4> GUST{150.0, 340.0, 460.0, 520.0};
        const double TIME = clock_ + substep_;
        for (std::size_t i = 0; i < wave_.size(); ++i) {
            const double MODE = kModes[i];
            const double FREQUENCY = modeFrequency(i);
            const double DECAY = modeDecay(i);
            const double DRIFT = (.43 * FREQUENCY * TIME) + phase_[i];
            const Vec2 FORCE{.x = (GAIN[i] * SCALE * SLOSH * std::cos(MODE * THETA)) +
                                  (BUFFET * GUST[i] * std::sin(DRIFT)),
                .y = (GAIN[i] * SCALE * SLOSH * std::sin(MODE * THETA)) +
                     (BUFFET * GUST[i] * std::cos((1.13 * DRIFT) + phase_[(i + 1) % 4]))};
            const Vec2 ACCEL =
                sub(sub(FORCE, mul(wave_[i], FREQUENCY * FREQUENCY)), mul(waveRate_[i], 2 * DECAY));
            waveRate_[i] = add(waveRate_[i], mul(ACCEL, h));
            wave_[i] = add(wave_[i], mul(waveRate_[i], h));
        }

        // The fluid compresses while the window gathers speed and rebounds when it sheds it.
        const double DRIVE = std::clamp(speedRate / 9000, -1.5, 1.5);
        const double PRESSURE_FREQUENCY = kPressureFrequency;
        const double PRESSURE_RATE = 1.7 * kPressureFrequency;
        pressureRate_.x += ((-PRESSURE_FREQUENCY * PRESSURE_FREQUENCY * (pressure_.x - DRIVE)) -
                               (2 * kPressureDamping * PRESSURE_FREQUENCY * pressureRate_.x)) *
                           h;
        pressureRate_.y += ((-PRESSURE_RATE * PRESSURE_RATE * (pressure_.y - (.5 * DRIVE))) -
                               (2 * .4 * PRESSURE_RATE * pressureRate_.y)) *
                           h;
        pressure_ = add(pressure_, mul(pressureRate_, h));

        // Surface tension pulls the moving film into a sphere briskly and nearly without
        // overshoot; spreading back into the frame is softer and overshoots a little before the
        // rectangle settles.
        const bool CONTRACTING = hold_ > round_;
        const double ROUND_FREQUENCY = kTau * (CONTRACTING ? 1.6 : 1.3);
        const double ROUND_DAMPING = CONTRACTING ? .75 : .5;
        roundRate_ += ((-ROUND_FREQUENCY * ROUND_FREQUENCY * (round_ - hold_)) -
                          (2 * ROUND_DAMPING * ROUND_FREQUENCY * roundRate_)) *
                      h;
        round_ += roundRate_ * h;
    }

    BubbleMotion::Outline BubbleMotion::outline(double angle) const noexcept {
        const Vec2 CENTRE{.x = body_.x + (body_.w * .5), .y = body_.y + (body_.h * .5)};
        const Vec2 HALF{.x = body_.w * .5, .y = body_.h * .5};
        const Vec2 MEMBRANE{.x = size_.x * .5, .y = size_.y * .5};
        const auto POINT_AT = [&](double a) {
            const double COSINE = std::cos(a);
            const double SINE = std::sin(a);
            // A steep superellipse: the body's straight sides with softly rounded corners.
            const double FACTOR = std::pow(
                std::pow(std::abs(COSINE), 12.0) + std::pow(std::abs(SINE), 12.0), -1.0 / 12.0);
            const Vec2 POINT =
                add(CENTRE, Vec2{.x = HALF.x * COSINE * FACTOR, .y = HALF.y * SINE * FACTOR});
            return bubbleMotionPoint(MEMBRANE, shape_, POINT);
        };
        constexpr double EPSILON = .01;
        Outline outlineResult;
        outlineResult.point = POINT_AT(angle);
        const Vec2 SPAN = sub(POINT_AT(angle + EPSILON), POINT_AT(angle - EPSILON));
        const double SPAN_LENGTH = length(SPAN);
        outlineResult.stretch = SPAN_LENGTH / (2 * EPSILON);
        outlineResult.tangent = SPAN_LENGTH > 0 ? mul(SPAN, 1 / SPAN_LENGTH)
                                                : Vec2{.x = -std::sin(angle), .y = std::cos(angle)};
        outlineResult.normal = {.x = outlineResult.tangent.y, .y = -outlineResult.tangent.x};
        if (dot(outlineResult.normal, sub(outlineResult.point, add(CENTRE, shape_.carry))) < 0) {
            outlineResult.normal = mul(outlineResult.normal, -1);
        }
        return outlineResult;
    }

    double BubbleMotion::trailingAngle() const noexcept {
        Vec2 direction{.x = 1, .y = 0};
        if (speed_ > 20) {
            direction = mul(velocity_, -1 / speed_);
        } else if (length(lag_) > 1e-3) {
            direction = mul(lag_, 1 / length(lag_));
        }
        return std::atan2(
            direction.y / std::max(body_.h, 1.0), direction.x / std::max(body_.w, 1.0));
    }

    BubbleMicro BubbleMotion::draw(const Attached &b) const noexcept {
        const auto OUTLINE = outline(b.angle);
        const double GROWTH = smoothstep(0, 1, b.growth);
        const double RADIUS = b.radius * (.35 + (.65 * GROWTH));
        // A forming bubble begins as a swelling half sunk in the film and rides out on its neck.
        BubbleMicro micro;
        micro.centre = add(OUTLINE.point, mul(OUTLINE.normal, RADIUS * (-.55 + (1.25 * GROWTH))));
        micro.radius = RADIUS;
        micro.growth = b.growth;
        micro.anchor = OUTLINE.point;
        micro.attach = 1 - b.collapse;
        micro.collapse = b.collapse;
        micro.thickness = b.thickness;
        micro.seed = b.seed;
        return micro;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t COUNT = std::min(attachedCount_, out.size());
        for (std::size_t i = 0; i < COUNT; ++i) {
            out[i] = draw(attached_[i]);
        }
        return COUNT;
    }

    void BubbleMotion::nucleate(double dt, Vec2 acceleration) {
        if (params_.microbubbles <= 0 || params_.strength <= 0) {
            return;
        }
        const double LIMIT = dragLimit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(lag_) / LIMIT, 0.0, 1.0) : 0;
        const double TRAVEL = std::clamp((speed_ - 150) / 1200, 0.0, 1.5);
        if (TRAVEL <= 0) {
            return;
        }
        const double TURN = turning(velocity_, speed_, acceleration);
        // Stretched, fast-moving film is where it thins and sheds bubbles.
        spawn_ += params_.microbubbles * 12 * TRAVEL * (.35 + STRETCH + TURN) * dt;
        while (spawn_ >= threshold_) {
            spawn_ -= threshold_;
            threshold_ = -std::log(1 - draw01());
            if (attachedCount_ >= attached_.size()) {
                continue;
            }
            Attached attached;
            const double TRAILING = trailingAngle();
            attached.angle = draw01() < .75
                                 ? TRAILING + ((draw01() + draw01() + draw01() - 1.5) * 1.1)
                                 : kTau * draw01();
            const double DRAW = draw01();
            attached.radius = (4 + (9 * DRAW * DRAW)) * std::clamp(reference() / 700, .8, 1.35);
            attached.grow = .14 + (.2 * draw01());
            attached.life = .45 + (1.1 * draw01());
            attached.thickness = 320 + (520 * draw01());
            attached.seed = draw01();
            const double FATE = draw01();
            attached.fate = FATE < .5 ? Fate::Detach : FATE < .72 ? Fate::Merge : Fate::Collapse;
            attached_[attachedCount_++] = attached;
        }
    }

    void BubbleMotion::migrate(double dt, Vec2 acceleration) {
        const double TRAILING = trailingAngle();
        std::size_t kept = 0;
        for (std::size_t i = 0; i < attachedCount_; ++i) {
            auto attached = attached_[i];
            attached.age += dt;
            const auto OUTLINE = outline(attached.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back toward the trailing side.
            const Vec2 FORCE = sub(mul(acceleration, -.05), mul(velocity_, 1.1));
            attached.slip += dot(FORCE, OUTLINE.tangent) / std::max(OUTLINE.stretch, 1.0) * dt;
            attached.slip = std::clamp(attached.slip * std::exp(-3.2 * dt), -3.0, 3.0);
            attached.angle += attached.slip * dt;

            const bool EXPIRED = attached.age >= attached.life;
            if (attached.merging ||
                (EXPIRED && attached.fate == Fate::Merge && attached.collapse == 0)) {
                attached.merging = true;
                attached.growth -= dt / .3;
                if (attached.growth <= 0) {
                    continue;
                }
            } else if (attached.collapse > 0 || EXPIRED) {
                attached.collapse += dt / .09;
                if (attached.collapse >= 1) {
                    continue;
                }
            } else {
                attached.growth = std::min(1.0, attached.age / attached.grow);
                const double OFF = std::remainder(attached.angle - TRAILING, kTau);
                if (attached.fate == Fate::Detach && attached.growth >= 1 &&
                    speed_ > kDetachSpeed && std::abs(OFF) < .6) {
                    if (releaseCount_ < releases_.size()) {
                        const auto HERE = outline(attached.angle);
                        BubbleRelease release;
                        release.micro = draw(attached);
                        release.micro.attach = 0;
                        release.micro.anchor = release.micro.centre;
                        release.velocity = add(add(mul(velocity_, .55 + (.3 * draw01())),
                                                   mul(HERE.tangent, attached.slip * HERE.stretch)),
                            mul(HERE.normal, 20 + (25 * draw01())));
                        release.life = .35 + (.55 * draw01());
                        releases_[releaseCount_++] = release;
                    }
                    continue;
                }
            }
            attached_[kept++] = attached;
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
        const Vec2 DIR = mul(velocity_, 1 / speed_);
        const auto OUTLINE = outline(trailingAngle());
        const double ACROSS = std::abs(body_.w * .5 * DIR.y) + std::abs(body_.h * .5 * DIR.x);
        // A contracted bubble sheds film as wide as it now is.
        const double RESHAPED =
            std::abs(shape_.extent.x * DIR.y) + std::abs(shape_.extent.y * DIR.x);
        const double WIDTH = ACROSS + ((RESHAPED - ACROSS) * std::clamp(shape_.round, 0.0, 1.0));
        // Sharp turns and a stretched film shed more of it.
        const double TURN = turning(velocity_, speed_, acceleration);
        const double LIMIT = dragLimit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(lag_) / LIMIT, 0.0, 1.0) : 0;
        const double DENSITY = std::clamp(
            2 * params_.trail * std::pow(std::clamp((speed_ - kTrailSpeed) / 2200, 0.0, 1.0), .8) *
                (.7 + (.5 * STRETCH) + (.6 * TURN)),
            0.0,
            1.0);
        trail_[trailCount_++] = {.position = OUTLINE.point,
            .direction = DIR,
            .halfWidth = WIDTH * (.22 + (.3 * DENSITY)),
            .density = DENSITY,
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
