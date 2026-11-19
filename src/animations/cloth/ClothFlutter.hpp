#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {

    struct ClothParams {
        double duration = 1.0, flutter = 0.5, shading = 0.7, variation = 0.5, shine = 0.0;
        bool operator==(const ClothParams &) const = default;
    };

    struct ClothMovementParams {
        double strength = 0.65, flutter = 0.5, shading = 0.7, variation = 0.5, shine = 0.0,
               springiness = 0.0, gravity = 1.0;
        bool operator==(const ClothMovementParams &) const = default;
    };

    struct ClothCloseGeometry {
        Edge edge = Edge::Bottom;
        Box screen;  // physical monitor boundary, including reserved areas, in logical pixels
    };

    // Owns Jolt's process-wide state and a worker reaper. Shutdown stops and joins every simulation
    // before destroying Jolt. Releasing/cancelling a cloth only requests stop; joining and
    // recording destruction happen on the reaper.
    class ClothPhysics {
      public:
        ClothPhysics();
        ~ClothPhysics();
        ClothPhysics(const ClothPhysics &) = delete;
        ClothPhysics &operator=(const ClothPhysics &) = delete;
        ClothPhysics(ClothPhysics &&) = delete;
        ClothPhysics &operator=(ClothPhysics &&) = delete;

        // Deterministic worker starvation control for model tests. Does not affect already
        // published snapshots.
        static void pause_workers_for_testing(bool pause, int after_snapshots_count = 0) noexcept;
    };

    // A fully simulated 57x57 fabric panel with a weighted hem, suspended along its entire top
    // edge. Opening spreads a vertically pleated sheet; closing carries its top rod toward the
    // selected screen edge. Copies retain the same immutable recording; each copy has its own
    // playback clock.
    class ClothFlutter {
      public:
        ClothFlutter();
        ~ClothFlutter();
        ClothFlutter(const ClothFlutter &other);
        ClothFlutter &operator=(const ClothFlutter &other);
        ClothFlutter(ClothFlutter &&) noexcept;
        ClothFlutter &operator=(ClothFlutter &&) noexcept;
        // Coordinates are logical pixels. Overlapping obstacles are ignored. Close inherits an
        // active opening's displayed shape, opacity and real-time velocity. Missing close geometry
        // defaults to a downward exit just below the window; provided invalid geometry, invalid
        // params or no live ClothPhysics leave the model idle.
        void begin(Transition kind,
            const ClothParams &params,
            std::uint64_t seed,
            const Box &window,
            std::span<const Box> obstacles,
            std::optional<ClothCloseGeometry> geometry = std::nullopt);
        // Geometry updates only set a target; advance() sends one timestamped target per rendered
        // frame, including stationary frames while the sheet settles.
        void begin_movement(
            const ClothMovementParams &params, std::uint64_t seed, const Box &capture);
        // Applied to the next published Movement target without restarting the worker.
        void set_movement_params(const ClothMovementParams &params) noexcept;
        void update_movement(const Box &capture, double elapsed_seconds) noexcept;
        [[nodiscard]] bool moving() const noexcept;
        bool advance(double elapsed_seconds) noexcept;
        void reset() noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        [[nodiscard]] double opacity() const noexcept;
        [[nodiscard]] bool shaded() const noexcept;
        void frame(Vec2 size, SheetFrame &out) const noexcept;

        // Model diagnostics: wait off the compositor path for a complete recording; sample the
        // displayed normalized sheet (x right, y up from the top, z towards viewer) and its
        // velocity per real second.
        [[nodiscard]] bool wait_for_recording(double timeout_seconds = 10.0) const noexcept;
        [[nodiscard]] bool wait_for_snapshots(
            int count, double timeout_seconds = 10.0) const noexcept;
        void sample_vertices(SheetGrid &positions, SheetGrid &velocities) const noexcept;

        struct Run;
        struct Fallback;
        struct MovementPlayback;

      private:
        enum class Phase : std::uint8_t { Idle, Opening, Movement, Closing, Closed };
        void start_fallback() noexcept;

        Phase m_phase = Phase::Idle;
        ClothParams m_params{};
        double m_t = 0, m_rate = 0;
        double m_startup_wait = 0, m_fallback_time = 0, m_initial_opacity = 1;
        int m_startup_recorded = 0;
        bool m_transition_held = false;
        std::shared_ptr<Run> m_run;
        std::shared_ptr<Fallback> m_fallback;
        std::unique_ptr<MovementPlayback> m_movement;
    };

}  // namespace hyprimate
