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

        constexpr double PI_VALUE = 3.14159265358979323846, TAU = 2 * PI_VALUE;
        constexpr std::array<int, 4> MODES{2, 3, 4, 5};
        // The trailing fluid: one damped mode, underdamped enough to overshoot once when the window
        // stops (about 16%) and quiet again within roughly half a second.
        constexpr double LAG_FREQUENCY = TAU * 1.9, LAG_DAMPING = .5;
        // Capillary waves on the outline: n = 2 rings at this frequency; higher modes follow the
        // dispersion of a 2D drop, w_n^2 ~ n^3 - n, and lose energy faster.
        constexpr double WAVE_FREQUENCY = TAU * 2.3;
        // Interior pressure, the mode the opening's fluid core settles through.
        constexpr double CORE_PRESSURE_FREQUENCY = TAU * 2.6, PRESSURE_DAMPING = .32;
        // Pointer-driven positions arrive unevenly; two smoothing stages keep that jitter out of
        // the acceleration.
        constexpr double VELOCITY_SMOOTHING = .03;
        constexpr double MAX_SPEED = 6000;
        // Below this speed the film neither sheds microbubbles nor leaves a trail.
        constexpr double TRAIL_SPEED = 140, DETACH_SPEED = 220;
        // Below this speed a moving window counts as paused: its shape holds briefly, then spreads.
        constexpr double PAUSE_SPEED = 30;
        // The film follows a layout-animated window critically damped: it eases out of its old
        // place and arrives within about half a second, whatever the layout's own curve.
        constexpr double CARRY_FREQUENCY = 9;
        // Beyond this the film is not left behind: a farther jump moves it mostly with the window.
        constexpr double MAX_CARRY = 2400;

        Vec2 add(Vec2 lhs, Vec2 rhs) {
            return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
        }
        Vec2 sub(Vec2 lhs, Vec2 rhs) {
            return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
        }
        Vec2 mul(Vec2 vector, double scale) {
            return {.x = vector.x * scale, .y = vector.y * scale};
        }
        double dot(Vec2 lhs, Vec2 rhs) {
            return (lhs.x * rhs.x) + (lhs.y * rhs.y);
        }
        double length(Vec2 lhs) {
            return std::hypot(lhs.x, lhs.y);
        }
        bool finite(Vec2 lhs) {
            return std::isfinite(lhs.x) && std::isfinite(lhs.y);
        }
        // Magnitude-preserving soft limit: linear for small values, never beyond limit.
        Vec2 soften(Vec2 value, double limit) {
            const double SPEED = length(value);
            if (!(limit > 0) || !(SPEED > 1e-12)) {
                return {};
            }
            return mul(value, limit * std::tanh(SPEED / limit) / SPEED);
        }
        double mode_frequency(std::size_t index) {
            const double MODE = MODES[index];
            return WAVE_FREQUENCY * std::sqrt(((MODE * MODE * MODE) - MODE) / 6.0);
        }
        double mode_decay(std::size_t index) {
            return 3.5 + (1.6 * static_cast<double>(index));
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
               carry.y == 0 &&
               std::ranges::all_of(waveCos, [](double component) { return component == 0; }) &&
               std::ranges::all_of(waveSin, [](double value) { return value == 0; });
    }

    Vec2 bubbleRoundHalf(Vec2 half_size, const BubbleDeformation &deformation) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        const Vec2 EXTENT{
            .x = std::max(deformation.extent.x, 1.0), .y = std::max(deformation.extent.y, 1.0)};
        return {
            .x = HALF.x + ((EXTENT.x - HALF.x) * deformation.round),
            .y = HALF.y + ((EXTENT.y - HALF.y) * deformation.round),
        };
    }

    Vec2 bubbleRoundPoint(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        if (deformation.round == 0) {
            return point;
        }
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        const double NORM_X = std::clamp(point.x / HALF.x, -1.5, 1.5);
        const double NORM_Y = std::clamp(point.y / HALF.y, -1.5, 1.5);
        const Vec2 DISC{
            .x = deformation.extent.x * NORM_X * std::sqrt(std::max(0.0, 1 - NORM_Y * NORM_Y * .5)),
            .y = deformation.extent.y * NORM_Y * std::sqrt(std::max(0.0, 1 - NORM_X * NORM_X * .5)),
        };
        return add(point, mul(sub(DISC, point), deformation.round));
    }

    Vec2 bubbleMotionPoint(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 ROUNDED = bubbleRoundPoint(half_size, deformation, point);
        return add(
            add(ROUNDED,
                bubbleMotionOffset(bubbleRoundHalf(half_size, deformation), deformation, ROUNDED)),
            deformation.carry);
    }

    Vec2 bubbleMotionOffset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        Vec2 offset{};
        const double DRAG = length(deformation.drag);
        if (DRAG > 1e-6) {
            const Vec2 DIR = mul(deformation.drag, 1 / DRAG);
            const double REACH = (std::abs(DIR.x) * HALF.x) + (std::abs(DIR.y) * HALF.y);
            const double BREADTH = (std::abs(DIR.y) * HALF.x) + (std::abs(DIR.x) * HALF.y);
            const double ALONG = dot(point, DIR);
            const double WEIGHT = smoothstep(-1.0, 1.0, std::clamp(ALONG / REACH, -1.0, 1.0));
            const Vec2 ACROSS = sub(point, mul(DIR, ALONG));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame.
            const double SIDE = std::min(1.0, dot(ACROSS, ACROSS) / (BREADTH * BREADTH));
            offset = sub(mul(deformation.drag, WEIGHT * (1 - (.55 * SIDE))),
                mul(ACROSS, .35 * DRAG / REACH * WEIGHT));
        }
        // Ripples run normal to the outline: a rounded rectangle's, turning into a circle's.
        const double ROUND = std::clamp(deformation.round, 0.0, 1.0);
        const Vec2 NORMALIZED{.x = point.x / HALF.x, .y = point.y / HALF.y};
        const Vec2 SQUARED{.x = NORMALIZED.x * NORMALIZED.x, .y = NORMALIZED.y * NORMALIZED.y};
        const double RHO4 = dot(SQUARED, SQUARED);
        if (RHO4 > 1e-12) {
            const double RHO = std::min((std::sqrt(std::sqrt(RHO4)) * (1 - ROUND)) +
                                            (std::sqrt(dot(NORMALIZED, NORMALIZED)) * ROUND),
                1.25);
            const double PHI = std::atan2(NORMALIZED.y, NORMALIZED.x);
            double radial = 0;
            for (std::size_t i = 0; i < MODES.size(); ++i) {
                radial += (deformation.waveCos[i] * std::cos(MODES[i] * PHI)) +
                          (deformation.waveSin[i] * std::sin(MODES[i] * PHI));
            }
            const Vec2 SQUARE{
                .x = SQUARED.x * NORMALIZED.x / HALF.x,
                .y = SQUARED.y * NORMALIZED.y / HALF.y,
            };
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
        m_pending = add(m_pending, displacement);
        if (!carried) {
            return;
        }
        m_carry = sub(m_carry, displacement);
        const double CARRY_LENGTH = length(m_carry);
        if (CARRY_LENGTH > MAX_CARRY) {
            m_carry = mul(m_carry, MAX_CARRY / CARRY_LENGTH);
        }
    }

    void BubbleMotion::seed(std::uint64_t seed) noexcept {
        m_random = seed;
        for (double &phase : m_phase) {
            phase = TAU * draw01();
        }
        m_threshold = -std::log(1 - draw01());
    }

    void BubbleMotion::reset() noexcept {
        const auto RANDOM = m_random;
        const auto PHASE = m_phase;
        const auto THRESHOLD = m_threshold;
        *this = BubbleMotion{};
        m_random = RANDOM;
        m_phase = PHASE;
        m_threshold = THRESHOLD;
    }

    double BubbleMotion::draw01() noexcept {
        return unit_draw(m_random);
    }

    double BubbleMotion::reference() const noexcept {
        return std::max(1.0, std::min(m_size.x, m_size.y));
    }

    double BubbleMotion::drag_limit() const noexcept {
        return m_params.strength * std::clamp(.09 * reference(), 6.0, 64.0);
    }

    bool BubbleMotion::stirred() const noexcept {
        // A translation not yet advanced already counts: the window must keep stepping to feel it.
        return m_stirred || m_pending.x != 0 || m_pending.y != 0;
    }

    double BubbleMotion::speed() const noexcept {
        return m_speed;
    }

    BubbleDeformation BubbleMotion::deformation() const noexcept {
        return m_shape;
    }

    Vec2 BubbleMotion::core() const noexcept {
        if (!m_stirred) {
            return {};
        }
        const double AMOUNT = .035 * reference() * m_params.strength;
        return {.x = AMOUNT * std::tanh(m_pressure.x), .y = .5 * AMOUNT * std::tanh(m_pressure.y)};
    }

    void BubbleMotion::reshape() noexcept {
        m_shape = {};
        if (!m_stirred) {
            return;
        }
        m_shape.drag = soften(m_lag, drag_limit());
        const double CAP = std::clamp(.035 * reference(), 1.0, 24.0);
        for (std::size_t i = 0; i < m_wave.size(); ++i) {
            const Vec2 WAVE = soften(m_wave[i], CAP);
            m_shape.waveCos[i] = WAVE.x;
            m_shape.waveSin[i] = WAVE.y;
        }
        m_shape.sheen = m_sheen;
        m_shape.carry = m_carry;
        // Each shape is shown short of where its square-to-ellipse map would fold the corners: the
        // pillow keeps the rectangle's straight run along each side, the ellipse and disc nearly
        // lose it. A spring overshooting back past the rectangle is softened to a small outward
        // swell.
        const double ROUNDNESS =
            m_round < 0 ? -.1 * std::tanh(-m_round / .1) : std::min(m_round, 1.06);
        const Vec2 HALF{.x = m_size.x * .5, .y = m_size.y * .5};
        switch (m_params.shape) {
            case BubbleShape::Pillow:
                m_shape.round = .55 * ROUNDNESS;
                m_shape.extent = mul(HALF, 1.1);
                break;
            case BubbleShape::Ellipse:
                m_shape.round = .92 * ROUNDNESS;
                m_shape.extent = mul(HALF, 1.1);
                break;
            case BubbleShape::Circle: {
                m_shape.round = .92 * ROUNDNESS;
                const double RADIUS = .37 * std::sqrt(m_size.x * m_size.y);
                m_shape.extent = {.x = RADIUS, .y = RADIUS};
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
        m_size = size;
        m_body = is_usable(body)
                     ? body
                     : Box{.x = -size.x * .5, .y = -size.y * .5, .w = size.x, .h = size.y};
        m_params = {
            .strength = std::max(0.0, params.strength),
            .microbubbles = std::clamp(params.microbubbles, 0.0, 1.0),
            .trail = std::clamp(params.trail, 0.0, 1.0),
            .roundness = std::clamp(params.roundness, 0.0, 1.0),
            .shape = params.shape,
            .shapeSpeed = std::max(params.shapeSpeed, 100.0),
        };
        const double DELTA_TIME = std::min(elapsed, STALL_SECONDS);
        // The film behind a layout-animated window closes in on it; what the window is seen to do
        // is its own translation plus how far the film moved toward it.
        if (m_carry.x != 0 || m_carry.y != 0 || m_carry_rate.x != 0 || m_carry_rate.y != 0) {
            const double FREQUENCY = CARRY_FREQUENCY;
            const double DECAY = std::exp(-FREQUENCY * DELTA_TIME);
            const Vec2 CARRY_TERM = add(m_carry_rate, mul(m_carry, FREQUENCY));
            m_carry = mul(add(m_carry, mul(CARRY_TERM, DELTA_TIME)), DECAY);
            m_carry_rate = mul(sub(m_carry_rate, mul(CARRY_TERM, FREQUENCY * DELTA_TIME)), DECAY);
        }
        Vec2 observed = mul(add(m_pending, sub(m_carry, m_carry_mark)), 1 / DELTA_TIME);
        m_carry_mark = m_carry;
        m_pending = {};
        if (!finite(observed)) {
            observed = {};
        }
        if (length(observed) > MAX_SPEED) {
            observed = mul(observed, MAX_SPEED / length(observed));
        }
        if (!m_stirred && length(observed) == 0 && m_carry.x == 0 && m_carry.y == 0) {
            return;
        }
        m_stirred = true;

        const double BLEND = 1 - std::exp(-DELTA_TIME / VELOCITY_SMOOTHING);
        const Vec2 PREVIOUS = m_velocity;
        const double PREVIOUS_SPEED = m_speed;
        m_response = add(m_response, mul(sub(observed, m_response), BLEND));
        m_velocity = add(m_velocity, mul(sub(m_response, m_velocity), BLEND));
        m_speed = length(m_velocity);
        const Vec2 ACCELERATION = mul(sub(m_velocity, PREVIOUS), 1 / DELTA_TIME);
        const double SPEED_RATE = (m_speed - PREVIOUS_SPEED) / DELTA_TIME;

        // The faster the window travels the further it contracts. Slowing down lets it relax as
        // it goes; a pause only lets it spread out once it has lasted, so brief stops in a drag do
        // not flicker between shapes.
        const double ASK =
            m_params.strength > 0
                ? m_params.roundness * smoothstep(PAUSE_SPEED, m_params.shapeSpeed, m_speed)
                : 0.0;
        if (ASK >= m_hold) {
            m_hold = ASK;
            m_idle = 0;
        } else if (m_speed > PAUSE_SPEED) {
            m_hold += (ASK - m_hold) * (1 - std::exp(-DELTA_TIME / .3));
            m_idle = 0;
        } else {
            m_idle += DELTA_TIME;
            if (m_idle > .16) {
                m_hold = std::max(ASK, m_hold - (DELTA_TIME / .22));
            }
        }

        m_substep += DELTA_TIME;
        while (m_substep >= SUBSTEP_SECONDS) {
            step(SUBSTEP_SECONDS, ACCELERATION, SPEED_RATE);
            m_substep -= SUBSTEP_SECONDS;
        }
        m_clock += DELTA_TIME;

        // Stress brings colour into the film quickly; it drains away more slowly once released.
        const double LIMIT = drag_limit();
        double waves = 0;
        for (const auto &wave : m_wave) {
            waves += length(wave);
        }
        const double STRESS = (LIMIT > 0 ? length(m_lag) / LIMIT : 0) +
                              (.35 * waves / std::max(1.0, .02 * reference())) +
                              (.3 * std::abs(m_pressure.x));
        const double TARGET = m_params.strength > 0 ? .6 * std::clamp(STRESS, 0.0, 1.0) : 0.0;
        m_sheen += (TARGET - m_sheen) * (1 - std::exp(-DELTA_TIME / (TARGET > m_sheen ? .06 : .3)));

        reshape();
        nucleate(DELTA_TIME, ACCELERATION);
        migrate(DELTA_TIME, ACCELERATION);
        emit_trail(DELTA_TIME, ACCELERATION);

        double wave_rest = 0;
        double wave_rate = 0;
        for (std::size_t i = 0; i < m_wave.size(); ++i) {
            wave_rest = std::max(wave_rest, length(m_wave[i]));
            wave_rate = std::max(wave_rate, length(m_wave_rate[i]));
        }
        const bool QUIET = m_speed < 2 && length(m_response) < 2 && length(m_lag) < .05 &&
                           length(m_lag_rate) < .5 && wave_rest < .02 && wave_rate < .5 &&
                           length(m_pressure) < 1e-3 && length(m_pressure_rate) < 1e-2 &&
                           m_sheen < .003 && m_attached_count == 0 && m_hold == 0 &&
                           std::abs(m_round) < 1e-3 && std::abs(m_round_rate) < 1e-2 &&
                           length(m_carry) < .05 && length(m_carry_rate) < .5;
        if (QUIET) {
            // Every remaining disturbance is far below a pixel: come to rest exactly.
            const auto RANDOM = m_random;
            const auto PHASE = m_phase;
            const auto THRESHOLD = m_threshold;
            const auto TRAIL = m_trail;
            const auto TRAIL_COUNT = m_trail_count;
            const auto RELEASES = m_releases;
            const auto RELEASE_COUNT = m_release_count;
            *this = BubbleMotion{};
            m_random = RANDOM;
            m_phase = PHASE;
            m_threshold = THRESHOLD;
            m_trail = TRAIL;
            m_trail_count = TRAIL_COUNT;
            m_releases = RELEASES;
            m_release_count = RELEASE_COUNT;
        }
    }

    void BubbleMotion::step(double step_seconds, Vec2 acceleration, double speed_rate) {
        const double STRENGTH = m_params.strength;
        const double REFERENCE = reference();
        // The trailing fluid: dragged along by the film at a steady speed, thrown back by
        // acceleration and carried forward by its own momentum when the window stops.
        Vec2 target{};
        if (m_speed > 1e-6) {
            target = mul(m_velocity,
                -STRENGTH * std::clamp(.05 * REFERENCE, 4.0, 32.0) * std::tanh(m_speed / 1600) /
                    m_speed);
        }
        const double LAG_FREQUENCY_SQUARED = LAG_FREQUENCY * LAG_FREQUENCY;
        const Vec2 LAG_FORCE = sub(sub(mul(sub(m_lag, target), -LAG_FREQUENCY_SQUARED),
                                       mul(m_lag_rate, 2 * LAG_DAMPING * LAG_FREQUENCY)),
            mul(acceleration, .45 * STRENGTH));
        m_lag_rate = add(m_lag_rate, mul(LAG_FORCE, step_seconds));
        m_lag = add(m_lag, mul(m_lag_rate, step_seconds));

        // The sloshing interior pushes on the outline in its own direction; air buffets a moving
        // film off resonance, so sustained travel keeps a faint, irregular ripple.
        const double SLOSH = length(m_lag_rate);
        const double THETA = std::atan2(m_lag_rate.y, m_lag_rate.x);
        const double SCALE = STRENGTH * REFERENCE / 600;
        const double BUFFET = SCALE * std::clamp(m_speed / 1600, 0.0, 1.5);
        constexpr std::array<double, 4> GAIN{8.0, 21.0, 30.0, 40.0};
        constexpr std::array<double, 4> GUST{150.0, 340.0, 460.0, 520.0};
        const double TIME = m_clock + m_substep;
        for (std::size_t i = 0; i < m_wave.size(); ++i) {
            const double MODE = MODES[i];
            const double FREQUENCY = mode_frequency(i);
            const double DECAY = mode_decay(i);
            const double DRIFT = (.43 * FREQUENCY * TIME) + m_phase[i];
            const Vec2 FORCE{
                .x = (GAIN[i] * SCALE * SLOSH * std::cos(MODE * THETA)) +
                     (BUFFET * GUST[i] * std::sin(DRIFT)),
                .y = (GAIN[i] * SCALE * SLOSH * std::sin(MODE * THETA)) +
                     (BUFFET * GUST[i] * std::cos((1.13 * DRIFT) + m_phase[(i + 1) % 4])),
            };
            const Vec2 ACCEL = sub(
                sub(FORCE, mul(m_wave[i], FREQUENCY * FREQUENCY)), mul(m_wave_rate[i], 2 * DECAY));
            m_wave_rate[i] = add(m_wave_rate[i], mul(ACCEL, step_seconds));
            m_wave[i] = add(m_wave[i], mul(m_wave_rate[i], step_seconds));
        }

        // The fluid compresses while the window gathers speed and rebounds when it sheds it.
        const double DRIVE = std::clamp(speed_rate / 9000, -1.5, 1.5);
        const double PRESSURE_FREQUENCY = CORE_PRESSURE_FREQUENCY;
        const double PRESSURE_RATE = 1.7 * CORE_PRESSURE_FREQUENCY;
        m_pressure_rate.x += ((-PRESSURE_FREQUENCY * PRESSURE_FREQUENCY * (m_pressure.x - DRIVE)) -
                                 (2 * PRESSURE_DAMPING * PRESSURE_FREQUENCY * m_pressure_rate.x)) *
                             step_seconds;
        m_pressure_rate.y += ((-PRESSURE_RATE * PRESSURE_RATE * (m_pressure.y - (.5 * DRIVE))) -
                                 (2 * .4 * PRESSURE_RATE * m_pressure_rate.y)) *
                             step_seconds;
        m_pressure = add(m_pressure, mul(m_pressure_rate, step_seconds));

        // Surface tension pulls the moving film into a sphere briskly and nearly without
        // overshoot; spreading back into the frame is softer and overshoots a little before the
        // rectangle settles.
        const bool CONTRACTING = m_hold > m_round;
        const double ROUND_FREQUENCY = TAU * (CONTRACTING ? 1.6 : 1.3);
        const double ROUND_DAMPING = CONTRACTING ? .75 : .5;
        m_round_rate += ((-ROUND_FREQUENCY * ROUND_FREQUENCY * (m_round - m_hold)) -
                            (2 * ROUND_DAMPING * ROUND_FREQUENCY * m_round_rate)) *
                        step_seconds;
        m_round += m_round_rate * step_seconds;
    }

    BubbleMotion::Outline BubbleMotion::outline(double angle) const noexcept {
        const Vec2 CENTRE{.x = m_body.x + (m_body.w * .5), .y = m_body.y + (m_body.h * .5)};
        const Vec2 HALF{.x = m_body.w * .5, .y = m_body.h * .5};
        const Vec2 MEMBRANE{.x = m_size.x * .5, .y = m_size.y * .5};
        const auto POINT_AT = [&](double sample_angle) {
            const double COSINE = std::cos(sample_angle);
            const double SINE = std::sin(sample_angle);
            // A steep superellipse: the body's straight sides with softly rounded corners.
            const double FACTOR = std::pow(
                std::pow(std::abs(COSINE), 12.0) + std::pow(std::abs(SINE), 12.0), -1.0 / 12.0);
            const Vec2 POINT =
                add(CENTRE, Vec2{.x = HALF.x * COSINE * FACTOR, .y = HALF.y * SINE * FACTOR});
            return bubbleMotionPoint(MEMBRANE, m_shape, POINT);
        };
        constexpr double EPSILON = .01;
        Outline outline_result;
        outline_result.point = POINT_AT(angle);
        const Vec2 SPAN = sub(POINT_AT(angle + EPSILON), POINT_AT(angle - EPSILON));
        const double SPAN_LENGTH = length(SPAN);
        outline_result.stretch = SPAN_LENGTH / (2 * EPSILON);
        outline_result.tangent = SPAN_LENGTH > 0
                                     ? mul(SPAN, 1 / SPAN_LENGTH)
                                     : Vec2{.x = -std::sin(angle), .y = std::cos(angle)};
        outline_result.normal = {.x = outline_result.tangent.y, .y = -outline_result.tangent.x};
        if (dot(outline_result.normal, sub(outline_result.point, add(CENTRE, m_shape.carry))) < 0) {
            outline_result.normal = mul(outline_result.normal, -1);
        }
        return outline_result;
    }

    double BubbleMotion::trailing_angle() const noexcept {
        Vec2 direction{.x = 1, .y = 0};
        if (m_speed > 20) {
            direction = mul(m_velocity, -1 / m_speed);
        } else if (length(m_lag) > 1e-3) {
            direction = mul(m_lag, 1 / length(m_lag));
        }
        return std::atan2(
            direction.y / std::max(m_body.h, 1.0), direction.x / std::max(m_body.w, 1.0));
    }

    BubbleMicro BubbleMotion::draw(const Attached &attached_bubble) const noexcept {
        const auto OUTLINE = outline(attached_bubble.angle);
        const double GROWTH = smoothstep(0, 1, attached_bubble.growth);
        const double RADIUS = attached_bubble.radius * (.35 + (.65 * GROWTH));
        // A forming bubble begins as a swelling half sunk in the film and rides out on its neck.
        BubbleMicro micro;
        micro.centre = add(OUTLINE.point, mul(OUTLINE.normal, RADIUS * (-.55 + (1.25 * GROWTH))));
        micro.radius = RADIUS;
        micro.growth = attached_bubble.growth;
        micro.anchor = OUTLINE.point;
        micro.attach = 1 - attached_bubble.collapse;
        micro.collapse = attached_bubble.collapse;
        micro.thickness = attached_bubble.thickness;
        micro.seed = attached_bubble.seed;
        return micro;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t COUNT = std::min(m_attached_count, out.size());
        for (std::size_t i = 0; i < COUNT; ++i) {
            out[i] = draw(m_attached[i]);
        }
        return COUNT;
    }

    void BubbleMotion::nucleate(double elapsed, Vec2 acceleration) {
        if (m_params.microbubbles <= 0 || m_params.strength <= 0) {
            return;
        }
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double TRAVEL = std::clamp((m_speed - 150) / 1200, 0.0, 1.5);
        if (TRAVEL <= 0) {
            return;
        }
        const double TURN = turning(m_velocity, m_speed, acceleration);
        // Stretched, fast-moving film is where it thins and sheds bubbles.
        m_spawn += m_params.microbubbles * 12 * TRAVEL * (.35 + STRETCH + TURN) * elapsed;
        while (m_spawn >= m_threshold) {
            m_spawn -= m_threshold;
            m_threshold = -std::log(1 - draw01());
            if (m_attached_count >= m_attached.size()) {
                continue;
            }
            Attached attached;
            const double TRAILING = trailing_angle();
            attached.angle = draw01() < .75
                                 ? TRAILING + ((draw01() + draw01() + draw01() - 1.5) * 1.1)
                                 : TAU * draw01();
            const double DRAW = draw01();
            attached.radius = (4 + (9 * DRAW * DRAW)) * std::clamp(reference() / 700, .8, 1.35);
            attached.grow = .14 + (.2 * draw01());
            attached.life = .45 + (1.1 * draw01());
            attached.thickness = 320 + (520 * draw01());
            attached.seed = draw01();
            const double FATE = draw01();
            attached.fate = FATE < .5 ? Fate::Detach : FATE < .72 ? Fate::Merge : Fate::Collapse;
            m_attached[m_attached_count++] = attached;
        }
    }

    void BubbleMotion::migrate(double elapsed, Vec2 acceleration) {
        const double TRAILING = trailing_angle();
        std::size_t kept = 0;
        for (std::size_t i = 0; i < m_attached_count; ++i) {
            auto attached = m_attached[i];
            attached.age += elapsed;
            const auto OUTLINE = outline(attached.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back toward the trailing side.
            const Vec2 FORCE = sub(mul(acceleration, -.05), mul(m_velocity, 1.1));
            attached.slip += dot(FORCE, OUTLINE.tangent) / std::max(OUTLINE.stretch, 1.0) * elapsed;
            attached.slip = std::clamp(attached.slip * std::exp(-3.2 * elapsed), -3.0, 3.0);
            attached.angle += attached.slip * elapsed;

            const bool EXPIRED = attached.age >= attached.life;
            if (attached.merging ||
                (EXPIRED && attached.fate == Fate::Merge && attached.collapse == 0)) {
                attached.merging = true;
                attached.growth -= elapsed / .3;
                if (attached.growth <= 0) {
                    continue;
                }
            } else if (attached.collapse > 0 || EXPIRED) {
                attached.collapse += elapsed / .09;
                if (attached.collapse >= 1) {
                    continue;
                }
            } else {
                attached.growth = std::min(1.0, attached.age / attached.grow);
                const double OFF = std::remainder(attached.angle - TRAILING, TAU);
                if (attached.fate == Fate::Detach && attached.growth >= 1 &&
                    m_speed > DETACH_SPEED && std::abs(OFF) < .6) {
                    if (m_release_count < m_releases.size()) {
                        const auto HERE = outline(attached.angle);
                        BubbleRelease release;
                        release.micro = draw(attached);
                        release.micro.attach = 0;
                        release.micro.anchor = release.micro.centre;
                        release.velocity = add(add(mul(m_velocity, .55 + (.3 * draw01())),
                                                   mul(HERE.tangent, attached.slip * HERE.stretch)),
                            mul(HERE.normal, 20 + (25 * draw01())));
                        release.life = .35 + (.55 * draw01());
                        m_releases[m_release_count++] = release;
                    }
                    continue;
                }
            }
            m_attached[kept++] = attached;
        }
        m_attached_count = kept;
    }

    void BubbleMotion::emit_trail(double elapsed, Vec2 acceleration) {
        if (m_params.trail <= 0 || m_params.strength <= 0 || m_speed < TRAIL_SPEED) {
            m_trailing = false;
            m_travel = 0;
            return;
        }
        m_travel += m_speed * elapsed;
        if (m_trailing && m_travel < std::clamp(m_speed * .02, 12.0, 40.0)) {
            return;
        }
        m_travel = 0;
        if (m_trail_count >= m_trail.size()) {
            return;
        }
        const Vec2 DIR = mul(m_velocity, 1 / m_speed);
        const auto OUTLINE = outline(trailing_angle());
        const double ACROSS = std::abs(m_body.w * .5 * DIR.y) + std::abs(m_body.h * .5 * DIR.x);
        // A contracted bubble sheds film as wide as it now is.
        const double RESHAPED =
            std::abs(m_shape.extent.x * DIR.y) + std::abs(m_shape.extent.y * DIR.x);
        const double WIDTH = ACROSS + ((RESHAPED - ACROSS) * std::clamp(m_shape.round, 0.0, 1.0));
        // Sharp turns and a stretched film shed more of it.
        const double TURN = turning(m_velocity, m_speed, acceleration);
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double DENSITY =
            std::clamp(2 * m_params.trail *
                           std::pow(std::clamp((m_speed - TRAIL_SPEED) / 2200, 0.0, 1.0), .8) *
                           (.7 + (.5 * STRETCH) + (.6 * TURN)),
                0.0,
                1.0);
        m_trail[m_trail_count++] = {
            .position = OUTLINE.point,
            .direction = DIR,
            .halfWidth = WIDTH * (.22 + (.3 * DENSITY)),
            .density = DENSITY,
            .start = !m_trailing,
        };
        m_trailing = true;
    }

    std::span<const BubbleTrailPoint> BubbleMotion::trail() const noexcept {
        return std::span(m_trail).first(m_trail_count);
    }

    std::span<const BubbleRelease> BubbleMotion::releases() const noexcept {
        return std::span(m_releases).first(m_release_count);
    }

    void BubbleMotion::clearEmissions() noexcept {
        m_trail_count = m_release_count = 0;
    }

}  // namespace hyprimate
