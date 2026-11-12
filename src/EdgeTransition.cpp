#include "EdgeTransition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace liquid_glass {
namespace {

constexpr double kEdgeTolerance = 0.5; // px a body may overhang an edge and still use it
constexpr double kTieTolerance = 1.0;  // px
constexpr double kContactSlack = 2.0;  // px a neighbour may overlap a body side and still touch it
constexpr double kContactReach = 64.0; // px a facing gap may be and still count as contact

constexpr double kSubstepSeconds = 1.0 / 240.0;
constexpr double kAccumulatorTolerance = 1e-9;
constexpr double kGapSeconds = 0.25;
constexpr double kReferenceStrength = 0.65;
// Bead.
constexpr double kRadiusPerSide = 0.14, kRadiusMin = 20.0, kRadiusMax = 80.0;
constexpr double kRadiusFit = 0.95;     // r <= this fraction of the capture half-extents
constexpr double kHoverPerPinch = 1.25; // nh = r + kHoverPerPinch * Lp
// Hop: the bead centre rises from behind the edge to its hover point.
constexpr double kHopOmega = 22.0;                          // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
constexpr double kHopZetaStiff = 0.8, kHopZetaLoose = 0.45; // momentum 0 / 1
// Grow: the spreads take the bead to the window's extents (tangential first, then normal).
constexpr double kGrowOmega = 15.0;                           // rad/s at elasticity 0.5
constexpr double kGrowZetaStiff = 0.85, kGrowZetaLoose = 0.5; // momentum 0 / 1
constexpr double kGrowNormalRatio = 0.75;
// Black edge liquid, scaled with the bead.
constexpr double kTuckPerRadius = 0.35, kTuckMin = 5.0, kTuckMax = 20.0;
constexpr double kPinchPerRadius = 1.1, kPinchMin = 16.0, kPinchMax = 72.0;
constexpr double kNeckPerRadius = 0.9, kNeckMin = 12.0, kNeckMax = 64.0;
constexpr double kCrossSkirtPerTuck = 2.5;
// Recoil after the snap.
constexpr double kRecoilOmega = 28.0;
constexpr double kRecoilZetaStiff = 0.6, kRecoilZetaLoose = 0.2;
constexpr double kRecoilSkirtGrowth = 0.6, kRecoilSkirtSeconds = 0.06, kRecoilPlateauSeconds = 0.03;
// Snap fallbacks: hop at apex or at rest.
constexpr double kRestHopPixels = 0.5, kRestHopPixelsPerSecond = 5.0;
// Close.
constexpr double kCloseSeconds = 0.46;
constexpr double kShrinkEnd = 0.45; // normal shrink ends here (fraction of the close)
constexpr double kShrinkTangentStart = 0.05, kShrinkTangentSpan = 0.5;
constexpr double kDiveMargin = 2.0;      // px beyond the bead's far side when the close completes
constexpr double kSplashSeconds = 0.42;  // at elasticity 0.5; scaled by 2^(0.5 - elasticity)
constexpr double kSplashPeakX = 0.22;    // impulse peak in normalised time
constexpr double kSplashPerRadius = 0.9; // height at the peak, per radius
constexpr double kSplashSkirt = 0.55;    // skirt half-width per radius
// Settling thresholds.
constexpr double kQuietPixels = 0.15, kQuietPixelsPerSecond = 2.0;
constexpr double kQuietSpread = 1e-4,
                 kQuietSpreadPerSecond = 1e-3; // spreads are unitless; ~0.1 px on a 1000 px half-extent
// Renderer guard: keep this much of each triangle's map-only area.
constexpr double kMinAreaFraction = 0.2;
constexpr int    kGainHalvings = 8;
constexpr int    kGridSide = 33; // vertices per side (32 x 32 cells)

bool usable(const Box &b) {
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.w) && std::isfinite(b.h) && b.w > 0.0 &&
           b.h > 0.0;
}

bool runsHorizontally(Edge e) { return e == Edge::Top || e == Edge::Bottom; }

// Overlap of two 1-D spans; 0 when disjoint.
double overlapLength(double a0, double a1, double b0, double b1) {
    return std::max(0.0, std::min(a1, b1) - std::max(a0, b0));
}

Edge opposite(Edge e) {
    switch (e) {
    case Edge::Left:
        return Edge::Right;
    case Edge::Right:
        return Edge::Left;
    case Edge::Top:
        return Edge::Bottom;
    case Edge::Bottom:
        return Edge::Top;
    }
    return e;
}

// Length a body side shares with neighbour n when n sits just beyond that side; 0 when it does not touch.
double contactLength(const Box &body, Edge side, const Box &n) {
    double gap = 0.0;
    switch (side) {
    case Edge::Left:
        gap = body.x - (n.x + n.w);
        break;
    case Edge::Right:
        gap = n.x - (body.x + body.w);
        break;
    case Edge::Top:
        gap = body.y - (n.y + n.h);
        break;
    case Edge::Bottom:
        gap = n.y - (body.y + body.h);
        break;
    }
    if (gap < -kContactSlack || gap > kContactReach)
        return 0.0;
    return runsHorizontally(side) ? overlapLength(body.x, body.x + body.w, n.x, n.x + n.w)
                                  : overlapLength(body.y, body.y + body.h, n.y, n.y + n.h);
}

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double easeInOutCubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

// Splash impulse in normalised time x: peaks at 1 when x = kSplashPeakX, is 0 outside (0, 1) and ends at 0 at x = 1.
double splashImpulse(double x) {
    if (x <= 0.0 || x >= 1.0)
        return 0.0;
    const double a = x / kSplashPeakX;
    return a * std::exp(1.0 - a) * (1.0 - smooth(0.7, 1.0, x));
}

// Semi-implicit Euler toward `target`. Stable for omega * dt < 2; the largest omega used (recoil, ~40 rad/s) gives
// omega * dt ~ 0.17 at 240 Hz substeps.
template <typename S> void spring(S &s, double target, double omega, double zeta) {
    const double accel = -omega * omega * (s.value - target) - 2.0 * zeta * omega * s.velocity;
    s.velocity += accel * kSubstepSeconds;
    s.value += s.velocity * kSubstepSeconds;
}

bool validFrame(const EdgeFrame &f) {
    const double v[] = {f.edgeDistance, f.captureFar, f.bodyNear, f.bodyFar, f.bodyT0, f.bodyT1};
    for (double x : v) {
        if (!std::isfinite(x))
            return false;
    }
    return f.edgeDistance > 0.0 && f.captureFar > f.edgeDistance && f.bodyNear >= 0.0 && f.bodyFar > f.bodyNear &&
           f.bodyT1 > f.bodyT0;
}

// Radially homogeneous outline map in units of the capture half-extents. The rectangle outline (max(|x|,|y|) = 1)
// is carried onto the superellipse of exponent 2 / roundness; every ray from the centre maps onto itself, so the
// image cannot fold. roundness 0 is the identity (rectangle); roundness 1 is the circle.
Vec2 outline(Vec2 p, double roundness) {
    if (!(roundness >= 2e-4))
        return p; // exponent > 1e4 is indistinguishable from a rectangle
    const double n = 2.0 / roundness;
    const double ax = std::abs(p.x), ay = std::abs(p.y);
    const double rho = std::max(ax, ay);
    if (!(rho > 0.0))
        return p;
    const double x = ax / rho, y = ay / rho;                             // direction on the rectangle outline, max = 1
    const double g = std::pow(std::pow(x, n) + std::pow(y, n), 1.0 / n); // in [1, 2^(1/n)]
    return {p.x / g, p.y / g};
}

double triangleArea(Vec2 a, Vec2 b, Vec2 c) { return 0.5 * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)); }

} // namespace

Vec2 inwardNormal(Edge edge) noexcept {
    switch (edge) {
    case Edge::Left:
        return {1.0, 0.0};
    case Edge::Right:
        return {-1.0, 0.0};
    case Edge::Top:
        return {0.0, 1.0};
    case Edge::Bottom:
        return {0.0, -1.0};
    }
    return {0.0, -1.0};
}

Vec2 edgePoint(const EdgeFrame &f, double t, double n) noexcept {
    const double d = f.edgeDistance;
    switch (f.edge) {
    case Edge::Left:
        return {n - d, t};
    case Edge::Right:
        return {d - n, t};
    case Edge::Top:
        return {t, n - d};
    case Edge::Bottom:
        return {t, d - n};
    }
    return {t, d - n};
}

double edgeNormal(const EdgeFrame &f, Vec2 p) noexcept {
    const double d = f.edgeDistance;
    switch (f.edge) {
    case Edge::Left:
        return d + p.x;
    case Edge::Right:
        return d - p.x;
    case Edge::Top:
        return d + p.y;
    case Edge::Bottom:
        return d - p.y;
    }
    return d - p.y;
}

double edgeTangent(const EdgeFrame &f, Vec2 p) noexcept { return runsHorizontally(f.edge) ? p.x : p.y; }

std::optional<EdgeFrame> resolveEdgeFrame(const Box &body, const Box &capture, const Box &screen,
                                          std::span<const Box> neighbours, Edge fallback,
                                          bool preferFallback) noexcept {
    if (!usable(body) || !usable(capture) || !usable(screen))
        return std::nullopt;

    constexpr std::array<Edge, 4> kOrder{Edge::Bottom, Edge::Right, Edge::Left, Edge::Top};
    const auto                    slot = [](Edge e) -> std::size_t {
        switch (e) {
        case Edge::Left:
            return 0;
        case Edge::Right:
            return 1;
        case Edge::Top:
            return 2;
        case Edge::Bottom:
            return 3;
        }
        return 0;
    };
    const double right = screen.x + screen.w, bottom = screen.y + screen.h;
    const double cx = body.x + 0.5 * body.w, cy = body.y + 0.5 * body.h;
    const auto   gap = [&](Edge e) {
        switch (e) {
        case Edge::Left:
            return body.x - screen.x;
        case Edge::Right:
            return right - (body.x + body.w);
        case Edge::Top:
            return body.y - screen.y;
        case Edge::Bottom:
            return bottom - (body.y + body.h);
        }
        return -1.0;
    };
    const auto reach = [&](Edge e) {
        switch (e) {
        case Edge::Left:
            return cx - screen.x;
        case Edge::Right:
            return right - cx;
        case Edge::Top:
            return cy - screen.y;
        case Edge::Bottom:
            return bottom - cy;
        }
        return std::numeric_limits<double>::infinity();
    };
    const auto valid = [&](Edge e) { return gap(e) >= -kEdgeTolerance; };

    // Nearest valid edge among `allowed`; ties within kTieTolerance go to fallback, then kOrder.
    const auto nearestAmong = [&](const std::array<bool, 4> &allowed) -> std::optional<Edge> {
        const auto ok = [&](Edge e) { return allowed[slot(e)] && valid(e); };
        double     best = std::numeric_limits<double>::infinity();
        for (Edge e : kOrder)
            if (ok(e))
                best = std::min(best, reach(e));
        if (ok(fallback) && reach(fallback) <= best + kTieTolerance)
            return fallback;
        for (Edge e : kOrder)
            if (ok(e) && reach(e) <= best + kTieTolerance)
                return e;
        return std::nullopt;
    };

    // Contact: the body sides touching neighbours point the window at the opposite screen edges.
    std::array<double, 4> contact{};
    for (const Box &n : neighbours) {
        if (!usable(n))
            continue;
        for (Edge side : kOrder)
            contact[slot(side)] += contactLength(body, side, n);
    }
    const double        most = *std::max_element(contact.begin(), contact.end());
    const bool          anyContact = most > 0.0;
    std::array<bool, 4> candidate{};
    if (anyContact)
        for (Edge side : kOrder)
            if (contact[slot(side)] > 0.0 && contact[slot(side)] >= most - kTieTolerance)
                candidate[slot(opposite(side))] = true;

    std::optional<Edge> chosen;
    if (preferFallback && valid(fallback))
        chosen = fallback;
    else {
        if (anyContact)
            chosen = nearestAmong(candidate);
        if (!chosen)
            chosen = nearestAmong({true, true, true, true});
    }
    if (!chosen)
        return std::nullopt;

    const Edge   e = *chosen;
    const bool   hz = runsHorizontally(e);
    const double ccx = capture.x + 0.5 * capture.w, ccy = capture.y + 0.5 * capture.h;
    EdgeFrame    f;
    f.edge = e;
    switch (e) {
    case Edge::Left:
        f.edgeDistance = ccx - screen.x;
        break;
    case Edge::Right:
        f.edgeDistance = right - ccx;
        break;
    case Edge::Top:
        f.edgeDistance = ccy - screen.y;
        break;
    case Edge::Bottom:
        f.edgeDistance = bottom - ccy;
        break;
    }
    f.captureFar = f.edgeDistance + 0.5 * (hz ? capture.h : capture.w);
    f.bodyNear = std::max(0.0, gap(e));
    f.bodyFar = f.bodyNear + (hz ? body.h : body.w);
    f.bodyT0 = hz ? body.x - ccx : body.y - ccy;
    f.bodyT1 = f.bodyT0 + (hz ? body.w : body.h);
    return f;
}

void EdgeTransition::begin(Transition kind, const EdgeFrame &frame, const MaterialParams &params, double strength,
                           bool closeSplash) noexcept {
    const bool           continuing = kind == Transition::Close && m_phase == Phase::Opening;
    const EdgeTransition previous = *this;
    reset();
    const EdgeFrame &chosen = continuing ? previous.m_frame : frame;
    if (!validFrame(chosen) || !std::isfinite(params.elasticity) || !std::isfinite(params.momentum) ||
        !std::isfinite(params.viscosity) || !std::isfinite(strength) || strength <= 0.0)
        return;

    m_frame = chosen;
    const double e = std::clamp(params.elasticity, 0.0, 1.0);
    const double m = std::clamp(params.momentum, 0.0, 1.0);
    const double k = std::clamp(strength / kReferenceStrength, 0.25, 1.5);
    const double width = m_frame.bodyT1 - m_frame.bodyT0;
    const double depth = m_frame.bodyFar - m_frame.bodyNear;
    m_halfN = m_frame.captureFar - m_frame.edgeDistance;
    // The capture contains the body, so its tangential half-extent is at least width / 2; using that bound keeps
    // the bead inside both capture half-extents without needing the capture size here.
    const double fit = kRadiusFit * std::min(m_halfN, 0.5 * width);
    m_radius = std::min(std::clamp(kRadiusPerSide * std::min(width, depth), kRadiusMin, kRadiusMax) * k, fit);
    m_tuck = std::clamp(kTuckPerRadius * m_radius, kTuckMin, kTuckMax);
    m_pinch = std::clamp(kPinchPerRadius * m_radius, kPinchMin, kPinchMax);
    m_neckSkirt = std::clamp(kNeckPerRadius * m_radius, kNeckMin, kNeckMax);
    m_crossSkirt = kCrossSkirtPerTuck * m_tuck;
    m_hover = m_radius + kHoverPerPinch * m_pinch;
    m_recoilOmega = kRecoilOmega * std::exp2(e - 0.5);
    m_recoilZeta = lerp(kRecoilZetaStiff, kRecoilZetaLoose, m);
    m_growOmega = kGrowOmega * std::exp2(e - 0.5);
    m_growZeta = lerp(kGrowZetaStiff, kGrowZetaLoose, m);

    if (kind == Transition::Open) {
        m_hopOmega = kHopOmega * std::exp2(e - 0.5);
        m_hopZeta = lerp(kHopZetaStiff, kHopZetaLoose, m);
        m_hop = {-m_radius, 0.0}; // bead fully behind the edge
        m_nc = -m_radius;
        m_phase = Phase::Opening;
    } else {
        m_duration = kCloseSeconds * std::exp2(0.5 - e);
        m_closeMomentum = m;
        m_closeSplash = closeSplash;
        m_splashDuration = kSplashSeconds * std::exp2(0.5 - e);
        m_nc0 = continuing ? previous.m_nc : m_frame.edgeDistance;
        m_sT0 = continuing ? previous.m_spreadT.value : 1.0;
        m_sN0 = continuing ? previous.m_spreadN.value : 1.0;
        m_spreadT = {m_sT0, 0.0};
        m_spreadN = {m_sN0, 0.0};
        m_nc = m_nc0;
        if (continuing && previous.m_recoiling) {
            m_recoiling = true;
            m_recoil = previous.m_recoil;
            m_snap = previous.m_snap;
            m_recoilTime = previous.m_recoilTime;
        } else if (continuing && previous.m_profile.peak() > 0.0) {
            // Visible open liquid (neck or fillets) snaps into a recoil from its current profile.
            startRecoil(previous.m_profile.lobes[0]);
        }
        m_phase = Phase::Closing;
    }
    m_taken = m_nc;
    refreshProfile();
}

void EdgeTransition::reset() noexcept { *this = EdgeTransition{}; }

void EdgeTransition::abort() noexcept {
    const bool closing = m_phase == Phase::Closing;
    reset();
    if (closing)
        m_phase = Phase::Closed;
}

bool EdgeTransition::active() const noexcept { return m_phase == Phase::Opening || m_phase == Phase::Closing; }
bool EdgeTransition::closed() const noexcept { return m_phase == Phase::Closed; }

Vec2 EdgeTransition::beadCentre() const noexcept {
    if (!active())
        return {0.0, 0.0};
    const bool   hz = runsHorizontally(m_frame.edge);
    const Vec2   in = inwardNormal(m_frame.edge);
    const Vec2   along = hz ? Vec2{1.0, 0.0} : Vec2{0.0, 1.0};
    const double tc = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1) * (1.0 - m_spreadT.value);
    const double nc = m_nc - m_frame.edgeDistance;
    return {along.x * tc + in.x * nc, along.y * tc + in.y * nc};
}

Vec2 EdgeTransition::beadHalf(Vec2 restSize) const noexcept {
    const bool   hz = runsHorizontally(m_frame.edge);
    const double halfT = 0.5 * (hz ? restSize.x : restSize.y);
    const double halfN = 0.5 * (hz ? restSize.y : restSize.x);
    if (!active())
        return {0.5 * restSize.x, 0.5 * restSize.y};
    const double eT = m_radius + (halfT - m_radius) * m_spreadT.value;
    const double eN = m_radius + (halfN - m_radius) * m_spreadN.value;
    return hz ? Vec2{eT, eN} : Vec2{eN, eT};
}

Vec2 EdgeTransition::map(Vec2 fromCentre, Vec2 size) const noexcept {
    if (!active() || !std::isfinite(fromCentre.x) || !std::isfinite(fromCentre.y) || !std::isfinite(size.x) ||
        !std::isfinite(size.y) || size.x <= 0.0 || size.y <= 0.0)
        return fromCentre;

    const bool   hz = runsHorizontally(m_frame.edge);
    const Vec2   in = inwardNormal(m_frame.edge);
    const Vec2   along = hz ? Vec2{1.0, 0.0} : Vec2{0.0, 1.0};
    const double halfT = 0.5 * (hz ? size.x : size.y);
    const double halfN = 0.5 * (hz ? size.y : size.x);

    // Rest point in units of the capture half-extents.
    const double a = edgeTangent(m_frame, fromCentre) / halfT;
    const double b = (fromCentre.x * in.x + fromCentre.y * in.y) / halfN;

    const double sT = m_spreadT.value;
    const double sN = m_spreadN.value;
    const double roundness = std::pow(1.0 - std::clamp(std::min(sT, sN), 0.0, 1.0), 1.5);
    const Vec2   shape = outline({a, b}, roundness);

    const double centreT = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    const double tOut = centreT * (1.0 - sT) + (m_radius + (halfT - m_radius) * sT) * shape.x;
    // The normal extent comes from `size`, like the tangential one: the frame's capture is taken when the open begins,
    // before decorations (shadow) report their extents, and a stale extent leaves the window short of its rest box.
    const double nOut = (m_nc - m_frame.edgeDistance) + (m_radius + (halfN - m_radius) * sN) * shape.y;
    return {along.x * tOut + in.x * nOut, along.y * tOut + in.y * nOut};
}

double EdgeTransition::displacementGain(const LiquidMesh &mesh, Vec2 restSize, Vec2 compensation,
                                        bool boxCarriesPose) const noexcept {
    if (!active() || mesh.settled())
        return 1.0;
    if (!std::isfinite(restSize.x) || !std::isfinite(restSize.y) || !std::isfinite(compensation.x) ||
        !std::isfinite(compensation.y) || restSize.x <= 0.0 || restSize.y <= 0.0 || compensation.x <= 0.0 ||
        compensation.y <= 0.0)
        return 0.0;
    // Same composition as the renderer: the displayed sample is map(((u-0.5)*S + g*d) * c, S*c).
    const Vec2 scaled{restSize.x * compensation.x, restSize.y * compensation.y};

    constexpr int               kVertices = kGridSide * kGridSide;
    std::array<Vec2, kVertices> base{}, flow{}, mapOnly{}, moved{};
    for (int j = 0; j < kGridSide; ++j) {
        for (int i = 0; i < kGridSide; ++i) {
            const double u = static_cast<double>(i) / (kGridSide - 1);
            const double v = static_cast<double>(j) / (kGridSide - 1);
            const int    k = j * kGridSide + i;
            base[k] = {(u - 0.5) * restSize.x, (v - 0.5) * restSize.y};
            flow[k] = mesh.sampleDisplacement(u, v);
            mapOnly[k] =
                boxCarriesPose ? base[k] : map({base[k].x * compensation.x, base[k].y * compensation.y}, scaled);
        }
    }

    for (int h = 0; h <= kGainHalvings; ++h) {
        const double gain = std::ldexp(1.0, -h);
        for (int k = 0; k < kVertices; ++k)
            moved[k] = boxCarriesPose ? Vec2{base[k].x + gain * flow[k].x, base[k].y + gain * flow[k].y}
                                      : map({(base[k].x + gain * flow[k].x) * compensation.x,
                                             (base[k].y + gain * flow[k].y) * compensation.y},
                                            scaled);

        bool ok = true;
        for (int j = 0; ok && j + 1 < kGridSide; ++j) {
            for (int i = 0; ok && i + 1 < kGridSide; ++i) {
                const int p00 = j * kGridSide + i, p10 = p00 + 1, p01 = p00 + kGridSide, p11 = p01 + 1;
                const int tris[2][3] = {{p00, p10, p01}, {p10, p11, p01}};
                for (const auto &t : tris) {
                    const double before = triangleArea(mapOnly[t[0]], mapOnly[t[1]], mapOnly[t[2]]);
                    if (!(before > 0.0))
                        continue; // degenerate at rest: nothing to keep
                    const double after = triangleArea(moved[t[0]], moved[t[1]], moved[t[2]]);
                    if (!(after >= kMinAreaFraction * before)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (ok)
            return gain;
    }
    return 0.0;
}

Vec2 EdgeTransition::takeTravel() noexcept {
    const double delta = m_phase != Phase::Idle ? m_nc - m_taken : 0.0; // Closed keeps the final substep's travel
    m_taken = m_nc;
    const Vec2 n = inwardNormal(m_frame.edge);
    return {n.x * delta, n.y * delta};
}

double EdgeTransition::eNormal() const noexcept { return m_radius + (m_halfN - m_radius) * m_spreadN.value; }

double EdgeTransition::nearSide() const noexcept { return m_nc - eNormal(); }
double EdgeTransition::farSide() const noexcept { return m_nc + eNormal(); }

bool EdgeTransition::advance(double elapsedSeconds) noexcept {
    if (!active())
        return false;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || elapsedSeconds > kGapSeconds) {
        abort();
        return false;
    }
    m_accumulator += elapsedSeconds;
    while (active() && m_accumulator >= kSubstepSeconds - kAccumulatorTolerance) {
        m_accumulator -= kSubstepSeconds;
        step();
        refreshProfile(); // per substep, so a snap always starts from the state one substep earlier
    }
    return active();
}

bool EdgeTransition::quiet() const noexcept {
    const bool hopQuiet =
        std::abs(m_hop.value - m_hover) < kQuietPixels && std::abs(m_hop.velocity) < kQuietPixelsPerSecond;
    const bool growQuiet =
        std::abs(m_spreadT.value - 1.0) < kQuietSpread && std::abs(m_spreadT.velocity) < kQuietSpreadPerSecond &&
        std::abs(m_spreadN.value - 1.0) < kQuietSpread && std::abs(m_spreadN.velocity) < kQuietSpreadPerSecond;
    const bool liquidQuiet =
        m_recoiling && std::abs(m_recoil.value) < kQuietPixels && std::abs(m_recoil.velocity) < kQuietPixelsPerSecond;
    return m_growing && hopQuiet && growQuiet && liquidQuiet;
}

void EdgeTransition::step() noexcept {
    if (m_phase == Phase::Opening) {
        const double before = m_hop.velocity;
        spring(m_hop, m_hover, m_hopOmega, m_hopZeta);
        if (m_growing) {
            spring(m_spreadT, 1.0, m_growOmega, m_growZeta);
            spring(m_spreadN, 1.0, m_growOmega * kGrowNormalRatio, m_growZeta);
        }
        // The centre carries the normal spread's overshoot: it sits at the hop until the grow starts.
        m_nc = m_hop.value + (m_frame.edgeDistance - m_hover) * m_spreadN.value;
        if (m_recoiling)
            advanceRecoil();
        else if (nearSide() >= 0.0) {
            const bool apex = before > 0.0 && m_hop.velocity <= 0.0;
            const bool resting =
                std::abs(m_hop.value - m_hover) < kRestHopPixels && std::abs(m_hop.velocity) < kRestHopPixelsPerSecond;
            if (nearSide() >= m_pinch || apex || resting) {
                startRecoil(m_profile.lobes[0]); // m_profile still holds the attached profile from before this substep
                m_growing = true;
            }
        }
        if (quiet())
            reset();
        return;
    }

    if (!m_drained) {
        m_time += kSubstepSeconds;
        const double p = std::min(m_time / m_duration, 1.0);
        // Shrink: the pill folds back into a circle, normal first, then tangential.
        m_spreadN.value = m_sN0 * (1.0 - easeInOutCubic(p / kShrinkEnd));
        m_spreadT.value = m_sT0 * (1.0 - easeInOutCubic((p - kShrinkTangentStart) / kShrinkTangentSpan));
        // One continuous trajectory: shrinking must not stop the centre at a hover point.
        // Momentum advances the dive without introducing a second launch.
        const double square = p * p, cubic = square * p;
        const double dive = m_closeMomentum < 0.5 ? lerp(cubic, square, m_closeMomentum * 2.0)
                                                  : lerp(square, 2.0 * square - cubic, (m_closeMomentum - 0.5) * 2.0);
        m_nc = lerp(m_nc0, -m_radius - kDiveMargin, dive);
        if (m_recoiling)
            advanceRecoil(); // residual recoil from an interrupted opening keeps falling
        if (m_time < m_duration - kAccumulatorTolerance)
            return;
        m_drained = true;
        m_splashing = m_closeSplash; // the splash starts with the next substep
    } else {
        if (m_recoiling)
            advanceRecoil();
        if (m_splashing) {
            m_splashTime += kSubstepSeconds;
            m_splashing = m_splashTime < m_splashDuration - kAccumulatorTolerance;
        }
    }
    if (!m_splashing && recoilSettled())
        m_phase = Phase::Closed;
}

EdgeTransition::Lobe EdgeTransition::liveLobe() const noexcept {
    const double n = nearSide();
    if (n < 0.0) {
        // Crossing: fillets hug the bead where it passes through the edge. They ramp in as the bead's far side breaks
        // the surface.
        return {m_tuck * smooth(0.0, 2.0 * m_tuck, farSide()), m_radius, m_crossSkirt};
    }
    if (n <= m_pinch) {
        // Attached: the tip stays tucked under the near side while the contact patch necks down.
        const double q = smooth(0.0, m_pinch, n);
        return {n + m_tuck, m_radius * (1.0 - q), lerp(m_crossSkirt, m_neckSkirt, q)};
    }
    return {};
}

EdgeTransition::Lobe EdgeTransition::recoilLobe() const noexcept {
    const double t = m_recoilTime;
    return {std::max(0.0, m_recoil.value), m_snap.plateau * std::exp(-t / kRecoilPlateauSeconds),
            m_snap.skirt * (1.0 + kRecoilSkirtGrowth * (1.0 - std::exp(-t / kRecoilSkirtSeconds)))};
}

EdgeTransition::Lobe EdgeTransition::splashLobe() const noexcept {
    const double x = m_splashTime / m_splashDuration;
    return {kSplashPerRadius * m_radius * splashImpulse(x), 0.0, kSplashSkirt * m_radius};
}

void EdgeTransition::refreshProfile() noexcept {
    m_profile = {};
    if (!active())
        return;
    // Open liquid (attached neck or crossing fillets) until the snap; the recoil after a snap (or an interrupted open's
    // residual during a close); the splash once a close has drained.
    if (m_phase == Phase::Opening && !m_recoiling)
        m_profile.add(liveLobe());
    if (m_recoiling)
        m_profile.add(recoilLobe());
    if (m_phase == Phase::Closing && m_drained && m_splashing)
        m_profile.add(splashLobe());
}

void EdgeTransition::startRecoil(const Lobe &snap) noexcept {
    m_snap = snap;
    m_recoil = {snap.height, 0.0};
    m_recoilTime = 0.0;
    m_recoiling = true;
}

bool EdgeTransition::recoilSettled() const noexcept {
    return !m_recoiling ||
           (std::abs(m_recoil.value) < kQuietPixels && std::abs(m_recoil.velocity) < kQuietPixelsPerSecond);
}

void EdgeTransition::advanceRecoil() noexcept {
    m_recoilTime += kSubstepSeconds;
    spring(m_recoil, 0.0, m_recoilOmega, m_recoilZeta);
}

double EdgeTransition::meniscus(double t) const noexcept {
    const double c = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    double       h = 0.0;
    for (const Lobe &l : m_profile.lobes) {
        if (!(l.height > 0.0))
            continue;
        const double x = std::abs(t - c) - l.plateau;
        if (x <= 0.0) {
            h = std::max(h, l.height);
            continue;
        }
        const double u = x / l.skirt;
        if (u >= 1.0)
            continue;
        const double w = 1.0 - u * u;
        h = std::max(h, l.height * w * w); // tangent to the edge at the skirt's foot, like the bar's concave flanks
    }
    return h;
}

double EdgeTransition::meniscusLow() const noexcept {
    const double c = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    double       low = c;
    bool         any = false;
    for (const Lobe &l : m_profile.lobes) {
        if (!(l.height > 0.0))
            continue;
        const double lo = c - l.plateau - l.skirt;
        low = any ? std::min(low, lo) : lo;
        any = true;
    }
    return low;
}

double EdgeTransition::meniscusHigh() const noexcept {
    const double c = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    double       high = c;
    bool         any = false;
    for (const Lobe &l : m_profile.lobes) {
        if (!(l.height > 0.0))
            continue;
        const double hi = c + l.plateau + l.skirt;
        high = any ? std::max(high, hi) : hi;
        any = true;
    }
    return high;
}

double EdgeTransition::meniscusPeak() const noexcept { return m_profile.peak(); }

} // namespace liquid_glass
