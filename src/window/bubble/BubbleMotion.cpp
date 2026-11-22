#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

#include "shared/Core.hpp"
#include "window/bubble/BubbleMotion.hpp"

namespace hyprimate {
    namespace {

        constexpr double PI_VALUE = std::numbers::pi, TAU = 2 * PI_VALUE;
        constexpr std::array<int, 4> MODES{2, 3, 4, 5};
        // The trailing fluid: one damped mode, underdamped enough to overshoot once when the window
        // stops (about 16%) and quiet again within roughly half a second.
        constexpr double LAG_FREQUENCY = TAU * 1.9, LAG_DAMPING = .5;
        constexpr double LEAD_FLATTEN = .3;
        // Capillary waves on the outline: n = 2 rings at this frequency; higher modes follow the
        // dispersion of a 2D drop, w_n^2 ~ n^3 - n, and lose energy faster.
        constexpr double WAVE_FREQUENCY = TAU * 2.3;
        // Interior pressure, the mode the opening's fluid core settles through.
        constexpr double CORE_PRESSURE_FREQUENCY = TAU * 2.6, PRESSURE_DAMPING = .32;
        // Pointer-driven positions arrive unevenly; two smoothing stages keep that jitter out of
        // the acceleration.
        constexpr double VELOCITY_SMOOTHING = .03;
        constexpr double MAX_SPEED = 6000;
        // Below this speed the film draws out no strands.
        constexpr double TRAIL_SPEED = 140;
        // Below this speed a moving window counts as paused: its shape holds briefly, then spreads.
        constexpr double PAUSE_SPEED = 30;
        // The film follows a layout-animated window critically damped: it eases out of its old
        // place and arrives within about half a second, whatever the layout's own curve.
        constexpr double CARRY_FREQUENCY = 9;
        // Beyond this the film is not left behind: a farther jump moves it mostly with the window.
        constexpr double MAX_CARRY = 2400;
        constexpr double PILLOW_ROUND = .5, PILLOW_EXTENT = .92;
        constexpr double BEHIND_TURN = .12;
        constexpr double FILM_INHERIT = .3;

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
        // The outline quadrupole (cos 2phi, sin 2phi) that elongates a bubble along unit n.
        Vec2 mode(Vec2 direction) {
            return {.x = (direction.x * direction.x) - (direction.y * direction.y),
                .y = 2 * direction.x * direction.y};
        }
        // Magnitude-preserving soft limit: linear for small values, never beyond limit.
        Vec2 soften(Vec2 value, double limit) {
            const double SPEED = length(value);
            if (!(limit > 0) || !(SPEED > 1e-12)) {
                return {};
            }
            return mul(value, limit * std::tanh(SPEED / limit) / SPEED);
        }
        double mode_frequency(double mode) {
            return WAVE_FREQUENCY * std::sqrt(((mode * mode * mode) - mode) / 6.0);
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
        // Outward direction of the rounded membrane (half size h, roundness `round`) at q: a
        // rounded rectangle's normal turning into an ellipse's. Not normalised; zero at the centre.
        Vec2 outward(Vec2 half_size, double roundness, Vec2 point) {
            const Vec2 UNIT{.x = point.x / half_size.x, .y = point.y / half_size.y};
            const Vec2 SQUARE{.x = UNIT.x * UNIT.x * UNIT.x / half_size.x,
                .y = UNIT.y * UNIT.y * UNIT.y / half_size.y};
            const Vec2 CIRCLE{.x = UNIT.x / half_size.x, .y = UNIT.y / half_size.y};
            const double SQUARE_LENGTH = length(SQUARE);
            const double CIRCLE_LENGTH = length(CIRCLE);
            if (!(SQUARE_LENGTH > 1e-12) || !(CIRCLE_LENGTH > 1e-12)) {
                return {};
            }
            return add(mul(SQUARE, (1 - roundness) / SQUARE_LENGTH),
                mul(CIRCLE, roundness / CIRCLE_LENGTH));
        }
        // Advances an outline quadrupole oscillator toward target; stable for any step.
        void ring(Vec2 &squash,
            Vec2 &rate,
            Vec2 target,
            double frequency,
            double damping,
            double elapsed) {
            const int STEPS = std::max(1, static_cast<int>(std::ceil(frequency * elapsed / .2)));
            const double STEP = elapsed / STEPS;
            for (int i = 0; i < STEPS; ++i) {
                rate = add(rate,
                    mul(add(mul(sub(squash, target), -frequency * frequency),
                            mul(rate, -2 * damping * frequency)),
                        STEP));
                squash = add(squash, mul(rate, STEP));
            }
        }

    }  // namespace

    bool BubbleDeformation::still() const noexcept {
        return drag.x == 0 && drag.y == 0 && sheen == 0 && round == 0 && carry.x == 0 &&
               carry.y == 0 &&
               std::ranges::all_of(wave_cos, [](double component) { return component == 0; }) &&
               std::ranges::all_of(wave_sin, [](double value) { return value == 0; }) &&
               std::ranges::all_of(buds, [](const BubbleBud &bud) { return bud.height == 0; });
    }

    Vec2 bubble_round_half(Vec2 half_size, const BubbleDeformation &deformation) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        const Vec2 EXTENT{
            .x = std::max(deformation.extent.x, 1.0),
            .y = std::max(deformation.extent.y, 1.0),
        };
        return {
            .x = HALF.x + ((EXTENT.x - HALF.x) * deformation.round),
            .y = HALF.y + ((EXTENT.y - HALF.y) * deformation.round),
        };
    }

    Vec2 bubble_round_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
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

    Vec2 bubble_motion_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 ROUNDED = bubble_round_point(half_size, deformation, point);
        return add(add(ROUNDED,
                       bubble_motion_offset(
                           bubble_round_half(half_size, deformation), deformation, ROUNDED)),
            deformation.carry);
    }

    Vec2 bubble_motion_offset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        Vec2 offset{};
        const double DRAG = length(deformation.drag);
        if (DRAG > 1e-6) {
            const Vec2 DIR = mul(deformation.drag, 1 / DRAG);
            const double REACH = (std::abs(DIR.x) * HALF.x) + (std::abs(DIR.y) * HALF.y);
            const double BREADTH = (std::abs(DIR.y) * HALF.x) + (std::abs(DIR.x) * HALF.y);
            const double ALONG = dot(point, DIR);
            const double WEIGHT = smoothstep(-1.0, 1.0, std::clamp(ALONG / REACH, -1.0, 1.0));
            const double LEAD = smoothstep(0.0, 1.0, std::clamp(-ALONG / REACH, 0.0, 1.0));
            const Vec2 ACROSS = sub(point, mul(DIR, ALONG));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame; the middle of the leading face is pressed back.
            const double SIDE = std::min(1.0, dot(ACROSS, ACROSS) / (BREADTH * BREADTH));
            offset = sub(mul(deformation.drag,
                             (WEIGHT * (1 - (.55 * SIDE))) + (LEAD_FLATTEN * LEAD * (1 - SIDE))),
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
            const auto *cosine = deformation.wave_cos.begin();
            const auto *sine = deformation.wave_sin.begin();
            for (const double MODE : MODES) {
                radial += (*cosine++ * std::cos(MODE * PHI)) + (*sine++ * std::sin(MODE * PHI));
            }
            const Vec2 GRADIENT = outward(HALF, ROUND, point);
            const double GRADIENT_LENGTH = length(GRADIENT);
            if (GRADIENT_LENGTH > 0) {
                offset = add(offset, mul(GRADIENT, radial * RHO * RHO * RHO / GRADIENT_LENGTH));
            }
        }
        // Buds: the film swells outward around each foot, along the outline's normal there.
        for (const auto &bud : deformation.buds) {
            if (!(bud.height > 0) || !(bud.width > 0)) {
                continue;
            }
            const Vec2 DELTA = sub(point, bud.foot);
            const double FACTOR = std::exp(-dot(DELTA, DELTA) / (bud.width * bud.width));
            if (FACTOR < 1e-4) {
                continue;
            }
            const Vec2 NORMAL = outward(HALF, ROUND, bud.foot);
            const double NORMAL_LENGTH = length(NORMAL);
            if (NORMAL_LENGTH > 0) {
                offset = add(offset, mul(NORMAL, bud.height * FACTOR / NORMAL_LENGTH));
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

    double BubbleMotion::exposure(double angle) const noexcept {
        if (length(m_behind) < .5) {
            return 0;
        }
        return std::max(0.0, dot(outline(angle).normal, m_behind));
    }

    double BubbleMotion::trailing_angle() const noexcept {
        const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
        return std::atan2(
            DIRECTION.y / std::max(m_body.h, 1.0), DIRECTION.x / std::max(m_body.w, 1.0));
    }

    BubbleBud BubbleMotion::bud(const Attached &attached_bubble) const noexcept {
        const Vec2 CENTRE{.x = m_body.x + (m_body.w * .5), .y = m_body.y + (m_body.h * .5)};
        const Vec2 HALF{.x = m_body.w * .5, .y = m_body.h * .5};
        const double COSINE = std::cos(attached_bubble.angle);
        const double SINE = std::sin(attached_bubble.angle);
        const double FACTOR = std::pow(
            std::pow(std::abs(COSINE), 12.0) + std::pow(std::abs(SINE), 12.0), -1.0 / 12.0);
        BubbleBud result;
        result.foot = bubble_round_point({.x = m_size.x * .5, .y = m_size.y * .5},
            m_shape,
            add(CENTRE, Vec2{.x = HALF.x * COSINE * FACTOR, .y = HALF.y * SINE * FACTOR}));
        result.width = 1.25 * attached_bubble.radius;
        const double FORMED = .22 * attached_bubble.radius;
        double height = 0;
        switch (attached_bubble.stage) {
            case Stage::Bud:
                height = attached_bubble.radius *
                         ((.5 * smoothstep(0, .45, attached_bubble.growth)) -
                             (.28 * smoothstep(.45, 1, attached_bubble.growth)));
                break;
            case Stage::Ride:
                height = FORMED;
                break;
            case Stage::Pinch:
                height = FORMED + (.3 * attached_bubble.radius * attached_bubble.pinch);
                break;
            case Stage::Merge:
                height = attached_bubble.radius * attached_bubble.growth *
                         (.22 + (.5 * (1 - attached_bubble.growth)));
                break;
            case Stage::Pop:
                height = FORMED;
                break;
        }
        result.height = std::clamp(height * (1 - attached_bubble.collapse), 0.0, .5 * result.width);
        return result;
    }

    BubbleMicro BubbleMotion::draw(const Attached &attached_bubble) const noexcept {
        const auto OUTLINE = outline(attached_bubble.angle);
        const double GROWTH = smoothstep(0, 1, attached_bubble.growth);
        const double RADIUS = attached_bubble.radius * (.35 + (.65 * GROWTH));
        BubbleMicro micro;
        micro.centre = add(OUTLINE.point,
            mul(OUTLINE.normal, RADIUS * (-.55 + (1.4 * GROWTH) + (.9 * attached_bubble.pinch))));
        micro.radius = RADIUS;
        micro.growth = attached_bubble.growth;
        micro.anchor = OUTLINE.point;
        micro.attach = (1 - attached_bubble.collapse) * (1 - (.8 * attached_bubble.pinch));
        micro.collapse = attached_bubble.collapse;
        micro.thickness = attached_bubble.thickness;
        micro.seed = attached_bubble.seed;
        micro.visibility =
            attached_bubble.stage == Stage::Bud     ? smoothstep(.2, .6, attached_bubble.growth)
            : attached_bubble.stage == Stage::Merge ? smoothstep(0, .5, attached_bubble.growth)
                                                    : 1.0;
        micro.squash =
            add(attached_bubble.squash, mul(mode(OUTLINE.normal), .12 * attached_bubble.pinch));
        return micro;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t COUNT = std::min(m_attached_count, out.size());
        auto attached = std::span(m_attached).first(COUNT).begin();
        for (auto &micro : out.first(COUNT)) {
            micro = draw(*attached++);
        }
        return COUNT;
    }

    void BubbleMotion::nucleate(double elapsed, Vec2 acceleration) {
        if (m_params.microbubbles <= 0 || m_params.strength <= 0) {
            return;
        }
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double TRAVEL = std::clamp((m_speed - 120) / 1100, 0.0, 1.5);
        const double JOLT = m_speed > 60 ? std::clamp(length(acceleration) / 14000, 0.0, 1.0) : 0;
        if (TRAVEL <= 0 && JOLT <= 0) {
            return;
        }
        const double TURN = turning(m_velocity, m_speed, acceleration);
        m_spawn += m_params.microbubbles * 40 * ((TRAVEL * (.35 + STRETCH + TURN)) + (.8 * JOLT)) *
                   elapsed;
        while (m_spawn >= m_threshold) {
            m_spawn -= m_threshold;
            m_threshold = -std::log(1 - draw01());
            if (m_attached_count >= m_attached.size()) {
                continue;
            }
            Attached attached;
            attached.radius = std::clamp(
                13 * std::exp(.45 * gauss()) * std::clamp(reference() / 600, .85, 1.5), 4.0, 34.0);
            double best = 0;
            double total = 0;
            for (int i = 0; i < 6; ++i) {
                const double ANGLE = TAU * draw01();
                const double EXPOSURE = exposure(ANGLE);
                const double WEIGHT = .12 + (1.6 * EXPOSURE * EXPOSURE) +
                                      (.5 * std::pow(std::abs(std::sin(2 * ANGLE)), 8.0));
                total += WEIGHT;
                if (draw01() * total < WEIGHT) {
                    best = ANGLE;
                }
            }
            attached.angle = best;
            const auto HERE = outline(attached.angle);
            const bool CROWDED = std::ranges::any_of(
                std::span(m_attached).first(m_attached_count), [&](const Attached &other) {
                    return std::abs(std::remainder(other.angle - attached.angle, TAU)) *
                               HERE.stretch <
                           .8 * (other.radius + attached.radius);
                });
            if (CROWDED) {
                continue;
            }
            attached.grow = (.16 + (.3 * draw01())) * std::pow(attached.radius / 8, .3);
            attached.patience = .35 + (1.2 * draw01());
            attached.hold = .25 + (.85 * draw01());
            attached.pinch_rate = 1 / (.1 + (.14 * draw01()));
            attached.thickness = 300 + (550 * draw01());
            attached.seed = draw01();
            attached.pops = draw01() < .35;
            m_attached[m_attached_count++] = attached;
        }
    }

    void BubbleMotion::release(const Attached &attached) {
        if (m_release_count >= m_releases.size()) {
            return;
        }
        const auto OUTLINE = outline(attached.angle);
        BubbleRelease result;
        result.micro = draw(attached);
        result.velocity = add(add(mul(m_velocity, .5 + (.3 * draw01())),
                                  mul(OUTLINE.tangent, attached.slip * OUTLINE.stretch)),
            mul(OUTLINE.normal, 25 + (35 * draw01())));
        result.squash_rate = add(mul(attached.squash_rate, 1.0), mul(mode(OUTLINE.normal), -2.5));
        result.life = .5 + (.9 * draw01());
        m_releases[m_release_count++] = result;
        const auto FOOT = bud(attached).foot;
        const Vec2 HALF = bubble_round_half({.x = m_size.x * .5, .y = m_size.y * .5}, m_shape);
        const double THETA =
            std::atan2(FOOT.y / std::max(HALF.y, 1.0), FOOT.x / std::max(HALF.x, 1.0));
        auto *rate = m_wave_rate.begin();
        std::size_t index = 0;
        for (const double MODE : MODES) {
            const double AMPLITUDE =
                -5 * attached.radius * m_params.strength / static_cast<double>(index + 1);
            *rate = add(*rate,
                Vec2{.x = AMPLITUDE * std::cos(MODE * THETA),
                    .y = AMPLITUDE * std::sin(MODE * THETA)});
            ++rate;
            ++index;
        }
        start_strand(attached.angle,
            std::max(1.0, .22 * attached.radius),
            380 + (200 * draw01()),
            20 + (40 * draw01()));
    }

    void BubbleMotion::migrate(double elapsed, Vec2 acceleration) {
        const double TURN = turning(m_velocity, m_speed, acceleration);
        const double SLOW = m_speed < 80 ? 1 : 0;
        std::size_t kept = 0;
        // NOLINTNEXTLINE(misc-const-correctness): The cursor writes retained bubbles below.
        auto *destination = m_attached.begin();
        for (const auto &current : std::span(m_attached).first(m_attached_count)) {
            auto attached = current;
            attached.age += elapsed;
            const auto OUTLINE = outline(attached.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back; sharp corners trap it.
            const Vec2 FORCE = sub(mul(acceleration, -.05), mul(m_velocity, 1.1));
            const double TRAP = 3.2 + (40 * std::min(1.0, OUTLINE.curvature * attached.radius));
            attached.slip += dot(FORCE, OUTLINE.tangent) / std::max(OUTLINE.stretch, 1.0) * elapsed;
            attached.slip = std::clamp(attached.slip * std::exp(-TRAP * elapsed), -3.0, 3.0);
            attached.angle += attached.slip * elapsed;

            const double SLIDING = std::clamp(attached.slip * OUTLINE.stretch / 900, -.1, .1);
            const Vec2 TARGET =
                add(mul(mode(OUTLINE.tangent), std::abs(SLIDING)), mul(mode(OUTLINE.normal), -.05));
            const double FREQUENCY =
                TAU * std::clamp(12 * std::sqrt(8 / std::max(attached.radius, 1.0)), 6.0, 22.0);
            ring(attached.squash, attached.squash_rate, TARGET, FREQUENCY, .3, elapsed);
            bool gone = false;
            switch (attached.stage) {
                case Stage::Bud:
                    attached.growth = std::min(1.0, attached.age / attached.grow);
                    if (attached.growth >= 1) {
                        attached.stage = Stage::Ride;
                    }
                    break;
                case Stage::Ride: {
                    attached.patience -= elapsed * (SLOW != 0 ? 2.5 : .5);
                    if (attached.patience <= 0) {
                        attached.stage = attached.pops ? Stage::Pop : Stage::Merge;
                        break;
                    }
                    const double INSTABILITY = (m_speed / 700 * std::sqrt(attached.radius / 8) *
                                                   (.25 + exposure(attached.angle))) +
                                               (length(acceleration) / 9000) + (.5 * TURN);
                    if (INSTABILITY > attached.hold &&
                        draw01() < 9 * (INSTABILITY - attached.hold) * elapsed) {
                        attached.stage = Stage::Pinch;
                    }
                    break;
                }
                case Stage::Pinch:
                    attached.pinch =
                        std::min(1.0, attached.pinch + (attached.pinch_rate * elapsed));
                    if (attached.pinch >= 1) {
                        release(attached);
                        gone = true;
                    }
                    break;
                case Stage::Merge:
                    attached.growth -= elapsed / .28;
                    gone = attached.growth <= 0;
                    break;
                case Stage::Pop:
                    attached.collapse += elapsed / .09;
                    gone = attached.collapse >= 1;
                    break;
            }
            if (!gone) {
                *destination++ = attached;
                ++kept;
            }
        }
        m_attached_count = kept;
        // Neighbours push apart; formed bubbles can coalesce.
        const auto ACTIVE = std::span(m_attached).first(m_attached_count);
        for (auto first = ACTIVE.begin(); first != ACTIVE.end(); ++first) {
            auto &first_bubble = *first;
            if (first_bubble.stage != Stage::Ride && first_bubble.stage != Stage::Bud) {
                continue;
            }
            for (auto second = first + 1; second != ACTIVE.end(); ++second) {
                auto &second_bubble = *second;
                if (second_bubble.stage != Stage::Ride && second_bubble.stage != Stage::Bud) {
                    continue;
                }
                const double GAP = std::remainder(second_bubble.angle - first_bubble.angle, TAU);
                const double STRETCH = std::max(outline(first_bubble.angle).stretch, 1.0);
                const double FIRST_RADIUS =
                    first_bubble.radius * (.35 + (.65 * first_bubble.growth));
                const double SECOND_RADIUS =
                    second_bubble.radius * (.35 + (.65 * second_bubble.growth));
                const double OVERLAP =
                    (.95 * (FIRST_RADIUS + SECOND_RADIUS)) - (std::abs(GAP) * STRETCH);
                if (OVERLAP <= 0) {
                    continue;
                }
                if (first_bubble.stage == Stage::Ride && second_bubble.stage == Stage::Ride &&
                    draw01() < 4 * elapsed * OVERLAP / (FIRST_RADIUS + SECOND_RADIUS)) {
                    auto &big =
                        first_bubble.radius >= second_bubble.radius ? first_bubble : second_bubble;
                    auto &small = &big == &first_bubble ? second_bubble : first_bubble;
                    big.radius = std::max(big.radius,
                        std::min(40.0,
                            std::cbrt((big.radius * big.radius * big.radius) +
                                      (small.radius * small.radius * small.radius))));
                    big.squash_rate =
                        add(big.squash_rate, mul(mode(outline(big.angle).tangent), 3));
                    small.stage = Stage::Merge;
                    continue;
                }
                const double PUSH = 6 * OVERLAP / STRETCH * elapsed;
                first_bubble.slip -= std::copysign(PUSH, GAP);
                second_bubble.slip += std::copysign(PUSH, GAP);
            }
        }
    }

    bool BubbleMotion::start_strand(
        double angle, double half_width, double thickness, double reach) {
        for (auto &strand : m_strands) {
            if (strand.live) {
                continue;
            }
            strand = {};
            strand.id = m_next_strand++;
            strand.angle = angle;
            strand.half_width = half_width;
            strand.thickness = thickness;
            strand.reach = reach;
            strand.live = true;
            strand.thread = half_width < 6;
            return true;
        }
        return false;
    }

    void BubbleMotion::emit_trail(double elapsed, Vec2 acceleration) {
        const bool DRAWING = m_params.trail > 0 && m_params.strength > 0;
        const double TRAVEL = std::clamp((m_speed - TRAIL_SPEED) / 1100, 0.0, 1.5);
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double TURN = turning(m_velocity, m_speed, acceleration);
        const double DENSITY =
            std::clamp(2 * m_params.trail *
                           std::pow(std::clamp((m_speed - TRAIL_SPEED) / 2200, 0.0, 1.0), .8) *
                           (.7 + (.5 * STRETCH) + (.6 * TURN)),
                0.0,
                1.0);
        if (DRAWING && TRAVEL > 0) {
            const bool SHEET = std::ranges::any_of(
                m_strands, [](const Strand &strand) { return strand.live && !strand.thread; });
            if (!SHEET && DENSITY > .04 && draw01() < 12 * elapsed) {
                const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
                const Vec2 EXTENT =
                    bubble_round_half({.x = m_size.x * .5, .y = m_size.y * .5}, m_shape);
                const double ACROSS =
                    std::abs(EXTENT.x * DIRECTION.y) + std::abs(EXTENT.y * DIRECTION.x);
                start_strand(trailing_angle(),
                    std::max(6.0, ACROSS * (.03 + (.07 * DENSITY)) * (.7 + (.6 * draw01()))),
                    480 + (380 * DENSITY),
                    (260 + (420 * draw01())) * (.6 + (.8 * DENSITY)));
            }
            m_thread_hazard += m_params.trail * 2.4 * TRAVEL * (1 + TURN) * elapsed;
            if (draw01() < m_thread_hazard * elapsed * 8) {
                m_thread_hazard = 0;
                const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
                const Vec2 UNIT{.x = DIRECTION.x / std::max(m_body.w, 1.0),
                    .y = DIRECTION.y / std::max(m_body.h, 1.0)};
                const double SIGN_X = UNIT.x >= 0 ? 1 : -1;
                const double SIGN_Y = UNIT.y >= 0 ? 1 : -1;
                const bool FLIP = std::abs(UNIT.x) > std::abs(UNIT.y);
                const double PICK = draw01() < .5 ? 1 : -1;
                const double CORNER_X = FLIP ? SIGN_X : PICK;
                const double CORNER_Y = FLIP ? PICK : SIGN_Y;
                start_strand(std::atan2(CORNER_Y, CORNER_X),
                    1.2 + (2.5 * draw01()),
                    350 + (400 * draw01()),
                    50 + (200 * draw01()));
            }
        }
        const double TRAILING = trailing_angle();
        for (auto &strand : m_strands) {
            if (!strand.live) {
                continue;
            }
            if (!strand.thread) {
                strand.angle +=
                    std::remainder(TRAILING - strand.angle, TAU) * (1 - std::exp(-elapsed / .15));
            }
            const auto OUTLINE = outline(strand.angle);
            const double MOVED = m_speed * elapsed;
            strand.travel += MOVED;
            strand.length += MOVED;
            strand.slack = m_speed < .6 * TRAIL_SPEED ? strand.slack + elapsed : 0;
            const bool LAST = !DRAWING || strand.length >= strand.reach || strand.slack > .08 ||
                              dot(OUTLINE.normal, m_behind) < .1;
            const double SPACING = std::clamp(m_speed * .012, 6.0, 14.0);
            const bool FIXED = strand.travel >= SPACING || LAST;
            if (FIXED) {
                strand.travel = 0;
            }
            if (m_trail_count < m_trail.size()) {
                BubbleTrailPoint point;
                point.position =
                    sub(OUTLINE.point, mul(OUTLINE.normal, std::min(2.0, strand.half_width)));
                point.velocity = mul(m_velocity, FILM_INHERIT);
                point.half_width =
                    strand.half_width * (1 - ((strand.thread ? .2 : .45) *
                                                 std::min(1.0, strand.length / strand.reach)));
                point.thickness = strand.thickness;
                point.strand = strand.id;
                point.provisional = !FIXED;
                point.last = LAST;
                m_trail[m_trail_count++] = point;
            }
            if (LAST) {
                strand.live = false;
            }
        }
    }

    std::span<const BubbleTrailPoint> BubbleMotion::trail() const noexcept {
        return std::span(m_trail).first(m_trail_count);
    }

    std::span<const BubbleRelease> BubbleMotion::releases() const noexcept {
        return std::span(m_releases).first(m_release_count);
    }

    void BubbleMotion::clear_emissions() noexcept {
        m_trail_count = m_release_count = 0;
    }

}  // namespace hyprimate
