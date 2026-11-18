#include <algorithm>
#include <cmath>
#include <utility>

#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {

    bool Transition::begin(Direction direction, double duration_seconds, Curve curve) {
        m_direction = direction;
        m_position = m_start = 0.0;
        m_velocity = m_velocityCorrection = 0.0;
        m_target = 1.0;
        m_elapsed = 0.0;
        m_duration = duration_seconds;
        m_curve = std::move(curve);
        m_active = std::isfinite(duration_seconds) && duration_seconds > 0.0 && bool(m_curve);
        if (!m_active) m_position = 1.0;
        return m_active;
    }

    void Transition::retarget(bool destination, double duration_seconds) {
        if (!m_active || !std::isfinite(duration_seconds) || duration_seconds <= 0.0) return;
        m_target = destination ? 1.0 : 0.0;
        m_start = m_position;
        m_elapsed = 0.0;
        m_duration = duration_seconds;
        // The correction has zero displacement at both ends and matches the incoming
        // physical velocity at the interruption. It vanishes for a fresh transition.
        constexpr double STEP = 0.0001;
        const double slope = (m_curve(STEP) - m_curve(0.0)) / STEP;
        m_velocityCorrection = m_velocity * m_duration - (m_target - m_start) * slope;
    }

    void Transition::advance(double elapsed_seconds) {
        if (!m_active) return;
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0 ||
            elapsed_seconds > STALL_SECONDS) {
            m_position = m_target;
            m_velocity = 0.0;
            m_active = false;
            return;
        }
        m_elapsed = std::min(m_duration, m_elapsed + elapsed_seconds);
        const double t = m_elapsed / m_duration;
        constexpr double STEP = 0.0001;
        const double lo = std::max(0.0, t - STEP);
        const double hi = std::min(1.0, t + STEP);
        const double value = m_curve(t);
        const double slope = hi > lo ? (m_curve(hi) - m_curve(lo)) / (hi - lo) : 0.0;
        const double u = 1.0 - t;
        const double raw = m_start + (m_target - m_start) * value +
                           m_velocityCorrection * t * u * u;
        // The wheel renderer is defined only on [0,1]. Native overshoot is clipped.
        m_position = std::clamp(raw, 0.0, 1.0);
        m_velocity = (raw < 0.0 || raw > 1.0) ? 0.0 :
            ((m_target - m_start) * slope + m_velocityCorrection * u * (1.0 - 3.0 * t)) / m_duration;
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

    bool Transition::targetDestination() const {
        return m_target == 1.0;
    }

    Direction Transition::direction() const {
        return m_direction;
    }

}  // namespace hyprimate::workspace
