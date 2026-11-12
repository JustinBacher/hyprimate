#pragma once

#include "EdgeTransition.hpp"
#include "SheetFacets.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace liquid_glass {

struct ClothParams {
    double duration = 1.0, flutter = 0.5, shading = 0.7, variation = 0.5;
    bool   operator==(const ClothParams &) const = default;
};

struct ClothMovementParams {
    double strength = 0.65, flutter = 0.5, shading = 0.7, variation = 0.5;
    bool   operator==(const ClothMovementParams &) const = default;
};

struct ClothCloseGeometry {
    Edge edge = Edge::Bottom;
    Box  screen; // physical monitor boundary, including reserved areas, in logical pixels
};

// Owns Jolt's process-wide state and a worker reaper. Shutdown stops and joins every simulation before destroying
// Jolt. Releasing/cancelling a cloth only requests stop; joining and recording destruction happen on the reaper.
class ClothPhysics {
  public:
    ClothPhysics();
    ~ClothPhysics();
    ClothPhysics(const ClothPhysics &) = delete;
    ClothPhysics &operator=(const ClothPhysics &) = delete;

    // Deterministic worker starvation control for model tests. Does not affect already published snapshots.
    static void pauseWorkersForTesting(bool pause, int afterSnapshots = 0) noexcept;
};

// A fully simulated 57x57 sheet. Opening drops an outward-raised sheet from its fixed top edge. Closing releases
// all particles to fall downward, or pulls the complete leading edge upward/sideways while the rest trails behind.
// Copies retain the same immutable recording; each copy has its own playback clock.
class ClothFlutter {
  public:
    ClothFlutter();
    ~ClothFlutter();
    ClothFlutter(const ClothFlutter &);
    ClothFlutter &operator=(const ClothFlutter &);
    ClothFlutter(ClothFlutter &&) noexcept;
    ClothFlutter &operator=(ClothFlutter &&) noexcept;
    // Coordinates are logical pixels. Overlapping obstacles are ignored. Close inherits an active opening's
    // displayed shape, opacity and real-time velocity. Missing close geometry defaults to a downward exit just
    // below the window; provided invalid geometry, invalid params or no live ClothPhysics leave the model idle.
    void begin(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
               std::span<const Box> obstacles, std::optional<ClothCloseGeometry> closeGeometry = std::nullopt);
    // The grab coordinate is in [0,1] across the decorated capture. Geometry updates only set a target;
    // advance() sends one timestamped target per rendered frame, including stationary frames while the sheet settles.
    void   beginMovement(const ClothMovementParams &params, std::uint64_t seed, const Box &capture, Vec2 grabUV);
    void   updateMovement(const Box &capture, double elapsedSeconds) noexcept;
    bool   moving() const noexcept;
    bool   advance(double elapsedSeconds) noexcept;
    void   reset() noexcept;
    bool   active() const noexcept;
    bool   closed() const noexcept;
    double opacity() const noexcept;
    bool   shaded() const noexcept;
    void   facets(Vec2 size, PaperFacets &out) const noexcept;

    // Model diagnostics: wait off the compositor path for a complete recording; sample the displayed normalized
    // sheet (x right, y up from the top, z towards viewer) and its velocity per real second.
    bool waitForRecording(double timeoutSeconds = 10.0) const noexcept;
    bool waitForSnapshots(int count, double timeoutSeconds = 10.0) const noexcept;
    void sampleVertices(SheetGrid &positions, SheetGrid &velocities) const noexcept;

    struct Run;
    struct Fallback;
    struct MovementPlayback;

  private:
    enum class Phase { Idle, Opening, Movement, Closing, Closed };
    void startFallback() noexcept;

    Phase                             m_phase = Phase::Idle;
    ClothParams                       m_params{};
    double                            m_t = 0, m_rate = 0;
    double                            m_startupWait = 0, m_fallbackTime = 0, m_initialOpacity = 1;
    std::shared_ptr<Run>              m_run;
    std::shared_ptr<Fallback>         m_fallback;
    std::unique_ptr<MovementPlayback> m_movement;
};

} // namespace liquid_glass
