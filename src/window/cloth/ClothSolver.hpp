#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "window/shared/SheetFrame.hpp"

namespace hyprimate::cloth {
    struct Point {
        float x{}, y{}, z{};

        constexpr Point &operator+=(Point other) noexcept {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }
        constexpr Point &operator-=(Point other) noexcept {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }
        constexpr Point &operator*=(float scale) noexcept {
            x *= scale;
            y *= scale;
            z *= scale;
            return *this;
        }
        friend constexpr Point operator+(Point left, Point right) noexcept {
            return left += right;
        }
        friend constexpr Point operator-(Point left, Point right) noexcept {
            return left -= right;
        }
        friend constexpr Point operator*(Point point, float scale) noexcept {
            return point *= scale;
        }
        friend constexpr Point operator*(float scale, Point point) noexcept {
            return point *= scale;
        }
        friend constexpr Point operator/(Point point, float scale) noexcept {
            return {point.x / scale, point.y / scale, point.z / scale};
        }
    };

    constexpr float dot(Point left, Point right) noexcept {
        return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
    }
    constexpr Point cross(Point left, Point right) noexcept {
        return {
            (left.y * right.z) - (left.z * right.y),
            (left.z * right.x) - (left.x * right.z),
            (left.x * right.y) - (left.y * right.x),
        };
    }
    constexpr float lengthSquared(Point point) noexcept {
        return dot(point, point);
    }

    // All contact geometry and lengths use normalized sheet space, the same as the pose.
    struct Box {
        Point min, max;
    };

    struct Obstacle {
        Point center, half_extent;
    };

    struct Plane {
        Point normal{};  // unit normal towards the legal half space; zero disables
        float offset{};  // legal centers satisfy dot(normal, p) >= offset + radius
    };

    struct StepSettings {
        float gravity{};  // signed on Y; the rest sheet extends along negative Y
        float damping{};  // linear velocity damping per second
        float speedCap{};  // dynamic vertex speed limit; zero disables
        std::span<const Obstacle> boxes{};  // first kMaxBoxes, in stable obstacle order
        Plane backing{};
        float radius{};
        float friction{};  // combined cloth/obstacle coefficient
    };

    class Solver {
      public:
        static constexpr float kTick = 1.0F / 120.0F;
        static constexpr std::size_t kMaxBoxes = 32;

        // All inputs have SHEET_GRID_VERTICES entries in SheetGrid order. Inverse masses are
        // immutable after construction; zero-mass pins advance with their assigned velocity,
        // unaffected by damping, constraints, collisions and the speed cap.
        Solver(const SheetGrid &rest,
            std::span<const Point> initialPositions,
            std::span<const Point> initialVelocities,
            std::span<const float> inverseMasses);

        [[nodiscard]] std::span<Point> positions() noexcept {
            return m_positions;
        }
        [[nodiscard]] std::span<const Point> positions() const noexcept {
            return m_positions;
        }
        [[nodiscard]] std::span<Point> velocities() noexcept {
            return m_velocities;
        }
        [[nodiscard]] std::span<const Point> velocities() const noexcept {
            return m_velocities;
        }

        void step(const StepSettings &settings) noexcept;

      private:
        using Index = std::uint16_t;

        struct Edge {
            std::array<Index, 2> vertices;
            float restLength;
        };
        struct Hinge {
            std::array<Index, 4> vertices;  // oriented shared edge, then the two tip vertices
            float restAngle;
        };
        struct Attachment {
            Index pin, vertex;
            float range;
        };
        template <typename Constraint>
        struct Batches {
            std::vector<Constraint> constraints;
            std::vector<std::size_t> ends;  // exclusive end of each conflict-free level
        };

        template <typename Constraint>
        static Batches<Constraint> make_batches(
            std::vector<Constraint> constraints, std::vector<std::size_t> &levels);
        void make_attachments(const std::vector<Edge> &edges);
        void solve_hinges() noexcept;
        void solve_edges() noexcept;
        void solve_attachments() noexcept;

        std::array<Point, SHEET_GRID_VERTICES> m_positions, m_velocities, m_previous;
        std::array<float, SHEET_GRID_VERTICES> m_inverseMasses;
        Batches<Edge> m_edges;
        Batches<Hinge> m_hinges;
        // One conflict-free batch: every attachment writes a distinct dynamic vertex and
        // reads only that vertex and a pin that stays immutable throughout the batch.
        std::vector<Attachment> m_attachments;
        Box m_broad_bounds;
    };
}  // namespace hyprimate::cloth
