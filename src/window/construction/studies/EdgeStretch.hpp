#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "shared/Core.hpp"

namespace hyprimate::construction::studies {

    enum class EdgeStretchAction { Hold, Pull, Push };
    enum class EdgeWorkerStage { Approach, Support, Release };

    struct EdgeStretchStep {
        Box body{};  // Authoritative logical rectangle, never time-filtered.
        Box work_area{};  // Optional apron limits in the same coordinates as body.
        double elapsed = 0;
        double sample_seconds =
            0;  // Measured geometry interval; can be nonzero on an elapsed-zero step.
        bool held = false;  // Keep inferred edges through a pause; new motion can retarget them.
        bool stalled = false;
        // Exact grabbed edges, when known. An empty set explicitly releases them;
        // nullopt instead infers edges from consecutive authoritative bodies.
        std::optional<EdgeSet> grab{};
    };

    struct EdgeWorkerPose {
        struct Arm {
            Vec3 shoulder{}, elbow{}, wrist{}, glove{};
            bool gripping = false;
        };
        struct Leg {
            Vec3 hip{}, knee{}, ankle{}, sole{};
            bool planted = false;
        };
        Edge edge{};
        unsigned slot = 0;  // Identity with edge, stable through same-edge reversals/regrabs.
        unsigned variant = 0;
        EdgeStretchAction action{};
        EdgeWorkerStage stage{};
        double height = 0, facing = 1, opacity = 0;
        Vec3 hip{}, neck{};
        // Index 0 is near, index 1 is far. Points are in the same logical world
        // coordinates as body(), with positive z toward the viewer. Grip depth is zero.
        std::array<Arm, 2> arms{};
        std::array<Leg, 2> legs{};
    };

    class EdgeStretch {
      public:
        explicit EdgeStretch(std::uint64_t seed, double worker_size = 1) noexcept
            : m_seed(seed), m_worker_size(worker_size) {}
        void step(const EdgeStretchStep &sample) noexcept;

        // The entire intact face is this one rectangle. No image or source-sized geometry is
        // stored.
        [[nodiscard]] Box body() const noexcept {
            return m_body;
        }
        [[nodiscard]] bool active() const noexcept {
            return m_count != 0;
        }
        // Includes arriving/departing actors even when their current opacity is zero.
        [[nodiscard]] std::span<const EdgeWorkerPose> workers() const noexcept {
            return {m_poses.data(), m_worker_size == 0 ? 0 : m_count};
        }

      private:
        struct Boot {
            Vec3 sole{}, start{}, end{};
            double elapsed = 0, duration = 0;
        };
        struct Job {
            bool live = false, leaving = false;
            double age = 0, presence = 0, leave_presence = 0;
            double size = 0, speed = 0, delay = 0, stride = 0, swing = 0;
            double lane = 1, facing = 1, approach = 0, exit_duration = 0;
            double fraction = 0;
            // Hip height, pelvis shift, shoulder lean in body heights.
            std::array<double, 3> shape{};
            Vec3 foot{}, origin{}, exit_to{}, station{}, station_velocity{};
            std::array<Boot, 2> boots{};
            std::array<Vec3, 2> gloves{};
            std::array<bool, 2> latched{};
            unsigned variant = 0;
        };
        struct Timing {
            double elapsed, follow, lead, blend;
        };
        static constexpr std::size_t MAX_WORKERS = 6;

        void enlist(Job &job,
            Edge edge,
            unsigned slot,
            const Box &area,
            bool leading_corner) const noexcept;
        void pose(Job &job,
            Edge edge,
            unsigned slot,
            EdgeStretchAction action,
            bool assigned,
            const Box &area,
            const Timing &timing,
            EdgeWorkerPose &out) const noexcept;

        std::uint64_t m_seed;
        double m_worker_size;
        Box m_body{};
        bool m_was_held = false;
        std::array<double, 4> m_velocity{}, m_recent{};
        std::array<std::array<Job, 2>, 4>
            m_jobs{};  // Two named sockets per edge; at most six live jobs.
        std::array<EdgeWorkerPose, MAX_WORKERS> m_poses{};
        std::size_t m_count = 0;
    };

}  // namespace hyprimate::construction::studies
