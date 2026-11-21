#include <algorithm>
#include <cmath>
#include <utility>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {

    bool Transition::begin(Direction direction, double duration_seconds, Curve curve) {
        auto selected = std::isfinite(duration_seconds) && duration_seconds > 0.0 && curve
            ? std::make_shared<const Curve>(std::move(curve)) : nullptr;
        m_direction = direction;
        m_position = m_start = 0.0;
        m_velocity = m_velocity_correction = 0.0;
        m_target = 1.0;
        m_elapsed = 0.0;
        m_duration = duration_seconds;
        m_curve = std::move(selected);
        m_active = static_cast<bool>(m_curve);
        if (!m_active) {
            m_position = 1.0;
        }
        return m_active;
    }

    void Transition::retarget(bool destination, double duration_seconds) {
        if (!m_active || !std::isfinite(duration_seconds) || duration_seconds <= 0.0) {
            return;
        }
        m_target = destination ? 1.0 : 0.0;
        m_start = m_position;
        m_elapsed = 0.0;
        m_duration = duration_seconds;
        // The correction has zero displacement at both ends and matches the incoming
        // physical velocity at the interruption. It vanishes for a fresh transition.
        constexpr double STEP = 0.0001;
        const double SLOPE = ((*m_curve)(STEP) - (*m_curve)(0.0)) / STEP;
        m_velocity_correction = (m_velocity * m_duration) - ((m_target - m_start) * SLOPE);
    }

    void Transition::advance(double elapsed_seconds) {
        if (!m_active) {
            return;
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
            m_position = m_target;
            m_velocity = 0.0;
            m_active = false;
            return;
        }
        m_elapsed = std::min(m_duration, m_elapsed + elapsed_seconds);
        const double TIME = m_elapsed / m_duration;
        constexpr double STEP = 0.0001;
        const double LOWER = std::max(0.0, TIME - STEP);
        const double UPPER = std::min(1.0, TIME + STEP);
        const double VALUE = (*m_curve)(TIME);
        const double SLOPE = UPPER > LOWER ? ((*m_curve)(UPPER) - (*m_curve)(LOWER)) / (UPPER - LOWER) : 0.0;
        const double REMAINING = 1.0 - TIME;
        const double RAW = m_start + ((m_target - m_start) * VALUE) +
                           (m_velocity_correction * TIME * REMAINING * REMAINING);
        // The wheel renderer is defined only on [0,1]. Native overshoot is clipped.
        m_position = std::clamp(RAW, 0.0, 1.0);
        m_velocity = (RAW < 0.0 || RAW > 1.0) ? 0.0 :
            (((m_target - m_start) * SLOPE) + (m_velocity_correction * REMAINING * (1.0 - (3.0 * TIME)))) / m_duration;
        if (m_elapsed >= m_duration) {
            m_position = m_target;
            m_velocity = 0.0;
            m_active = false;
        }
    }

    void Transition::cancel() {
        m_active = false;
        m_position = m_target;
        m_velocity = 0.0;
    }

    bool Transition::active() const {
        return m_active;
    }

    double Transition::progress() const {
        return m_position;
    }

    double Transition::velocity() const {
        return m_velocity;
    }

    bool Transition::target_destination() const {
        return m_target == 1.0;
    }

    Direction Transition::direction() const {
        return m_direction;
    }

}  // namespace hyprimate::workspace
