#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "shared/Core.hpp"
#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleMotionPrivate.hpp"

namespace hyprimate {
    using namespace bubble_motion_detail;


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
        const auto STRAND = m_next_strand;
        *this = BubbleMotion{};
        m_random = RANDOM;
        m_phase = PHASE;
        m_threshold = THRESHOLD;
        m_next_strand = STRAND;
    }

    double BubbleMotion::draw01() noexcept {
        return unit_draw(m_random);
    }

    double BubbleMotion::gauss() noexcept {
        // Irwin–Hall: close enough to normal for size spreads, and bounded.
        return (draw01() + draw01() + draw01() + draw01() - 2) * std::sqrt(3.0);
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
        auto *cosine = m_shape.wave_cos.begin();
        auto *sine = m_shape.wave_sin.begin();
        for (const Vec2 SHAPE_WAVE : m_wave) {
            const Vec2 WAVE = soften(SHAPE_WAVE, CAP);
            *cosine = WAVE.x;
            ++cosine;
            *sine = WAVE.y;
            ++sine;
        }
        m_shape.sheen = m_sheen;
        m_shape.carry = m_carry;
        // Each shape is shown short of where its square-to-ellipse map would fold the corners: the
        // pillow keeps the rectangle's straight run along each side and draws in slightly, the
        // ellipse and disc nearly lose it. A spring overshooting back past the rectangle is
        // softened to a small outward swell.
        const double ROUNDNESS =
            m_round < 0 ? -.1 * std::tanh(-m_round / .1) : std::min(m_round, 1.06);
        const Vec2 HALF{.x = m_size.x * .5, .y = m_size.y * .5};
        switch (m_params.shape) {
            case BubbleShape::Pillow:
                m_shape.round = PILLOW_ROUND * ROUNDNESS;
                m_shape.extent = mul(HALF, PILLOW_EXTENT);
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
        // The bubbles pressing hardest on the film swell it; the rest are too small to show.
        std::array<BubbleBud, K_BUBBLE_MICRO_CAPACITY> buds{};
        // NOLINTNEXTLINE(misc-const-correctness): The cursor writes each bud below.
        auto *next_bud = buds.begin();
        for (const auto &attached : std::span(m_attached).first(m_attached_count)) {
            *next_bud++ = bud(attached);
        }
        std::partial_sort(buds.begin(),
            buds.begin() + K_BUBBLE_BUDS,
            buds.end(),
            [](const BubbleBud &left, const BubbleBud &right) {
                return left.height > right.height;
            });
        std::copy_n(buds.begin(), K_BUBBLE_BUDS, m_shape.buds.begin());
    }

    void BubbleMotion::advance(
        double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || !(size.x > 0) || !(size.y > 0)) {
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
            .shape_speed = std::max(params.shape_speed, 100.0),
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
        if (m_speed > 20) {
            const Vec2 BACK = mul(m_velocity, -1 / m_speed);
            if (length(m_behind) < .5) {
                m_behind = BACK;
            } else {
                const double TURN =
                    std::atan2((m_behind.x * BACK.y) - (m_behind.y * BACK.x), dot(m_behind, BACK)) *
                    (1 - std::exp(-DELTA_TIME / BEHIND_TURN));
                const double COSINE = std::cos(TURN);
                const double SINE = std::sin(TURN);
                m_behind = {.x = (m_behind.x * COSINE) - (m_behind.y * SINE),
                    .y = (m_behind.x * SINE) + (m_behind.y * COSINE)};
            }
        }

        // The faster the window travels the further it contracts. Slowing down lets it relax as
        // it goes; a pause only lets it spread out once it has lasted, so brief stops in a drag do
        // not flicker between shapes.
        const double ASK =
            m_params.strength > 0
                ? m_params.roundness * smoothstep(PAUSE_SPEED, m_params.shape_speed, m_speed)
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
        reshape();

        double wave_rest = 0;
        double wave_rate = 0;
        const auto *rate = m_wave_rate.begin();
        for (const Vec2 SHAPE_WAVE : m_wave) {
            wave_rest = std::max(wave_rest, length(SHAPE_WAVE));
            wave_rate = std::max(wave_rate, length(*rate++));
        }
        const bool STRANDS =
            std::ranges::any_of(m_strands, [](const Strand &strand) { return strand.live; });
        const bool QUIET = m_speed < 2 && length(m_response) < 2 && length(m_lag) < .05 &&
                           length(m_lag_rate) < .5 && wave_rest < .02 && wave_rate < .5 &&
                           length(m_pressure) < 1e-3 && length(m_pressure_rate) < 1e-2 &&
                           m_sheen < .003 && m_attached_count == 0 && !STRANDS && m_hold == 0 &&
                           std::abs(m_round) < 1e-3 && std::abs(m_round_rate) < 1e-2 &&
                           length(m_carry) < .05 && length(m_carry_rate) < .5;
        if (QUIET) {
            // Every remaining disturbance is far below a pixel: come to rest exactly.
            const auto TRAIL = m_trail;
            const auto TRAIL_COUNT = m_trail_count;
            const auto RELEASES = m_releases;
            const auto RELEASE_COUNT = m_release_count;
            reset();
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
        const auto *mode = MODES.begin();
        const auto *gain = GAIN.begin();
        const auto *gust = GUST.begin();
        const auto *phase = m_phase.begin();
        auto *rate = m_wave_rate.begin();
        std::size_t wave_index = 0;
        for (auto &wave : m_wave) {
            const double MODE = *mode++;
            const double FREQUENCY = mode_frequency(MODE);
            const double DECAY = mode_decay(wave_index);
            const double DRIFT = (.43 * FREQUENCY * TIME) + *phase++;
            const Vec2 FORCE{
                .x = (*gain * SCALE * SLOSH * std::cos(MODE * THETA)) +
                     (BUFFET * *gust * std::sin(DRIFT)),
                .y = (*gain * SCALE * SLOSH * std::sin(MODE * THETA)) +
                     (BUFFET * *gust * std::cos((1.13 * DRIFT) + m_phase[(wave_index + 1) % 4])),
            };
            const Vec2 ACCEL =
                sub(sub(FORCE, mul(wave, FREQUENCY * FREQUENCY)), mul(*rate, 2 * DECAY));
            *rate = add(*rate, mul(ACCEL, step_seconds));
            wave = add(wave, mul(*rate, step_seconds));
            ++gain;
            ++gust;
            ++rate;
            ++wave_index;
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

        // Surface tension draws the moving film in nearly without overshoot; spreading back into
        // the frame is softer and swells past it once before the rectangle settles.
        const bool CONTRACTING = m_hold > m_round;
        const double ROUND_FREQUENCY = TAU * (CONTRACTING ? 1.6 : 1.3);
        const double ROUND_DAMPING = CONTRACTING ? .75 : .6;
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
            return bubble_motion_point(MEMBRANE, m_shape, POINT);
        };
        constexpr double EPSILON = .01;
        Outline outline_result;
        outline_result.point = POINT_AT(angle);
        const Vec2 BEFORE = POINT_AT(angle - EPSILON);
        const Vec2 AFTER = POINT_AT(angle + EPSILON);
        const Vec2 SPAN = sub(AFTER, BEFORE);
        const double SPAN_LENGTH = length(SPAN);
        outline_result.stretch = SPAN_LENGTH / (2 * EPSILON);
        outline_result.tangent = SPAN_LENGTH > 0
                                     ? mul(SPAN, 1 / SPAN_LENGTH)
                                     : Vec2{.x = -std::sin(angle), .y = std::cos(angle)};
        outline_result.normal = {.x = outline_result.tangent.y, .y = -outline_result.tangent.x};
        if (dot(outline_result.normal, sub(outline_result.point, add(CENTRE, m_shape.carry))) < 0) {
            outline_result.normal = mul(outline_result.normal, -1);
        }
        const Vec2 BEND = sub(add(AFTER, BEFORE), mul(outline_result.point, 2));
        outline_result.curvature =
            outline_result.stretch > 1e-6
                ? length(BEND) /
                      (outline_result.stretch * outline_result.stretch * EPSILON * EPSILON)
                : 0;
        return outline_result;
    }

}  // namespace hyprimate
