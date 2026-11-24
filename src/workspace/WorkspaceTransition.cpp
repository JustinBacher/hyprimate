#include <algorithm>
#include <cmath>
#include <utility>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {

    namespace {
        // Braking can spend half of the last representable gap. Rounding that
        // displacement must not briefly show the rejected native endpoint.
        double brake_position(double start, double displacement) {
            const double position = start + displacement;
            if (start > 0.0 && start < 1.0 && (position == 0.0 || position == 1.0)) {
                return std::nextafter(position, start);
            }
            return position;
        }

        // Retain the selected curve through most of the return. Ease only its
        // time input at the joins, so braking and landing both meet at rest.
        double soften_ends(double time) {
            constexpr double EDGE = 0.125;
            const auto EASE_EDGE = [](double unit) { return unit * unit * (2.0 - unit); };
            if (time < EDGE) {
                return EDGE * EASE_EDGE(time / EDGE);
            }
            if (time > 1.0 - EDGE) {
                return 1.0 - (EDGE * EASE_EDGE((1.0 - time) / EDGE));
            }
            return time;
        }

        bool bounded_curve(const Transition::Curve &curve) {
            // A Bezier that overshoots on departure must not hit a native endpoint
            // early on a return. Springs already expose only their rising lobe.
            constexpr int SAMPLES = 64;
            constexpr double TOLERANCE = 1e-9;
            double previous = curve(0.0);
            if (!std::isfinite(previous) || std::abs(previous) > TOLERANCE) {
                return false;
            }
            for (int i = 1; i <= SAMPLES; ++i) {
                const double VALUE = curve(static_cast<double>(i) / SAMPLES);
                if (!std::isfinite(VALUE) || VALUE < previous - TOLERANCE ||
                    VALUE > 1.0 + TOLERANCE) {
                    return false;
                }
                previous = VALUE;
            }
            return std::abs(previous - 1.0) <= TOLERANCE;
        }
    }  // namespace

    bool Transition::begin(Direction direction, double duration_seconds, Curve curve) {
        auto selected = std::isfinite(duration_seconds) && duration_seconds > 0.0 && curve
                            ? std::make_shared<const Curve>(std::move(curve))
                            : nullptr;
        m_direction = direction;
        m_position = m_start = 0.0;
        m_velocity = m_brake_time = m_brake_velocity = 0.0;
        m_retargeted = false;
        m_bounded_curve.reset();
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

    void Transition::retarget(bool destination,
        double nominal_seconds,
        WorkspaceMotion presented,
        WorkspaceTravel travel) {
        if (!m_curve || destination == target_destination() || !std::isfinite(nominal_seconds) ||
            nominal_seconds <= 0.0) {
            return;
        }
        m_target = destination ? 1.0 : 0.0;
        m_start = m_position = presented.position;
        m_velocity = presented.velocity;
        m_elapsed = 0.0;
        m_retargeted = true;
        const double DISTANCE =
            travel != nullptr ? travel(m_start, destination) : std::abs(m_target - m_start);
        m_duration = nominal_seconds * DISTANCE;
        m_active = m_duration > 0.0;
        m_brake_time = 0.0;
        m_brake_velocity = 0.0;
        if (!m_active) {
            m_position = m_target;
            m_velocity = 0.0;
            return;
        }
        if (!m_bounded_curve.has_value()) {
            m_bounded_curve = bounded_curve(*m_curve);
        }
        if (m_velocity != 0.0) {
            const double ROOM = m_velocity > 0.0 ? 1.0 - m_start : m_start;
            // Constant deceleration joins the return at rest. Budget at most
            // an eighth of the segment and half the available excursion.
            constexpr double BRAKE_SHARE = .125;
            m_brake_time = std::min(m_duration * BRAKE_SHARE, ROOM / std::abs(m_velocity));
            if (m_brake_time > 0.0) {
                m_brake_velocity = m_velocity;
            } else {
                m_velocity = 0.0;  // Outward velocity at a path boundary has no continuation.
            }
        }
    }

    void Transition::advance(double elapsed_seconds) {
        if (!m_active) {
            return;
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0 ||
            elapsed_seconds >= m_duration - m_elapsed) {
            m_position = m_target;
            m_velocity = 0.0;
            m_active = false;
            return;
        }
        m_elapsed += elapsed_seconds;
        if (m_elapsed < m_brake_time) {
            const double BRAKE = m_elapsed / m_brake_time;
            m_position =
                brake_position(m_start, m_brake_velocity * m_elapsed * (1.0 - (.5 * BRAKE)));
            m_velocity = m_brake_velocity * (1.0 - BRAKE);
            return;
        }
        const double START = brake_position(m_start, m_brake_velocity * .5 * m_brake_time);
        const double SPAN = m_duration - m_brake_time;
        const double TIME = (m_elapsed - m_brake_time) / SPAN;
        const auto SAMPLE = [&](double unit) {
            if (!m_retargeted) {
                return (*m_curve)(unit);
            }
            const double SOFTENED = soften_ends(unit);
            return *m_bounded_curve ? (*m_curve)(SOFTENED) : SOFTENED;
        };
        constexpr double STEP = 0.0001;
        const double LOWER = std::max(0.0, TIME - STEP);
        const double UPPER = std::min(1.0, TIME + STEP);
        const double VALUE = SAMPLE(TIME);
        const double SLOPE =
            UPPER > LOWER ? (SAMPLE(UPPER) - SAMPLE(LOWER)) / (UPPER - LOWER) : 0.0;
        const double RAW = std::lerp(START, m_target, VALUE);
        // Preserve existing native Bezier overshoot behavior on outbound turns.
        // Returns must stay between the turnaround and target, even for overshooting curves.
        m_position = m_retargeted ? std::lerp(START, m_target, std::clamp(VALUE, 0.0, 1.0))
                                  : std::clamp(RAW, 0.0, 1.0);
        m_velocity = (VALUE < 0.0 || VALUE > 1.0 || (m_retargeted && TIME == 0.0))
                         ? 0.0
                         : (m_target - START) * SLOPE / SPAN;
        if ((m_position == 0.0 && m_velocity < 0.0) || (m_position == 1.0 && m_velocity > 0.0)) {
            m_velocity = 0.0;
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
