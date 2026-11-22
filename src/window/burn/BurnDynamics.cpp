#include <algorithm>
#include <cmath>
#include <numbers>

#include "window/burn/BurnDynamics.hpp"

namespace hyprimate {
    namespace {
        constexpr double PI = std::numbers::pi;
        BurnParams sanitized(BurnParams params) noexcept {
            if (!std::isfinite(params.duration) || params.duration <= 0) params.duration = 1.5;
            params.variation = std::clamp(params.variation, 0., 1.);
            params.ember_density = std::clamp(params.ember_density, 0., 2.);
            params.fragment_density = std::clamp(params.fragment_density, 0., 2.);
            return params;
        }
    }  // namespace
    double burn_arrival(Vec2 uv, std::uint64_t seed, double variation) noexcept {
        const double X = 2 * uv.x - 1, Y = 2 * uv.y - 1;
        const double R = std::max(std::abs(X), std::abs(Y));
        const double S = burn_phase(seed), V = std::clamp(variation, 0., 1.);
        const double ANGLE = std::atan2(Y, X);
        const double A = ANGLE + .19 * std::sin(3 * ANGLE + S + 1.3 * R);
        const double GRAIN = .5 + .19 * std::sin(3 * A + S) + .14 * std::sin(7 * A - S) +
                             .10 * std::sin(17 * A + 2 * S + 4 * R) +
                             .05 * std::sin(41 * A + S + 6 * R) + .02 * std::sin(89 * A - S);
        const double RADIUS = std::lerp(R, std::hypot(X, Y) * .70710678, .82 * V);
        // Isolated ignition sites touch 1 at the perimeter, so the fire starts
        // immediately even where the rounded interior has a short travel time.
        const double SITE = .5 + .5 * std::cos(5 * ANGLE + S + .7 * std::sin(3 * ANGLE - S));
        const double SITE2 = SITE * SITE, SITE4 = SITE2 * SITE2;
        return std::lerp(RADIUS * (1 - .43 * V * GRAIN), R, smoothstep(.65, 1., R) * SITE4 * SITE4);
    }
    void BurnSimulation::open(Box capture, const BurnParams &params, std::uint64_t seed) noexcept {
        reset();
        if (!is_usable(capture)) return;
        m_params = sanitized(params);
        m_front = {.capture = capture, .progress = 0, .closing = false, .seed = seed};
        m_rng = seed;
        m_initialized = true;
    }
    void BurnSimulation::close(Box capture, const BurnParams &params, std::uint64_t seed) noexcept {
        if (!is_usable(capture)) return;
        // Do not discard detached material or replace a partially revealed opening.
        if (!m_initialized) {
            m_front = {.capture = capture, .progress = 1, .closing = true, .seed = seed};
            m_rng = seed;
            m_initialized = true;
        }
        m_front.capture = capture;
        m_front.closing = true;
        m_params = sanitized(params);
    }
    void BurnSimulation::reset() noexcept {
        m_fragment_count = m_particle_count = 0;
        m_emission_credit = m_fragment_credit = 0;
        m_front = {};
        m_initialized = false;
    }
    Vec2 BurnSimulation::acceleration(bool ember) const noexcept {
        const double ANGLE = m_params.wind_direction * PI / 180;
        const double WIND_X = std::cos(ANGLE) * m_params.wind_strength;
        const double WIND_Y = std::sin(ANGLE) * m_params.wind_strength;
        switch (m_params.direction) {
            case BurnDirection::Fall:
                return {WIND_X * .12, std::abs(m_params.gravity_strength)};
            case BurnDirection::Rise:
                return {WIND_X * .12, -std::abs(m_params.gravity_strength)};
            case BurnDirection::Wind:
                return {WIND_X * 2, WIND_Y * 2};
            case BurnDirection::Natural:
                return {WIND_X * .35,
                    WIND_Y * .35 - std::abs(m_params.gravity_strength) * (ember ? .5 : .18)};
        }
        return {};
    }
    void BurnSimulation::fragment(Vec2 uv, Vec2 size, unsigned layer) noexcept {
        if (m_fragment_count == MAX_FRAGMENTS) return;
        auto &f = m_fragments[m_fragment_count++];
        const double ANGLE = unit_draw(m_rng) * 2 * PI;
        const double SPEED = 15 + unit_draw(m_rng) * 65;
        const double WIND = m_params.direction == BurnDirection::Wind ? 1. : .2;
        const double WIND_ANGLE = m_params.wind_direction * PI / 180;
        f = {.position = {m_front.capture.x + uv.x * m_front.capture.w,
                 m_front.capture.y + uv.y * m_front.capture.h},
            .size = size,
            .uv = {uv.x - size.x / m_front.capture.w * .5,
                uv.y - size.y / m_front.capture.h * .5,
                size.x / m_front.capture.w,
                size.y / m_front.capture.h},
            .rotation = {},
            .heat = 1,
            .opacity = 1,
            .layer = layer,
            .velocity = {std::cos(ANGLE) * SPEED +
                             std::cos(WIND_ANGLE) * m_params.wind_strength * WIND,
                std::sin(ANGLE) * SPEED + std::sin(WIND_ANGLE) * m_params.wind_strength * WIND},
            .spin = {(unit_draw(m_rng) - .5) * 8,
                (unit_draw(m_rng) - .5) * 8,
                (unit_draw(m_rng) - .5) * 5},
            .age = 0,
            .lifetime = .65 + unit_draw(m_rng) * .65};
        // A source footprint never samples outside the retained image.
        f.uv.x = std::clamp(f.uv.x, 0., 1. - f.uv.w);
        f.uv.y = std::clamp(f.uv.y, 0., 1. - f.uv.h);
    }
    void BurnSimulation::particle(Vec2 position, bool ember) noexcept {
        if (m_particle_count == MAX_PARTICLES) return;
        const double ANGLE = unit_draw(m_rng) * 2 * PI;
        const double SPEED = 10 + unit_draw(m_rng) * 50;
        m_particles[m_particle_count++] = {.position = position,
            .size = ember ? 1 + unit_draw(m_rng) * 2.5 : 2 + unit_draw(m_rng) * 4,
            .heat = ember ? 1. : .15,
            .opacity = 1,
            .ember = ember,
            .velocity = {std::cos(ANGLE) * SPEED, std::sin(ANGLE) * SPEED},
            .age = 0,
            .lifetime = .35 + unit_draw(m_rng) * (ember ? .9 : 1.2)};
    }
    void BurnSimulation::emit(double previous) noexcept {
        const double AREA = std::min(1600000., m_front.capture.w * m_front.capture.h);
        const double SWEPT =
            std::abs(previous * previous - m_front.progress * m_front.progress) * AREA;
        m_emission_credit += SWEPT * m_params.ember_density / 1100;
        if (m_front.closing)
            m_fragment_credit +=
                SWEPT * m_params.fragment_density / (m_params.consume ? 14000 : 6000);
        // At most a bounded number of events per step even with extreme geometry.
        const auto PARTICLES = static_cast<unsigned>(std::min(32., std::floor(m_emission_credit)));
        const auto FRAGMENTS = static_cast<unsigned>(std::min(8., std::floor(m_fragment_credit)));
        m_emission_credit -= PARTICLES;
        m_fragment_credit -= FRAGMENTS;
        for (unsigned i = 0; i < PARTICLES + FRAGMENTS; ++i) {
            const double A = unit_draw(m_rng) * 2 * PI;
            const double DX = std::cos(A), DY = std::sin(A);
            const double SCALE = std::max(std::abs(DX), std::abs(DY));
            const double TARGET = previous + (m_front.progress - previous) * unit_draw(m_rng);
            if (TARGET > burn_arrival({.5 + .5 * DX / SCALE, .5 + .5 * DY / SCALE},
                             m_front.seed,
                             m_params.variation))
                continue;
            double low = 0, high = 1;
            for (unsigned j = 0; j < 10; ++j) {
                const double R = (low + high) * .5;
                if (burn_arrival({.5 + .5 * DX / SCALE * R, .5 + .5 * DY / SCALE * R},
                        m_front.seed,
                        m_params.variation) < TARGET)
                    low = R;
                else
                    high = R;
            }
            const double R = (low + high) * .5;
            const Vec2 UV{.5 + .5 * DX / SCALE * R, .5 + .5 * DY / SCALE * R};
            if (i < FRAGMENTS) {
                const double WEIGHT = unit_draw(m_rng);
                const double SIZE =
                    (m_params.consume ? 4 + WEIGHT * 7 : 12 + WEIGHT * WEIGHT * 55) *
                    (.6 + m_params.variation * .8);
                fragment(UV,
                    {std::min(SIZE, m_front.capture.w * .2),
                        std::min(SIZE * .7, m_front.capture.h * .2)});
            } else
                particle({m_front.capture.x + UV.x * m_front.capture.w,
                             m_front.capture.y + UV.y * m_front.capture.h},
                    unit_draw(m_rng) < .65);
        }
    }
    void BurnSimulation::integrate(double dt) noexcept {
        const Vec2 FORCE = acceleration(false), EMBER_FORCE = acceleration(true);
        for (std::size_t i = 0; i < m_fragment_count;) {
            auto &f = m_fragments[i];
            const double OLD_AGE = f.age;
            f.age += dt;
            if (f.age >= f.lifetime) {
                f = m_fragments[--m_fragment_count];
                continue;
            }
            const double RESPONSE = std::clamp(20. / std::sqrt(f.size.x * f.size.y), .5, 3.);
            const double VERTICAL_FORCE = m_params.direction == BurnDirection::Natural
                                              ? std::abs(m_params.gravity_strength) * .7
                                              : FORCE.y;
            const double FLUTTER =
                std::sin(f.age * 23 + f.uv.x * 41) * 50 * m_params.curl * RESPONSE;
            f.velocity.x += (FORCE.x * RESPONSE + FLUTTER - f.velocity.x * .45) * dt;
            f.velocity.y += (VERTICAL_FORCE * RESPONSE - f.velocity.y * .3) * dt;
            f.position.x += f.velocity.x * dt;
            f.position.y += f.velocity.y * dt;
            f.rotation.x += (f.spin.x + std::sin(f.age * 17) * m_params.curl) * dt;
            f.rotation.y += f.spin.y * dt;
            f.rotation.z += f.spin.z * dt;
            f.heat = std::max(0., 1 - f.age / (f.lifetime * .65));
            f.opacity = 1 - smoothstep(.55, 1., f.age / f.lifetime);
            if (OLD_AGE < f.lifetime * .68 && f.age >= f.lifetime * .68 &&
                m_particle_count < MAX_PARTICLES) {
                // Erode at the fragment's current position, not back at the window.
                particle(f.position, false);
                auto &ash = m_particles[m_particle_count - 1];
                ash.velocity.x += f.velocity.x * .7;
                ash.velocity.y += f.velocity.y * .7;
                ash.lifetime = std::min(ash.lifetime, .65);
            }
            if (f.layer == 0 && f.size.x > 15 && OLD_AGE < f.lifetime * .4 &&
                f.age >= f.lifetime * .4 && m_fragment_count + 3 <= MAX_FRAGMENTS) {
                const auto PARENT = f;
                for (unsigned child = 0; child < 4; ++child) {
                    auto &piece = child == 0 ? f : m_fragments[m_fragment_count++];
                    piece = PARENT;
                    piece.layer = 1;
                    piece.size = {PARENT.size.x * .5, PARENT.size.y * .5};
                    piece.uv = {PARENT.uv.x + (child % 2) * PARENT.uv.w * .5,
                        PARENT.uv.y + (child / 2) * PARENT.uv.h * .5,
                        PARENT.uv.w * .5,
                        PARENT.uv.h * .5};
                    const double X = (child % 2 ? 1. : -1.), Y = (child / 2 ? 1. : -1.);
                    piece.velocity.x += X * 15;
                    piece.velocity.y += Y * 15;
                    piece.spin.x += X * 2;
                    piece.spin.y += Y * 2;
                    const double LOCAL_X = X * PARENT.size.x * .25,
                                 LOCAL_Y = Y * PARENT.size.y * .25;
                    const double AFTER_X_Y = LOCAL_Y * std::cos(PARENT.rotation.x);
                    const double AFTER_X_Z = LOCAL_Y * std::sin(PARENT.rotation.x);
                    const double AFTER_Y_X = LOCAL_X * std::cos(PARENT.rotation.y) +
                                             AFTER_X_Z * std::sin(PARENT.rotation.y);
                    piece.position.x += AFTER_Y_X * std::cos(PARENT.rotation.z) -
                                        AFTER_X_Y * std::sin(PARENT.rotation.z);
                    piece.position.y += AFTER_Y_X * std::sin(PARENT.rotation.z) +
                                        AFTER_X_Y * std::cos(PARENT.rotation.z);
                    piece.depth += -LOCAL_X * std::sin(PARENT.rotation.y) +
                                   AFTER_X_Z * std::cos(PARENT.rotation.y);
                    piece.lifetime =
                        piece.age + (PARENT.lifetime - piece.age) * (.7 + .6 * unit_draw(m_rng));
                }
            }
            ++i;
        }
        for (std::size_t i = 0; i < m_particle_count;) {
            auto &p = m_particles[i];
            p.age += dt;
            if (p.age >= p.lifetime) {
                p = m_particles[--m_particle_count];
                continue;
            }
            const auto A = p.ember ? EMBER_FORCE : FORCE;
            const double RESPONSE = std::clamp(3. / p.size, .7, 2.);
            const double EDDY = std::sin(p.age * 9 + p.lifetime * 17 + p.size * 3) * 38;
            p.velocity.x += ((A.x + EDDY) * RESPONSE - p.velocity.x * .6) * dt;
            p.velocity.y += (A.y * RESPONSE - p.velocity.y * .6) * dt;
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.heat = p.ember ? std::max(0., 1 - p.age / p.lifetime) : .1;
            p.opacity = 1 - smoothstep(.2, 1., p.age / p.lifetime);
            ++i;
        }
    }
    void BurnSimulation::advance(double elapsed, Box capture) noexcept {
        if (!m_initialized || !std::isfinite(elapsed) || elapsed <= 0) return;
        if (!m_front.closing && is_usable(capture)) m_front.capture = capture;
        double remaining = std::min(elapsed, STALL_SECONDS);
        while (remaining > 1e-9) {
            const double DT = std::min(remaining, SUBSTEP_SECONDS);
            const double PREVIOUS = m_front.progress;
            m_front.progress =
                std::clamp(PREVIOUS + (m_front.closing ? -DT : DT) / m_params.duration, 0., 1.);
            integrate(DT);
            if (PREVIOUS != m_front.progress) emit(PREVIOUS);
            remaining -= DT;
        }
    }
    bool BurnSimulation::active() const noexcept {
        return m_initialized && ((m_front.closing ? m_front.progress > 0 : m_front.progress < 1) ||
                                    m_fragment_count || m_particle_count);
    }
    Box BurnSimulation::bounds() const noexcept {
        if (!m_initialized) return {};
        double left = m_front.capture.x, top = m_front.capture.y;
        double right = left + m_front.capture.w, bottom = top + m_front.capture.h;
        const auto include = [&](Vec2 p, double radius) {
            left = std::min(left, p.x - radius);
            top = std::min(top, p.y - radius);
            right = std::max(right, p.x + radius);
            bottom = std::max(bottom, p.y + radius);
        };
        for (const auto &f : fragments())
            include(f.position, std::hypot(f.size.x, f.size.y) * .5 + 2);
        for (const auto &p : particles()) include(p.position, p.size + 2);
        const double PAD = 8 + m_params.curl * 24;
        return {left - PAD, top - PAD, right - left + PAD * 2, bottom - top + PAD * 2};
    }
}  // namespace hyprimate
