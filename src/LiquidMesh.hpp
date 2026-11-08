#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace liquid_glass {

struct Vec2 {
    double x = 0;
    double y = 0;
};

enum class MotionMode { Liquid, Wiggle };

// Liquid material tuning. Finite values are clamped to [0, 1]; nonfinite Liquid values reset before integration.
struct MaterialParams {
    double viscosity     = 0.75; // [0,1] Liquid flow resistance (existing semantics)
    double elasticity    = 0.5;  // [0,1] restoring stiffness / recovery speed; higher = snappier, firmer return
    double momentum      = 0.5;  // [0,1] inertia carried past a stop (overshoot amount); 0 = none
    // [0,1] Liquid-only per-motion randomness: each movement from rest draws its own timing, damping, lag, bow
    // shape, skew, sideways lean and response latency from the mesh's seed. 0 = every mesh responds identically.
    double variation     = 0.0;
    bool operator==(const MaterialParams&) const = default;
};

// Logical-pixel material state; no compositor or graphics dependency.
class LiquidMesh {
  public:
    // Meshes with the same mode, params and seed respond identically; give each window its own seed.
    explicit LiquidMesh(MotionMode mode = MotionMode::Liquid, MaterialParams params = {}, std::uint64_t seed = 0) noexcept;
    MotionMode                  mode() const noexcept { return m_mode; }
    const MaterialParams&       params() const noexcept { return m_params; }
    double                      viscosity() const noexcept { return m_params.viscosity; }
    void reset() noexcept;
    void recordTranslation(Vec2 delta, Vec2 size, double strength) noexcept;
    bool advance(double elapsedSeconds, Vec2 size) noexcept;
    bool settled() const noexcept;
    // C1-smooth interpolation of the 9x9 lattice field; u and v are clamped to [0, 1].
    Vec2 sampleDisplacement(double u, double v) const noexcept;
    const std::array<Vec2, 81>& displacements() const noexcept { return m_displacements; }

  private:
    bool constrain(Vec2 size) noexcept;
    bool validTriangles(Vec2 size) const noexcept;
    void integrateLiquidSubstep(Vec2 size) noexcept;
    // Draws a new Liquid character (gains and shape) scaled by params.variation; neutral when variation is 0.
    void rollCharacter() noexcept;
    double drawUnit() noexcept; // uniform in [-1, 1] from m_rng

    MotionMode     m_mode;
    MaterialParams m_params;
    // Viscosity-derived velocity coefficient for internal shear flow.
    double m_shearDamping;
    // Liquid body and head spring gains, derived from elasticity, momentum and the current character.
    double m_bodyStiffness = 0;
    double m_bodyDamping   = 0;
    double m_headStiffness = 0;
    double m_headDamping   = 0;
    // Liquid character, redrawn by rollCharacter at each motion onset; all neutral when variation is 0.
    double        m_lagGain         = 1;    // trailing lag multiplier
    double        m_bowDepth        = 0;    // trailing-edge bow depth (corner lag share removed)
    double        m_taperRatio      = 0;    // cross-axis tail taper per unit lag
    double        m_skew            = 0;    // cross-axis offset of the bow's deepest point, in s units
    double        m_lean            = 0;    // sideways tail shear per unit lag
    double        m_kickGain        = 1;    // stop-kick multiplier
    double        m_velocitySeconds = 0;    // window-velocity filter time constant (response latency)
    std::uint64_t m_rng             = 0;    // splitmix64 state

    std::array<Vec2, 81> m_displacements{};
    std::array<Vec2, 81> m_velocities{};
    std::array<Vec2, 81> m_accelerations{};
    // Liquid-only scratch: divergence of the pre-substep relative field, recomputed every substep.
    std::array<double, 81> m_divergence{};
    // Liquid-only: per-node head grip in [0, 1] (1 = firm leading face), relaxed toward its target each substep.
    std::array<double, 81> m_grip{};
    // Liquid-only: translation recorded since the last advance, and the window velocity it implies.
    Vec2   m_pendingDelta{};
    Vec2   m_windowVelocity{};
    Vec2   m_smoothDelta{};  // exponentially weighted translation (px), decays with the velocity time constant
    double m_smoothTime = 0; // exponentially weighted time (s), same weights as m_smoothDelta
    Vec2   m_slowDelta{};    // slower (inertia) estimate of translation (px), averages over zero-delta frames
    double m_slowTime = 0;   // matching slower time (s)
    Vec2   m_slowVelocity{}; // slower velocity the inertia is differentiated from
    Vec2   m_inertia{};      // pseudo-force from smoothed deceleration, applied on every substep of one advance
    double m_strength = 0;

    double   m_accumulator = 0;
    unsigned m_quietSteps  = 0;
};

} // namespace liquid_glass
