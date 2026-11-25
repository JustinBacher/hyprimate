#pragma once

#include <functional>
#include <memory>
#include <optional>

namespace hyprimate::workspace {

    enum class Direction { Forward, Backward };

    // A frame is identified by a reversible style-path parameter, not elapsed time.
    // Velocity is its signed derivative in units per second on the original path.
    struct WorkspaceMotion {
        double position = 0.0;
        double velocity = 0.0;
    };

    // Fraction of the full style path remaining from a presented parameter to
    // the selected endpoint. Finite, in [0,1], zero only at that endpoint.
    // Used for timing, not to remap geometry or the derivative above.
    using WorkspaceTravel = double (*)(double position, bool destination);

    class Transition {
      public:
        using Curve = std::function<double(double)>;
        // A style resolver may supply a curve, defer to a known Bézier, or reject the name.
        struct CurveSelection {
            enum class Kind { Spring, Bezier, Invalid };
            Kind kind = Kind::Invalid;
            Curve curve;
        };
        bool begin(
            Direction direction, double duration_seconds, Curve curve = [](double time) {
                return time * time * (3.0 - (2.0 * time));
            });
        void retarget(bool destination,
            double nominal_seconds,
            WorkspaceMotion presented,
            WorkspaceTravel travel = nullptr);
        void advance(double elapsed_seconds);
        void cancel();

        [[nodiscard]] bool active() const;
        [[nodiscard]] double progress() const;
        [[nodiscard]] WorkspaceMotion motion() const {
            return {.position = m_position, .velocity = m_velocity};
        }
        [[nodiscard]] double velocity() const;
        [[nodiscard]] bool target_destination() const;
        [[nodiscard]] Direction direction() const;

      private:
        Direction m_direction = Direction::Forward;
        double m_duration = 0.0;
        double m_elapsed = 0.0;
        double m_start = 0.0;
        double m_brake_time = 0.0;
        double m_brake_velocity = 0.0;
        std::optional<bool> m_bounded_curve;
        bool m_retargeted = false;
        double m_target = 1.0;
        double m_position = 0.0;
        double m_velocity = 0.0;
        // A speculative sample may outlive the turn that selected this curve.
        // Copying the timeline must not copy/allocate a callable every frame.
        std::shared_ptr<const Curve> m_curve;
        bool m_active = false;
    };

}  // namespace hyprimate::workspace
