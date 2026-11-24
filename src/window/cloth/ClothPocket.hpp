#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "window/shared/SheetFrame.hpp"

namespace hyprimate {
    // One-way coupled sleeve: the cloth follows these samples, but does not push them.
    class ClothPocket {
      public:
        using Samples = std::array<float, SHEET_CELLS + 1>;

        ClothPocket(const Samples &positions, const Samples &velocities) noexcept
            : m_positions(positions), m_velocities(velocities) {
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                const float START = m_positions[i] = std::clamp(m_positions[i], -LIMIT, LIMIT);
                m_rail[i] = {START, 0, -LIMIT - START, LIMIT - START};
                if (i != 0) {
                    const float PREVIOUS = m_positions[i - 1];
                    m_links[i - 1] = {(PREVIOUS + MIN_SPACING) - PREVIOUS, 0, 0, 1};
                }
            }
        }

        [[nodiscard]] float position(int column) const noexcept {
            return m_positions[static_cast<std::size_t>(column)];
        }

        // Returns false if either the input or the resulting state is not finite.
        bool update(
            double acceleration, double rod_velocity, double quiet, double springiness) noexcept {
            if (!std::isfinite(acceleration) || !std::isfinite(rod_velocity) ||
                !std::isfinite(quiet) || !std::isfinite(springiness)) {
                return false;
            }
            if (!apply_forces(acceleration, rod_velocity, quiet, springiness)) {
                return false;
            }
            constrain_velocities();
            integrate();
            constrain_positions();
            return finite_state();
        }

      private:
        static constexpr float STEP = 1.0F / 120.0F;
        static constexpr float SPRING = 48.0F, DAMPING = 4.0F;
        static constexpr float LIMIT = .498F, MIN_SPACING = .004F;
        static constexpr float MAX_SPEED = 8.0F;
        static constexpr int ITERATIONS = 64;

        bool apply_forces(
            double acceleration, double rod_velocity, double quiet, double springiness) noexcept {
            const float STRENGTH = static_cast<float>(springiness);
            const float DIRECTION = static_cast<float>(std::clamp(rod_velocity / .05, -1.0, 1.0));
            const float TRAILING = .5F * std::abs(DIRECTION);
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                const float REST = static_cast<float>((static_cast<double>(i) / SHEET_CELLS) - .5);
                const float EFFECTIVE_SPRING =
                    SPRING * (4 - (3 * STRENGTH)) * (1 + (3 * (TRAILING - (DIRECTION * REST))));
                const float EFFECTIVE_DAMPING = DAMPING * std::sqrt(EFFECTIVE_SPRING / SPRING);
                const float FORCE = static_cast<float>(
                    (-acceleration * STRENGTH) +
                    ((EFFECTIVE_SPRING + (quiet * 8)) * (REST - m_positions[i])) -
                    (EFFECTIVE_DAMPING * m_velocities[i]));
                const float VELOCITY = m_velocities[i] + (FORCE * STEP);
                if (!std::isfinite(m_positions[i]) || !std::isfinite(VELOCITY)) {
                    return false;
                }
                m_velocities[i] = std::clamp(VELOCITY, -MAX_SPEED, MAX_SPEED);
            }
            return true;
        }

        struct Limit {
            float anchor_left, anchor_right, lower, upper;
            float impulse{};
            int direction{};

            [[nodiscard]] float separation(float left, float right) const noexcept {
                return (right - left) + (anchor_right - anchor_left);
            }

            void prepare(float left,
                float right,
                float &left_speed,
                float &right_speed,
                float inverse_left) noexcept {
                const float DISTANCE = separation(left, right);
                direction = DISTANCE <= lower ? 1 : DISTANCE >= upper ? -1 : 0;
                if (direction == 0) {
                    impulse = 0;
                } else {
                    left_speed -= inverse_left * impulse;
                    right_speed += impulse;
                }
            }

            bool apply(float &left_speed, float &right_speed, float inverse_left) noexcept {
                if (direction == 0) {
                    return false;
                }
                const float REQUEST = (left_speed - right_speed) * (inverse_left == 0 ? 1 : .5F);
                const float TOTAL = direction > 0 ? std::max(0.0F, impulse + REQUEST)
                                                  : std::min(0.0F, impulse + REQUEST);
                const float CHANGE = TOTAL - impulse;
                impulse = TOTAL;
                left_speed -= inverse_left * CHANGE;
                right_speed += CHANGE;
                return CHANGE != 0;
            }

            bool project(float &left, float &right, float inverse_left) const noexcept {
                const float DISTANCE = separation(left, right);
                const float RESIDUAL = DISTANCE - std::clamp(DISTANCE, lower, upper);
                const float CHANGE = -RESIDUAL * (inverse_left == 0 ? 1 : .5F);
                left -= inverse_left * CHANGE;
                right += CHANGE;
                return CHANGE != 0;
            }
        };

        // The 57-column sleeve is small and strictly one-dimensional. Keep the fixed
        // rail/link sequence to preserve the direction of compression under a reversal.
        void constrain_velocities() noexcept {
            float fixed_speed = 0;
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                m_rail[i].prepare(0, m_positions[i], fixed_speed, m_velocities[i], 0);
                if (i != 0) {
                    m_links[i - 1].prepare(m_positions[i - 1],
                        m_positions[i],
                        m_velocities[i - 1],
                        m_velocities[i],
                        1);
                }
            }
            for (int iteration = 0; iteration < ITERATIONS; ++iteration) {
                bool changed = false;
                for (std::size_t i = 0; i < m_positions.size(); ++i) {
                    changed |= m_rail[i].apply(fixed_speed, m_velocities[i], 0);
                    if (i != 0) {
                        changed |= m_links[i - 1].apply(m_velocities[i - 1], m_velocities[i], 1);
                    }
                }
                if (!changed) {
                    break;
                }
            }
        }

        void integrate() noexcept {
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                m_positions[i] += m_velocities[i] * STEP;
            }
        }

        void constrain_positions() noexcept {
            float fixed_position = 0;
            for (int iteration = 0; iteration < ITERATIONS; ++iteration) {
                bool changed = false;
                for (std::size_t i = 0; i < m_positions.size(); ++i) {
                    changed |= m_rail[i].project(fixed_position, m_positions[i], 0);
                    if (i != 0) {
                        changed |= m_links[i - 1].project(m_positions[i - 1], m_positions[i], 1);
                    }
                }
                if (!changed) {
                    break;
                }
            }
        }

        [[nodiscard]] bool finite_state() const noexcept {
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                if (!std::isfinite(m_positions[i]) || !std::isfinite(m_velocities[i])) {
                    return false;
                }
            }
            return true;
        }

        Samples m_positions{};
        Samples m_velocities{};
        std::array<Limit, SHEET_CELLS + 1> m_rail{};
        std::array<Limit, SHEET_CELLS> m_links{};
    };
}  // namespace hyprimate
