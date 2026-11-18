#pragma once

#include <functional>

namespace hyprimate::workspace {

    enum class Direction { Forward, Backward };

    // Position is the physical surface coordinate; retargeting preserves its velocity.
    class Transition {
      public:
        using Curve = std::function<double(double)>;
        bool begin(Direction direction, double duration_seconds,
            Curve curve = [](double t) { return t * t * (3.0 - 2.0 * t); });
        void retarget(bool destination, double duration_seconds);
        void advance(double elapsed_seconds);
        void cancel();

        [[nodiscard]] bool active() const;
        [[nodiscard]] double progress() const;
        [[nodiscard]] double velocity() const;
        [[nodiscard]] bool targetDestination() const;
        [[nodiscard]] Direction direction() const;

      private:
        Direction m_direction = Direction::Forward;
        double m_duration = 0.0;
        double m_elapsed = 0.0;
        double m_start = 0.0;
        double m_velocityCorrection = 0.0;
        double m_target = 1.0;
        double m_position = 0.0;
        double m_velocity = 0.0;
        Curve m_curve;
        bool m_active = false;
    };

}  // namespace hyprimate::workspace
