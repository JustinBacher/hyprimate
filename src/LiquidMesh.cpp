#include "LiquidMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace liquid_glass {
namespace {

using Field = std::array<Vec2, 81>;
using Scalars = std::array<double, 81>;

constexpr unsigned kSide = 9;  // nodes per lattice side
constexpr unsigned kCells = 8; // cells per lattice side

constexpr double kSubstepSeconds = 1.0 / 240.0;
// Tolerates floating-point drift so frame cadences that land exactly on a substep boundary
// (for example 144 Hz frames summed to 0.5 s) take that substep rather than deferring it.
constexpr double kAccumulatorTolerance = kSubstepSeconds * 1e-9;
constexpr double kMaxFrameSeconds = 0.05;
constexpr double kGapResetSeconds = 0.25;
constexpr unsigned kMaxSubstepsPerAdvance = 12;

// Wiggle mode (unchanged).
constexpr double kAnchorStiffness = 144.0;
constexpr double kAnchorDamping = 24.0;
constexpr double kNeighborStiffness = 80.0;
constexpr double kNeighborDamping = 12.0;

// Liquid mode.
// Body natural frequency (rad/s) at elasticity 0.5; elasticity scales it by 2^(elasticity - 0.5).
constexpr double kLiquidBaseOmega = 26.0;
// Head springs are 4x body stiffness and critically damped, so the leading face grips.
constexpr double kLiquidHeadStiffnessScale = 4.0;
constexpr double kLiquidShearStiffness = 60.0;
// Viscosity maps linearly onto shear-velocity damping: 4 at viscosity 0, 48 at viscosity 1.
constexpr double kLiquidShearDampingMin = 4.0;
constexpr double kLiquidShearDampingRange = 44.0;
// Pressure wave speed as a multiple of the shorter window side.
constexpr double kLiquidWaveSpeedScale = 1.5;
// Steady trailing lag in px at strength 1: lag = K * V^2 / (V + V0), V the window speed in px/s.
// Quadratic for slow drags (gentle), about linear for fast ones.
constexpr double kLiquidLagSeconds = 0.075;
constexpr double kLiquidLagSpeed = 600.0;
// Trailing edge bows back at its middle: corners lag (1 - kLiquidBowDepth) of the centre.
constexpr double kLiquidBowDepth = 0.5;
// Trailing corners taper inward cross-axis by this fraction of the centre lag. The taper and the bow
// both follow t^2, so the side edges sag into curves toward the tail instead of sloping straight.
constexpr double kLiquidTaperRatio = 0.18;
// Grip relaxes toward its target with this time constant, so a stop releases the leading face over ~100 ms.
constexpr double kLiquidGripRelaxSeconds = 0.04;
// Inertia gain, scaled by momentum: the leading face takes the smoothed deceleration as a forward push at a stop.
constexpr double kLiquidStopKick = 2.2;
// Trailing edge share of the stop kick relative to the leading face, which sets the forward surge.
constexpr double kLiquidStopTrailRatio = 1.0;
// Time constant of the window-velocity estimate, so irregular position updates still read as steady motion.
constexpr double kLiquidVelocitySeconds = 0.06;
// Time constant of the slower velocity the inertia is differentiated from; averages irregular frames further.
constexpr double kLiquidInertiaSeconds = 0.15;
// Smoothed speed (px/s) below which the head grip releases, so only a sustained stop frees the leading face.
constexpr double kLiquidGripQuietSpeed = 2.0;
// Deceleration rate (1/s) relative to speed that counts as a landing and releases the grip at once; a pure
// stop reads 1/kLiquidVelocitySeconds, so steady drag ripple stays below this.
constexpr double kLiquidGripLandingRate = 4.0;
// Window speed cap in shorter sides per second, so a bad sample cannot launch the lattice.
constexpr double kLiquidMaxSpeedScale = 10.0;
// Head grip ramps from 0 at this q (on the trailing side of the leading face) to 1 at q = 1.
constexpr double kLiquidGripStartQ = 0.25;

constexpr double kMaxDisplacementLimit = 120.0;
constexpr double kMaxDisplacementFraction = 0.2;
// Wiggle keeps its original hard bound: min(96 px, 12% of the shorter side).
constexpr double kWiggleMaxDisplacementLimit = 96.0;
constexpr double kWiggleMaxDisplacementFraction = 0.12;
constexpr double kMinAreaRatio = 0.2;
constexpr unsigned kMaxHalvings = 8;

constexpr double kQuietDisplacement = 0.15;
constexpr double kQuietVelocity = 2.0;
constexpr unsigned kQuietSubsteps = 3;

struct GridPoint {
    unsigned x;
    unsigned y;
};

constexpr std::size_t gridIndex(unsigned x, unsigned y) noexcept {
    return static_cast<std::size_t>(y) * kSide + x;
}

bool isFinite(Vec2 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool isValidSize(Vec2 size) noexcept {
    return isFinite(size) && size.x > 0.0 && size.y > 0.0;
}

// Finite parameters are clamped to [0, 1]. Nonfinite input is kept so Liquid can reject it before
// integration, rather than being replaced by a fabricated default.
double normalizeParam(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : value;
}

MaterialParams normalizeParams(MaterialParams p) noexcept {
    p.viscosity = normalizeParam(p.viscosity);
    p.elasticity = normalizeParam(p.elasticity);
    p.momentum = normalizeParam(p.momentum);
    return p;
}

bool hasFiniteParams(const MaterialParams& p) noexcept {
    return std::isfinite(p.viscosity) && std::isfinite(p.elasticity) && std::isfinite(p.momentum);
}

// Damping ratio from momentum: 1 = critically damped (no overshoot). Momentum 0.5 gives a trailing
// surge of about 20% of the lag after a stop; momentum 1 gives a larger, still decaying surge.
double dampingRatio(double momentum) noexcept {
    return momentum <= 0.5 ? 1.0 - 1.2 * momentum : 0.4 - 0.4 * (momentum - 0.5);
}

// Displacement magnitude bound: a soft saturation with this scale replaces the old hard clamp for Liquid.
double displacementLimit(Vec2 size) noexcept {
    return std::min(kMaxDisplacementLimit, kMaxDisplacementFraction * std::min(size.x, size.y));
}

// Soft saturation of one displacement. Magnitudes up to 0.75 of the limit pass through unchanged, so
// routine per-call constraint adds no damping and the response is independent of call cadence.
// Above the knee the magnitude follows a tanh that is C1 at the knee and approaches limit.
// The outward component of velocity is scaled by sech^2 of the tanh, the derivative of the map
// above the knee, so saturation does not inject energy. Below the knee nothing changes.
void softSaturate(Vec2& d, Vec2& v, double magnitude, double limit) noexcept {
    const double knee = 0.75 * limit;
    if (magnitude <= knee)
        return;
    const double span = limit - knee;
    const double t = std::tanh((magnitude - knee) / span);
    const double mapped = knee + span * t;
    const double nx = d.x / magnitude;
    const double ny = d.y / magnitude;
    const double radialVelocity = v.x * nx + v.y * ny;
    const double sech2 = 1.0 - t * t;
    v.x -= (1.0 - sech2) * radialVelocity * nx;
    v.y -= (1.0 - sech2) * radialVelocity * ny;
    d = {nx * mapped, ny * mapped};
}

bool isZero(const Field& field) noexcept {
    for (const Vec2& p : field) {
        if (p.x != 0.0 || p.y != 0.0)
            return false;
    }
    return true;
}

bool isZero(Vec2 v) noexcept {
    return v.x == 0.0 && v.y == 0.0;
}

Vec2 restPosition(GridPoint p, Vec2 size) noexcept {
    return {size.x * p.x / kCells, size.y * p.y / kCells};
}

double signedArea(Vec2 a, Vec2 b, Vec2 c) noexcept {
    return 0.5 * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

// Corners of the two triangles in cell (cx, cy), in renderer order.
std::array<GridPoint, 3> triangleCorners(unsigned cx, unsigned cy, unsigned which) noexcept {
    if (which == 0)
        return {GridPoint{cx, cy}, GridPoint{cx, cy + 1}, GridPoint{cx + 1, cy}};
    return {GridPoint{cx + 1, cy}, GridPoint{cx, cy + 1}, GridPoint{cx + 1, cy + 1}};
}

bool isQuiet(const Field& displacements, const Field& velocities) noexcept {
    constexpr double maxDisplacement2 = kQuietDisplacement * kQuietDisplacement;
    constexpr double maxVelocity2 = kQuietVelocity * kQuietVelocity;
    for (std::size_t i = 0; i < displacements.size(); ++i) {
        const Vec2& d = displacements[i];
        const Vec2& v = velocities[i];
        if (d.x * d.x + d.y * d.y >= maxDisplacement2 || v.x * v.x + v.y * v.y >= maxVelocity2)
            return false;
    }
    return true;
}

// Compute every acceleration from the same old field before updating any node.
// A single scratch array avoids copying/swapping complete state arrays each substep.
void integrateWiggleSubstep(Field& d, Field& v, Field& accelerations) noexcept {
    for (unsigned y = 0; y < kSide; ++y) {
        for (unsigned x = 0; x < kSide; ++x) {
            double sumDx = 0.0;
            double sumDy = 0.0;
            double sumVx = 0.0;
            double sumVy = 0.0;
            unsigned count = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0)
                        continue;
                    const int nx = static_cast<int>(x) + ox;
                    const int ny = static_cast<int>(y) + oy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(kSide) || ny >= static_cast<int>(kSide))
                        continue;
                    const std::size_t j = gridIndex(static_cast<unsigned>(nx), static_cast<unsigned>(ny));
                    sumDx += d[j].x;
                    sumDy += d[j].y;
                    sumVx += v[j].x;
                    sumVy += v[j].y;
                    ++count;
                }
            }

            const double inverse = 1.0 / static_cast<double>(count);
            const double meanDx = sumDx * inverse;
            const double meanDy = sumDy * inverse;
            const double meanVx = sumVx * inverse;
            const double meanVy = sumVy * inverse;

            const std::size_t i = gridIndex(x, y);
            const double accelX = -kAnchorStiffness * d[i].x - kAnchorDamping * v[i].x +
                                  kNeighborStiffness * (meanDx - d[i].x) +
                                  kNeighborDamping * (meanVx - v[i].x);
            const double accelY = -kAnchorStiffness * d[i].y - kAnchorDamping * v[i].y +
                                  kNeighborStiffness * (meanDy - d[i].y) +
                                  kNeighborDamping * (meanVy - v[i].y);

            accelerations[i] = {accelX, accelY};
        }
    }
    for (std::size_t i = 0; i < d.size(); ++i) {
        v[i].x += accelerations[i].x * kSubstepSeconds;
        v[i].y += accelerations[i].y * kSubstepSeconds;
        d[i].x += v[i].x * kSubstepSeconds;
        d[i].y += v[i].y * kSubstepSeconds;
    }
}

// Head grip for a lattice coordinate q in [-1, 1] (q = 1 is the leading face): 0 on the body,
// rising smoothly to 1 at the leading face. Smoothstep keeps forces continuous across the ramp.
double headGrip(double q) noexcept {
    const double t = std::clamp((q - kLiquidGripStartQ) / (1.0 - kLiquidGripStartQ), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Derivative along one lattice line: central inside, one-sided at the two free edges.
// `sample(k)` returns the scalar at position k along the line; `h` is the lattice spacing.
template <typename Sample>
double lineSlope(Sample sample, unsigned k, double h) noexcept {
    if (k == 0)
        return (sample(1) - sample(0)) / h;
    if (k == kSide - 1)
        return (sample(kSide - 1) - sample(kSide - 2)) / h;
    return (sample(k + 1) - sample(k - 1)) / (2.0 * h);
}

// Second difference along one lattice line with mirror ghosts at the free edges.
// The ghost takes the interior neighbour's value, so the edge node feels no outward pull.
template <typename Sample>
double lineLaplacian(Sample sample, unsigned k) noexcept {
    const double left = (k == 0) ? sample(1) : sample(k - 1);
    const double right = (k == kSide - 1) ? sample(kSide - 2) : sample(k + 1);
    return left + right - 2.0 * sample(k);
}

// Anisotropic index-space Laplacian of one Vec2 component. wx and wy scale each axis.
double shearLaplacian(const Field& f, double Vec2::*comp, unsigned x, unsigned y, double wx, double wy) noexcept {
    const double lx = lineLaplacian([&](unsigned k) { return f[gridIndex(k, y)].*comp; }, x);
    const double ly = lineLaplacian([&](unsigned k) { return f[gridIndex(x, k)].*comp; }, y);
    return wx * lx + wy * ly;
}

// Catmull-Rom cubic through p1 and p2 at t in [0, 1]; equals p1 at t = 0 and p2 at t = 1.
double catmullRom(double p0, double p1, double p2, double p3, double t) noexcept {
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

} // namespace

LiquidMesh::LiquidMesh(MotionMode mode, MaterialParams params) noexcept
    : m_mode(mode), m_params(normalizeParams(params)),
      m_shearDamping(kLiquidShearDampingMin + kLiquidShearDampingRange * m_params.viscosity) {
    // Gains depend only on elasticity and momentum, so compute once here, not per substep.
    const double omega = kLiquidBaseOmega * std::exp2(m_params.elasticity - 0.5);
    const double zeta = dampingRatio(m_params.momentum);
    m_bodyStiffness = omega * omega;
    m_bodyDamping = 2.0 * zeta * omega;
    m_headStiffness = kLiquidHeadStiffnessScale * m_bodyStiffness;
    // Head is always critically damped, so its damping is 2 * sqrt(stiffness).
    m_headDamping = 2.0 * std::sqrt(m_headStiffness);
}

void LiquidMesh::reset() noexcept {
    m_displacements.fill(Vec2{});
    m_velocities.fill(Vec2{});
    m_grip.fill(0.0);
    m_pendingDelta = {};
    m_windowVelocity = {};
    m_smoothDelta = {};
    m_smoothTime = 0.0;
    m_inertia = {};
    m_slowDelta = {};
    m_slowTime = 0.0;
    m_slowVelocity = {};
    m_strength = 0.0;
    m_accumulator = 0.0;
    m_quietSteps = 0;
}

void LiquidMesh::recordTranslation(Vec2 delta, Vec2 size, double strength) noexcept {
    // A nonfinite Liquid parameter has no defined response, so the field must not be excited.
    if (!isFinite(delta) || !isValidSize(size) || !std::isfinite(strength) || strength <= 0.0 ||
        (m_mode == MotionMode::Liquid && !hasFiniteParams(m_params))) {
        reset();
        return;
    }

    const double length = std::hypot(delta.x, delta.y);
    if (!std::isfinite(length)) {
        reset();
        return;
    }

    if (length > 0.0) {
        const double ex = delta.x / length;
        const double ey = delta.y / length;
        // Unit direction has |ex|+|ey| >= 1, so this normalizer is always positive.
        const double normalizer = 0.5 * (std::abs(ex) + std::abs(ey));
        for (unsigned y = 0; y < kSide; ++y) {
            for (unsigned x = 0; x < kSide; ++x) {
                const double u = static_cast<double>(x) / kCells;
                const double v = static_cast<double>(y) / kCells;
                // q is -1 at the trailing edge and +1 at the leading edge.
                const double q = ((u - 0.5) * ex + (v - 0.5) * ey) / normalizer;
                const std::size_t i = gridIndex(x, y);
                if (m_mode == MotionMode::Wiggle) {
                    const double bowFactor =
                        0.65 + 0.35 * std::sin(std::numbers::pi * u) * std::sin(std::numbers::pi * v);
                    const double edgeFactor = 0.25 + 0.75 * (1.0 - q) / 2.0;
                    const double weight = edgeFactor * bowFactor;
                    const double scale = strength * weight;

                    m_displacements[i].x -= scale * delta.x;
                    m_displacements[i].y -= scale * delta.y;
                }
            }
        }
        if (m_mode == MotionMode::Liquid) {
            m_pendingDelta.x += delta.x;
            m_pendingDelta.y += delta.y;
            m_strength = strength;
        }
        m_quietSteps = 0;
    }

    // Applies bounds and triangle protection to the field, including for zero delta.
    constrain(size);
}

// Liquid substep. The field d relaxes toward a steady lag field that depends
// only on window velocity: the trailing edge bows back at its middle, the trailing corners taper
// inward cross-axis, and the leading face carries no lag. Each node blends body and head stiffness
// by its grip. Neighbour coupling (shear and pressure) smooths the field into one continuous body.
// When motion stops the lag field returns to zero, so the trailing mass surges forward and rings down.
// Divergence and every acceleration read only old state, so the update is simultaneous.
void LiquidMesh::integrateLiquidSubstep(Vec2 size) noexcept {
    Field& d = m_displacements;
    Field& v = m_velocities;
    Field& a = m_accelerations;
    const Field& e = d;
    Scalars& div = m_divergence;

    const Vec2 spacing{size.x / kCells, size.y / kCells};
    // Scale each axis's shear so the tighter axis keeps the index-space stiffness and
    // the looser axis is weakened in proportion, giving comparable physical coupling.
    const double tighter = std::min(spacing.x, spacing.y);
    const double wx = (tighter / spacing.x) * (tighter / spacing.x);
    const double wy = (tighter / spacing.y) * (tighter / spacing.y);
    const double waveSpeed = kLiquidWaveSpeedScale * std::min(size.x, size.y);
    const double waveSpeed2 = waveSpeed * waveSpeed;

    for (unsigned y = 0; y < kSide; ++y) {
        for (unsigned x = 0; x < kSide; ++x) {
            const double dx = lineSlope([&](unsigned k) { return e[gridIndex(k, y)].x; }, x, spacing.x);
            const double dy = lineSlope([&](unsigned k) { return e[gridIndex(x, k)].y; }, y, spacing.y);
            div[gridIndex(x, y)] = dx + dy;
        }
    }

    // Lag magnitude in px: quadratic in speed for slow drags, about linear for fast ones.
    const double speed = std::hypot(m_windowVelocity.x, m_windowVelocity.y);
    const double lag = speed > 0.0
                           ? kLiquidLagSeconds * m_strength * speed * speed / (speed + kLiquidLagSpeed)
                           : 0.0;
    // With no motion the lag is zero, so the direction is unused and any unit vector serves.
    const double dirX = speed > 0.0 ? m_windowVelocity.x / speed : 1.0;
    const double dirY = speed > 0.0 ? m_windowVelocity.y / speed : 0.0;
    const double perpX = -dirY;
    const double perpY = dirX;
    // A unit direction has |dx| + |dy| >= 1, so this normalizer is always positive.
    const double normalizer = 0.5 * (std::abs(dirX) + std::abs(dirY));
    const double gripBlend = 1.0 - std::exp(-kSubstepSeconds / kLiquidGripRelaxSeconds);
    const double shearDamping = m_shearDamping;
    // Landing: smoothed deceleration is a large share of speed, so the front releases at once instead of
    // waiting for the smoothed speed to fall through the quiet threshold.
    const bool landing = std::hypot(m_inertia.x, m_inertia.y) > kLiquidGripLandingRate * speed;

    for (unsigned y = 0; y < kSide; ++y) {
        for (unsigned x = 0; x < kSide; ++x) {
            const std::size_t i = gridIndex(x, y);
            // Lattice position relative to the centre, in [-0.5, 0.5].
            const double u = static_cast<double>(x) / kCells - 0.5;
            const double w = static_cast<double>(y) / kCells - 0.5;
            // q: -1 at the trailing edge, +1 at the leading face. s: -1..1 across the motion.
            const double q = std::clamp((u * dirX + w * dirY) / normalizer, -1.0, 1.0);
            const double s = std::clamp((u * perpX + w * perpY) / normalizer, -1.0, 1.0);
            // Grip relaxes toward head grip while moving and toward zero when still, so a stop releases the front.
            m_grip[i] += ((speed > kLiquidGripQuietSpeed && !landing ? headGrip(q) : 0.0) - m_grip[i]) * gripBlend;
            const double g = m_grip[i];
            const double stiffness = m_bodyStiffness * (1.0 - g) + m_headStiffness * g;
            const double lead = std::max(q, 0.0);
            const double trail = std::max(-q, 0.0);
            // Inertia weights: the leading face carries the push forward and the trailing edge a weaker share.
            const double kickWeight = kLiquidStopKick * m_params.momentum *
                                      (lead * lead + kLiquidStopTrailRatio * trail * trail);
            const double damping = m_bodyDamping * (1.0 - g) + m_headDamping * g;

            // t: 1 at the trailing edge, 0 at the leading face. The trailing edge bows back at its middle and
            // the trailing corners taper cross-axis; both follow t^2 so the side edges curve toward the tail.
            const double t = 0.5 * (1.0 - q);
            const double t2 = t * t;
            const double bow = t2 * (1.0 - kLiquidBowDepth * s * s);
            const double taper = lag * kLiquidTaperRatio * t2 * s;
            const Vec2 lagField{-(lag * bow * dirX + taper * perpX), -(lag * bow * dirY + taper * perpY)};

            const double gradX = lineSlope([&](unsigned k) { return div[gridIndex(k, y)]; }, x, spacing.x);
            const double gradY = lineSlope([&](unsigned k) { return div[gridIndex(x, k)]; }, y, spacing.y);

            const double coupledX = kLiquidShearStiffness * shearLaplacian(e, &Vec2::x, x, y, wx, wy) +
                                    shearDamping * shearLaplacian(v, &Vec2::x, x, y, wx, wy) + waveSpeed2 * gradX;
            const double coupledY = kLiquidShearStiffness * shearLaplacian(e, &Vec2::y, x, y, wx, wy) +
                                    shearDamping * shearLaplacian(v, &Vec2::y, x, y, wx, wy) + waveSpeed2 * gradY;

            a[i] = {-stiffness * (e[i].x - lagField.x) - damping * v[i].x + coupledX + m_inertia.x * kickWeight,
                    -stiffness * (e[i].y - lagField.y) - damping * v[i].y + coupledY + m_inertia.y * kickWeight};
        }
    }
    // The pseudo-force decays exponentially across each tick, so its shape does not depend on the tick length.
    const double inertiaDecay = std::exp(-kSubstepSeconds / kLiquidInertiaSeconds);
    m_inertia = {m_inertia.x * inertiaDecay, m_inertia.y * inertiaDecay};

    for (std::size_t i = 0; i < d.size(); ++i) {
        v[i].x += a[i].x * kSubstepSeconds;
        v[i].y += a[i].y * kSubstepSeconds;
        d[i].x += v[i].x * kSubstepSeconds;
        d[i].y += v[i].y * kSubstepSeconds;
    }
}

bool LiquidMesh::advance(double elapsedSeconds, Vec2 size) noexcept {
    if (!isValidSize(size) || !std::isfinite(elapsedSeconds) || elapsedSeconds > kGapResetSeconds ||
        (m_mode == MotionMode::Liquid && !hasFiniteParams(m_params))) {
        reset();
        return false;
    }

    // Window velocity is the ratio of exponentially weighted translation to weighted time, so irregular or
    // zero-delta frames average into the true speed instead of reading as a stop. Its change between ticks
    // gives the inertia pseudo-force: only deceleration or reversal along the previous motion is carried forward.
    if (m_mode == MotionMode::Liquid && elapsedSeconds > 0.0) {
        const double decay = std::exp(-elapsedSeconds / kLiquidVelocitySeconds);
        m_smoothDelta = {m_smoothDelta.x * decay + m_pendingDelta.x, m_smoothDelta.y * decay + m_pendingDelta.y};
        m_smoothTime = m_smoothTime * decay + elapsedSeconds;
        Vec2 velocity{m_smoothDelta.x / m_smoothTime, m_smoothDelta.y / m_smoothTime};
        if (!isFinite(velocity)) {
            reset();
            return false;
        }
        const double speed = std::hypot(velocity.x, velocity.y);
        const double cap = kLiquidMaxSpeedScale * std::min(size.x, size.y);
        if (speed > cap) {
            velocity.x *= cap / speed;
            velocity.y *= cap / speed;
        }
        // Slower velocity for the inertia: averaging over zero-delta frames keeps lumpy arrivals from reading as
        // deceleration, so only a real stop or reversal pushes the leading face.
        const double slowDecay = std::exp(-elapsedSeconds / kLiquidInertiaSeconds);
        m_slowDelta = {m_slowDelta.x * slowDecay + m_pendingDelta.x, m_slowDelta.y * slowDecay + m_pendingDelta.y};
        m_slowTime = m_slowTime * slowDecay + elapsedSeconds;
        const Vec2 slowVelocity{m_slowDelta.x / m_slowTime, m_slowDelta.y / m_slowTime};
        if (!isFinite(slowVelocity)) {
            reset();
            return false;
        }
        // Raw deceleration along the previous slow motion; zero on acceleration or steady drag.
        Vec2 rawDecel{};
        const double previousSlowSpeed = std::hypot(m_slowVelocity.x, m_slowVelocity.y);
        if (previousSlowSpeed > 0.0) {
            const double dirX = m_slowVelocity.x / previousSlowSpeed;
            const double dirY = m_slowVelocity.y / previousSlowSpeed;
            const double dvx = (slowVelocity.x - m_slowVelocity.x) / elapsedSeconds;
            const double dvy = (slowVelocity.y - m_slowVelocity.y) / elapsedSeconds;
            const double decel = -(dvx * dirX + dvy * dirY);
            if (decel > 0.0)
                rawDecel = {dirX * decel, dirY * decel};
        }
        m_slowVelocity = slowVelocity;
        // Tick-start rate of an exponential pseudo-force whose integral over this tick equals the velocity change:
        // R0 * tau * (1 - exp(-dt/tau)) = rawDecel * dt. Substeps decay it, so cadence does not change the push.
        const double tickShape = -std::expm1(-elapsedSeconds / kLiquidInertiaSeconds);
        const double tickGain = elapsedSeconds / (kLiquidInertiaSeconds * tickShape);
        m_inertia = {rawDecel.x * tickGain, rawDecel.y * tickGain};
        m_windowVelocity = velocity;
        m_pendingDelta = {};
    }

    // Applies the current geometry's bounds even when no substep runs.
    if (!constrain(size))
        return false;

    if (settled()) {
        m_accumulator = 0.0;
        m_quietSteps = 0;
        return false;
    }

    m_accumulator += std::max(0.0, std::min(elapsedSeconds, kMaxFrameSeconds));

    unsigned steps = 0;
    while (m_accumulator >= kSubstepSeconds - kAccumulatorTolerance) {
        if (steps == kMaxSubstepsPerAdvance) {
            m_accumulator = 0.0;
            break;
        }
        m_accumulator -= kSubstepSeconds;
        ++steps;

        if (m_mode == MotionMode::Liquid)
            integrateLiquidSubstep(size);
        else
            integrateWiggleSubstep(m_displacements, m_velocities, m_accelerations);

        if (!constrain(size))
            return false;

        const bool quiet = isQuiet(m_displacements, m_velocities);
        if (quiet) {
            if (++m_quietSteps >= kQuietSubsteps) {
                reset();
                return false;
            }
        } else {
            m_quietSteps = 0;
        }
    }

    return !settled();
}

bool LiquidMesh::settled() const noexcept {
    if (!isZero(m_displacements) || !isZero(m_velocities))
        return false;
    if (m_mode == MotionMode::Liquid)
        return isZero(m_windowVelocity) && isZero(m_pendingDelta);
    return true;
}

Vec2 LiquidMesh::sampleDisplacement(double u, double v) const noexcept {
    if (!std::isfinite(u) || !std::isfinite(v))
        return {};

    const double x = std::clamp(u, 0.0, 1.0) * kCells;
    const double y = std::clamp(v, 0.0, 1.0) * kCells;
    const int cells = static_cast<int>(kCells);
    const int ix = std::min(static_cast<int>(std::floor(x)), cells - 1);
    const int iy = std::min(static_cast<int>(std::floor(y)), cells - 1);
    const double tx = x - ix;
    const double ty = y - iy;

    // Node values with edge clamping, so the spline reproduces the boundary field.
    const auto node = [&](int i, int j) -> Vec2 {
        const unsigned ci = static_cast<unsigned>(std::clamp(i, 0, cells));
        const unsigned cj = static_cast<unsigned>(std::clamp(j, 0, cells));
        return m_displacements[gridIndex(ci, cj)];
    };

    // Interpolate along x for the four rows around the sample, then along y.
    std::array<Vec2, 4> column{};
    for (int k = 0; k < 4; ++k) {
        const int j = iy - 1 + k;
        const Vec2 p0 = node(ix - 1, j);
        const Vec2 p1 = node(ix, j);
        const Vec2 p2 = node(ix + 1, j);
        const Vec2 p3 = node(ix + 2, j);
        column[k] = {catmullRom(p0.x, p1.x, p2.x, p3.x, tx), catmullRom(p0.y, p1.y, p2.y, p3.y, tx)};
    }
    return {catmullRom(column[0].x, column[1].x, column[2].x, column[3].x, ty),
            catmullRom(column[0].y, column[1].y, column[2].y, column[3].y, ty)};
}

bool LiquidMesh::constrain(Vec2 size) noexcept {
    for (std::size_t i = 0; i < m_displacements.size(); ++i) {
        if (!isFinite(m_displacements[i]) || !isFinite(m_velocities[i])) {
            reset();
            return false;
        }
    }

    const double limit = m_mode == MotionMode::Liquid
        ? displacementLimit(size)
        : std::min(kWiggleMaxDisplacementLimit, kWiggleMaxDisplacementFraction * std::min(size.x, size.y));
    for (std::size_t i = 0; i < m_displacements.size(); ++i) {
        Vec2& d = m_displacements[i];
        Vec2& v = m_velocities[i];
        const double magnitude = std::hypot(d.x, d.y);

        if (m_mode == MotionMode::Liquid) {
            if (magnitude > 0.0)
                softSaturate(d, v, magnitude, limit);
            continue;
        }

        if (magnitude <= limit)
            continue;

        // magnitude > limit > 0 here, so the division is safe.
        const double nx = d.x / magnitude;
        const double ny = d.y / magnitude;
        d = {nx * limit, ny * limit};

        // Remove only the outward component so a clamped node cannot keep pushing past the bound.
        const double outward = v.x * nx + v.y * ny;
        if (outward > 0.0) {
            v.x -= outward * nx;
            v.y -= outward * ny;
        }
    }

    for (unsigned halvings = 0;; ++halvings) {
        if (validTriangles(size))
            return true;
        if (halvings == kMaxHalvings)
            break;
        for (std::size_t i = 0; i < m_displacements.size(); ++i) {
            m_displacements[i] = {m_displacements[i].x * 0.5, m_displacements[i].y * 0.5};
            m_velocities[i] = {m_velocities[i].x * 0.5, m_velocities[i].y * 0.5};
        }
    }

    reset();
    return false;
}

bool LiquidMesh::validTriangles(Vec2 size) const noexcept {
    for (unsigned cy = 0; cy < kCells; ++cy) {
        for (unsigned cx = 0; cx < kCells; ++cx) {
            for (unsigned which = 0; which < 2; ++which) {
                const auto corners = triangleCorners(cx, cy, which);
                std::array<Vec2, 3> rest{};
                std::array<Vec2, 3> deformed{};
                for (std::size_t k = 0; k < 3; ++k) {
                    rest[k] = restPosition(corners[k], size);
                    const Vec2 d = m_displacements[gridIndex(corners[k].x, corners[k].y)];
                    deformed[k] = {rest[k].x + d.x, rest[k].y + d.y};
                }

                // Rest orientation is the same sign for all triangles, so the ratio is positive
                // while orientation is kept and negative or zero after a foldover or collapse.
                const double restArea = signedArea(rest[0], rest[1], rest[2]);
                if (!std::isfinite(restArea) || restArea == 0.0)
                    return false;
                const double area = signedArea(deformed[0], deformed[1], deformed[2]);
                if (!(area / restArea >= kMinAreaRatio))
                    return false;
            }
        }
    }
    return true;
}

} // namespace liquid_glass
