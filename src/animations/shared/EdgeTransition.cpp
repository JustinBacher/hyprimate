#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"

namespace hyprimate {
    namespace {

        constexpr double kEdgeTolerance = 0.5;  // px a body may overhang an edge and still use it
        constexpr double kTieTolerance = 1.0;  // px
        constexpr double kContactSlack =
            2.0;  // px a neighbour may overlap a body side and still touch it
        constexpr double kContactReach = 64.0;  // px a facing gap may be and still count as contact

        constexpr double kAccumulatorTolerance = 1e-9;
        constexpr double kReferenceStrength = 0.65;
        // Bead.
        constexpr double kRadiusPerSide = 0.14, kRadiusMin = 20.0, kRadiusMax = 80.0;
        constexpr double kRadiusFit = 0.95;  // r <= this fraction of the capture half-extents
        constexpr double kHoverPerPinch = 1.25;  // nh = r + kHoverPerPinch * Lp
        // Hop: the bead centre rises from behind the edge to its hover point.
        constexpr double kHopOmega =
            22.0;  // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
        constexpr double kHopZetaStiff = 0.8, kHopZetaLoose = 0.45;  // momentum 0 / 1
        // Grow: the spreads take the bead to the window's extents (tangential first, then normal).
        constexpr double kGrowOmega = 15.0;  // rad/s at elasticity 0.5
        constexpr double kGrowZetaStiff = 0.85, kGrowZetaLoose = 0.5;  // momentum 0 / 1
        constexpr double kGrowNormalRatio = 0.75;
        // Black edge liquid, scaled with the bead.
        constexpr double kTuckPerRadius = 0.35, kTuckMin = 5.0, kTuckMax = 20.0;
        constexpr double kPinchPerRadius = 1.1, kPinchMin = 16.0, kPinchMax = 72.0;
        constexpr double kNeckPerRadius = 0.9, kNeckMin = 12.0, kNeckMax = 64.0;
        constexpr double kCrossSkirtPerTuck = 2.5;
        // Recoil after the snap.
        constexpr double kRecoilOmega = 28.0;
        constexpr double kRecoilZetaStiff = 0.6, kRecoilZetaLoose = 0.2;
        constexpr double kRecoilSkirtGrowth = 0.6, kRecoilSkirtSeconds = 0.06,
                         kRecoilPlateauSeconds = 0.03;
        // Snap fallbacks: hop at apex or at rest.
        constexpr double kRestHopPixels = 0.5, kRestHopPixelsPerSecond = 5.0;
        // Close.
        constexpr double kCloseSeconds = 0.46;
        constexpr double kShrinkEnd = 0.45;  // normal shrink ends here (fraction of the close)
        constexpr double kShrinkTangentStart = 0.05, kShrinkTangentSpan = 0.5;
        constexpr double kDiveMargin =
            2.0;  // px beyond the bead's far side when the close completes
        constexpr double kSplashSeconds =
            0.42;  // at elasticity 0.5; scaled by 2^(0.5 - elasticity)
        constexpr double kSplashPeakX = 0.22;  // impulse peak in normalised time
        constexpr double kSplashPerRadius = 0.9;  // height at the peak, per radius
        constexpr double kSplashSkirt = 0.55;  // skirt half-width per radius
        // Settling thresholds.
        constexpr double kQuietPixels = 0.15, kQuietPixelsPerSecond = 2.0;
        constexpr double kQuietSpread = 1e-4,
                         kQuietSpreadPerSecond =
                             1e-3;  // spreads are unitless; ~0.1 px on a 1000 px half-extent

        bool runsHorizontally(Edge e) {
            return e == Edge::Top || e == Edge::Bottom;
        }

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

        // Length a body side shares with neighbour n when n sits just beyond that side; 0 when it
        // does not touch.
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
            if (gap < -kContactSlack || gap > kContactReach) {
                return 0.0;
            }
            return runsHorizontally(side) ? overlapLength(body.x, body.x + body.w, n.x, n.x + n.w)
                                          : overlapLength(body.y, body.y + body.h, n.y, n.y + n.h);
        }

        double lerp(double a, double b, double t) {
            return a + ((b - a) * t);
        }

        double easeInOutCubic(double t) {
            t = std::clamp(t, 0.0, 1.0);
            return t < 0.5 ? 4.0 * t * t * t : 1.0 - (std::pow((-2.0 * t) + 2.0, 3.0) / 2.0);
        }

        // Splash impulse in normalised time x: peaks at 1 when x = kSplashPeakX, is 0 outside (0,
        // 1) and ends at 0 at x = 1.
        double splashImpulse(double x) {
            if (x <= 0.0 || x >= 1.0) {
                return 0.0;
            }
            const double PEAK_RATIO = x / kSplashPeakX;
            return PEAK_RATIO * std::exp(1.0 - PEAK_RATIO) * (1.0 - smoothstep(0.7, 1.0, x));
        }

        // Semi-implicit Euler toward `target`. Stable for omega * dt < 2; the largest omega used
        // (recoil, ~40 rad/s) gives omega * dt ~ 0.17 at 240 Hz substeps.
        template <typename S>
        void spring(S &s, double target, double omega, double zeta) {
            const double ACCEL =
                (-omega * omega * (s.value - target)) - (2.0 * zeta * omega * s.velocity);
            s.velocity += ACCEL * SUBSTEP_SECONDS;
            s.value += s.velocity * SUBSTEP_SECONDS;
        }

        bool validFrame(const EdgeFrame &f) {
            const double VALUES[] = {
                f.edgeDistance, f.captureFar, f.bodyNear, f.bodyFar, f.bodyT0, f.bodyT1};
            for (const double VALUE : VALUES) {
                if (!std::isfinite(VALUE)) {
                    return false;
                }
            }
            return f.edgeDistance > 0.0 && f.captureFar > f.edgeDistance && f.bodyNear >= 0.0 &&
                   f.bodyFar > f.bodyNear && f.bodyT1 > f.bodyT0;
        }

        // Radially homogeneous outline map in units of the capture half-extents. The rectangle
        // outline (max(|x|,|y|) = 1) is carried onto the superellipse of exponent n = 2 /
        // roundness; every ray from the centre maps onto itself, so the image cannot fold. `invN`
        // is 1/n. Callers pass the rectangle through unchanged (roundness below 2e-4).
        Vec2 outline(Vec2 p, double n, double invN) {
            const double ABS_X = std::abs(p.x);
            const double ABS_Y = std::abs(p.y);
            const double RHO = std::max(ABS_X, ABS_Y);
            if (!(RHO > 0.0)) {
                return p;
            }
            const double DIR_X = ABS_X / RHO;
            const double DIR_Y = ABS_Y / RHO;  // direction on the rectangle outline, max = 1
            const double OUTLINE_SCALE =
                std::pow(std::pow(DIR_X, n) + std::pow(DIR_Y, n), invN);  // in [1, 2^(1/n)]
            return {.x = p.x / OUTLINE_SCALE, .y = p.y / OUTLINE_SCALE};
        }

    }  // namespace

    std::optional<EdgeFrame> resolveCloseEdgeFrame(const Box &body,
        const Box &capture,
        const Box &bounds,
        std::span<const Box> neighbours,
        Edge fallback,
        CloseSide side,
        std::optional<Edge> origin,
        bool lastWindow) noexcept {
        bool prefer = true;
        switch (side) {
            case CloseSide::Origin:
                prefer = origin.has_value();
                if (origin) {
                    fallback = *origin;
                }
                break;
            case CloseSide::Top:
                fallback = Edge::Top;
                break;
            case CloseSide::Bottom:
                fallback = Edge::Bottom;
                break;
            case CloseSide::Left:
                fallback = Edge::Left;
                break;
            case CloseSide::Right:
                fallback = Edge::Right;
                break;
            case CloseSide::Auto:
                prefer = lastWindow;
                break;
            case CloseSide::Nearest:
                prefer = false;
                break;
        }
        return resolveEdgeFrame(body,
            capture,
            bounds,
            side == CloseSide::Nearest ? std::span<const Box>{} : neighbours,
            fallback,
            prefer);
    }

    EdgeSet resolveEdgeSides(
        const Box &body, const Box &screen, std::span<const Box> neighbours) noexcept {
        EdgeSet sides;
        if (!is_usable(body) || !is_usable(screen)) {
            return sides;
        }

        // Indexed by the Edge value.
        constexpr std::array<Edge, 4> K_EDGES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
        const auto SLOT = [](Edge e) { return static_cast<std::size_t>(e); };
        std::array<double, 4> contact{};
        for (const Box &adjacent : neighbours) {
            if (!is_usable(adjacent)) {
                continue;
            }
            for (const Edge SIDE : K_EDGES) {
                contact[SLOT(SIDE)] += contactLength(body, SIDE, adjacent);
            }
        }
        const double MOST = *std::max_element(contact.begin(), contact.end());
        const auto FREE = [&](Edge e) { return !(contact[SLOT(e)] > 0.0); };
        const double BODY_CENTRE_X = body.x + (0.5 * body.w);
        const double BODY_CENTRE_Y = body.y + (0.5 * body.h);
        const std::array<double, 4> REACH{BODY_CENTRE_X - screen.x,
            screen.x + screen.w - BODY_CENTRE_X,
            BODY_CENTRE_Y - screen.y,
            screen.y + screen.h - BODY_CENTRE_Y};

        if (MOST > 0.0) {
            for (const Edge SIDE : K_EDGES) {
                if (contact[SLOT(SIDE)] >= MOST - kTieTolerance && FREE(opposite(SIDE))) {
                    sides.add(opposite(SIDE));
                }
            }
            if (!sides.empty()) {
                return sides;
            }
        }

        // Nearest edges among the free ones; among all four when every side is touched.
        const bool ANY_FREE = std::ranges::any_of(K_EDGES, FREE);
        const auto ALLOWED = [&](Edge e) { return !ANY_FREE || FREE(e); };
        double best = std::numeric_limits<double>::infinity();
        for (const Edge EDGE_OPTION : K_EDGES) {
            if (ALLOWED(EDGE_OPTION)) {
                best = std::min(best, REACH[SLOT(EDGE_OPTION)]);
            }
        }
        for (const Edge EDGE : K_EDGES) {
            if (ALLOWED(EDGE) && REACH[SLOT(EDGE)] <= best + kTieTolerance) {
                sides.add(EDGE);
            }
        }
        return sides;
    }

    Vec2 inwardNormal(Edge edge) noexcept {
        switch (edge) {
            case Edge::Left:
                return {.x = 1.0, .y = 0.0};
            case Edge::Right:
                return {.x = -1.0, .y = 0.0};
            case Edge::Top:
                return {.x = 0.0, .y = 1.0};
            case Edge::Bottom:
                return {.x = 0.0, .y = -1.0};
        }
        return {.x = 0.0, .y = -1.0};
    }

    Vec2 edgePoint(const EdgeFrame &f, double t, double n) noexcept {
        const double EDGE_DISTANCE = f.edgeDistance;
        switch (f.edge) {
            case Edge::Left:
                return {.x = n - EDGE_DISTANCE, .y = t};
            case Edge::Right:
                return {.x = EDGE_DISTANCE - n, .y = t};
            case Edge::Top:
                return {.x = t, .y = n - EDGE_DISTANCE};
            case Edge::Bottom:
                return {.x = t, .y = EDGE_DISTANCE - n};
        }
        return {.x = t, .y = EDGE_DISTANCE - n};
    }

    double edgeNormal(const EdgeFrame &f, Vec2 p) noexcept {
        const double EDGE_DISTANCE = f.edgeDistance;
        switch (f.edge) {
            case Edge::Left:
                return EDGE_DISTANCE + p.x;
            case Edge::Right:
                return EDGE_DISTANCE - p.x;
            case Edge::Top:
                return EDGE_DISTANCE + p.y;
            case Edge::Bottom:
                return EDGE_DISTANCE - p.y;
        }
        return EDGE_DISTANCE - p.y;
    }

    double edgeTangent(const EdgeFrame &f, Vec2 p) noexcept {
        return runsHorizontally(f.edge) ? p.x : p.y;
    }

    std::optional<EdgeFrame> resolveEdgeFrame(const Box &body,
        const Box &capture,
        const Box &screen,
        std::span<const Box> neighbours,
        Edge fallback,
        bool preferFallback) noexcept {
        if (!is_usable(body) || !is_usable(capture) || !is_usable(screen)) {
            return std::nullopt;
        }

        constexpr std::array<Edge, 4> K_ORDER{Edge::Bottom, Edge::Right, Edge::Left, Edge::Top};
        const auto SLOT = [](Edge e) -> std::size_t {
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
        const double RIGHT = screen.x + screen.w;
        const double BOTTOM = screen.y + screen.h;
        const double CENTRE_X = body.x + (0.5 * body.w);
        const double CENTRE_Y = body.y + (0.5 * body.h);
        const auto GAP = [&](Edge e) {
            switch (e) {
                case Edge::Left:
                    return body.x - screen.x;
                case Edge::Right:
                    return RIGHT - (body.x + body.w);
                case Edge::Top:
                    return body.y - screen.y;
                case Edge::Bottom:
                    return BOTTOM - (body.y + body.h);
            }
            return -1.0;
        };
        const auto REACH = [&](Edge e) {
            switch (e) {
                case Edge::Left:
                    return CENTRE_X - screen.x;
                case Edge::Right:
                    return RIGHT - CENTRE_X;
                case Edge::Top:
                    return CENTRE_Y - screen.y;
                case Edge::Bottom:
                    return BOTTOM - CENTRE_Y;
            }
            return std::numeric_limits<double>::infinity();
        };
        const auto VALID = [&](Edge e) { return GAP(e) >= -kEdgeTolerance; };

        // Nearest valid edge among `allowed`; ties within kTieTolerance go to fallback, then
        // kOrder.
        const auto NEAREST_AMONG = [&](const std::array<bool, 4> &allowed) -> std::optional<Edge> {
            const auto IS_ALLOWED = [&](Edge e) { return allowed[SLOT(e)] && VALID(e); };
            double best = std::numeric_limits<double>::infinity();
            for (const Edge CANDIDATE : K_ORDER) {
                if (IS_ALLOWED(CANDIDATE)) {
                    best = std::min(best, REACH(CANDIDATE));
                }
            }
            if (IS_ALLOWED(fallback) && REACH(fallback) <= best + kTieTolerance) {
                return fallback;
            }
            for (Edge edge_option : K_ORDER) {
                if (IS_ALLOWED(edge_option) && REACH(edge_option) <= best + kTieTolerance) {
                    return edge_option;
                }
            }
            return std::nullopt;
        };

        // Contact: the body sides touching neighbours point the window at the opposite screen
        // edges.
        std::array<double, 4> contact{};
        for (const Box &neighbour : neighbours) {
            if (!is_usable(neighbour)) {
                continue;
            }
            for (const Edge SIDE : K_ORDER) {
                contact[SLOT(SIDE)] += contactLength(body, SIDE, neighbour);
            }
        }
        const double MOST = *std::max_element(contact.begin(), contact.end());
        const bool ANY_CONTACT = MOST > 0.0;
        std::array<bool, 4> candidate{};
        if (ANY_CONTACT) {
            for (const Edge SIDE : K_ORDER) {
                if (contact[SLOT(SIDE)] > 0.0 && contact[SLOT(SIDE)] >= MOST - kTieTolerance) {
                    candidate[SLOT(opposite(SIDE))] = true;
                }
            }
        }

        std::optional<Edge> chosen;
        if (preferFallback && VALID(fallback)) {
            chosen = fallback;
        } else {
            if (ANY_CONTACT) {
                chosen = NEAREST_AMONG(candidate);
            }
            if (!chosen) {
                chosen = NEAREST_AMONG({true, true, true, true});
            }
        }
        if (!chosen) {
            return std::nullopt;
        }

        const Edge EDGE = *chosen;
        const bool HORIZONTAL = runsHorizontally(EDGE);
        const double CCX = capture.x + (0.5 * capture.w);
        const double CCY = capture.y + (0.5 * capture.h);
        EdgeFrame frame;
        frame.edge = EDGE;
        switch (EDGE) {
            case Edge::Left:
                frame.edgeDistance = CCX - screen.x;
                break;
            case Edge::Right:
                frame.edgeDistance = RIGHT - CCX;
                break;
            case Edge::Top:
                frame.edgeDistance = CCY - screen.y;
                break;
            case Edge::Bottom:
                frame.edgeDistance = BOTTOM - CCY;
                break;
        }
        frame.captureFar = frame.edgeDistance + (0.5 * (HORIZONTAL ? capture.h : capture.w));
        frame.bodyNear = std::max(0.0, GAP(EDGE));
        frame.bodyFar = frame.bodyNear + (HORIZONTAL ? body.h : body.w);
        frame.bodyT0 = HORIZONTAL ? body.x - CCX : body.y - CCY;
        frame.bodyT1 = frame.bodyT0 + (HORIZONTAL ? body.w : body.h);
        return frame;
    }

    void EdgeTransition::begin(Transition kind,
        const EdgeFrame &frame,
        const TransitionParams &params,
        double strength,
        bool closeSplash) noexcept {
        const bool CONTINUING = kind == Transition::Close && m_phase == Phase::Opening;
        const EdgeTransition PREVIOUS = *this;
        reset();
        const EdgeFrame &chosen = CONTINUING ? PREVIOUS.m_frame : frame;
        if (!validFrame(chosen) || !std::isfinite(params.elasticity) ||
            !std::isfinite(params.momentum) || !std::isfinite(strength) || strength <= 0.0) {
            return;
        }

        m_frame = chosen;
        const double ELASTICITY = std::clamp(params.elasticity, 0.0, 1.0);
        const double MOMENTUM = std::clamp(params.momentum, 0.0, 1.0);
        const double STRENGTH_SCALE = std::clamp(strength / kReferenceStrength, 0.25, 1.5);
        const double WIDTH = m_frame.bodyT1 - m_frame.bodyT0;
        const double DEPTH = m_frame.bodyFar - m_frame.bodyNear;
        m_halfN = m_frame.captureFar - m_frame.edgeDistance;
        // The capture contains the body, so its tangential half-extent is at least width / 2; using
        // that bound keeps the bead inside both capture half-extents without needing the capture
        // size here.
        const double FIT = kRadiusFit * std::min(m_halfN, 0.5 * WIDTH);
        m_radius =
            std::min(std::clamp(kRadiusPerSide * std::min(WIDTH, DEPTH), kRadiusMin, kRadiusMax) *
                         STRENGTH_SCALE,
                FIT);
        m_tuck = std::clamp(kTuckPerRadius * m_radius, kTuckMin, kTuckMax);
        m_pinch = std::clamp(kPinchPerRadius * m_radius, kPinchMin, kPinchMax);
        m_neckSkirt = std::clamp(kNeckPerRadius * m_radius, kNeckMin, kNeckMax);
        m_crossSkirt = kCrossSkirtPerTuck * m_tuck;
        m_hover = m_radius + (kHoverPerPinch * m_pinch);
        m_recoilOmega = kRecoilOmega * std::exp2(ELASTICITY - 0.5);
        m_recoilZeta = lerp(kRecoilZetaStiff, kRecoilZetaLoose, MOMENTUM);
        m_growOmega = kGrowOmega * std::exp2(ELASTICITY - 0.5);
        m_growZeta = lerp(kGrowZetaStiff, kGrowZetaLoose, MOMENTUM);

        if (kind == Transition::Open) {
            m_hopOmega = kHopOmega * std::exp2(ELASTICITY - 0.5);
            m_hopZeta = lerp(kHopZetaStiff, kHopZetaLoose, MOMENTUM);
            m_hop = {.value = -m_radius, .velocity = 0.0};  // bead fully behind the edge
            m_nc = -m_radius;
            m_phase = Phase::Opening;
        } else {
            m_duration = kCloseSeconds * std::exp2(0.5 - ELASTICITY);
            m_closeMomentum = MOMENTUM;
            m_closeSplash = closeSplash;
            m_splashDuration = kSplashSeconds * std::exp2(0.5 - ELASTICITY);
            m_nc0 = CONTINUING ? PREVIOUS.m_nc : m_frame.edgeDistance;
            m_sT0 = CONTINUING ? PREVIOUS.m_spreadT.value : 1.0;
            m_sN0 = CONTINUING ? PREVIOUS.m_spreadN.value : 1.0;
            m_spreadT = {.value = m_sT0, .velocity = 0.0};
            m_spreadN = {.value = m_sN0, .velocity = 0.0};
            m_nc = m_nc0;
            if (CONTINUING && PREVIOUS.m_recoiling) {
                m_recoiling = true;
                m_recoil = PREVIOUS.m_recoil;
                m_snap = PREVIOUS.m_snap;
                m_recoilTime = PREVIOUS.m_recoilTime;
            } else if (CONTINUING && PREVIOUS.m_profile.peak() > 0.0) {
                // Visible open liquid (neck or fillets) snaps into a recoil from its current
                // profile.
                startRecoil(PREVIOUS.m_profile.lobes[0]);
            }
            m_phase = Phase::Closing;
        }
        m_taken = m_nc;
        refreshProfile();
    }

    void EdgeTransition::reset() noexcept {
        *this = EdgeTransition{};
    }

    void EdgeTransition::abort() noexcept {
        const bool CLOSING = m_phase == Phase::Closing;
        reset();
        if (CLOSING) {
            m_phase = Phase::Closed;
        }
    }

    bool EdgeTransition::active() const noexcept {
        return m_phase == Phase::Opening || m_phase == Phase::Closing;
    }
    bool EdgeTransition::closed() const noexcept {
        return m_phase == Phase::Closed;
    }

    Vec2 EdgeTransition::beadCentre() const noexcept {
        if (!active()) {
            return {.x = 0.0, .y = 0.0};
        }
        const bool HORIZONTAL = runsHorizontally(m_frame.edge);
        const Vec2 INWARD = inwardNormal(m_frame.edge);
        const Vec2 ALONG = HORIZONTAL ? Vec2{.x = 1.0, .y = 0.0} : Vec2{.x = 0.0, .y = 1.0};
        const double TANGENT_CENTRE =
            0.5 * (m_frame.bodyT0 + m_frame.bodyT1) * (1.0 - m_spreadT.value);
        const double NORMAL_CENTRE = m_nc - m_frame.edgeDistance;
        return {.x = (ALONG.x * TANGENT_CENTRE) + (INWARD.x * NORMAL_CENTRE),
            .y = (ALONG.y * TANGENT_CENTRE) + (INWARD.y * NORMAL_CENTRE)};
    }

    Vec2 EdgeTransition::beadHalf(Vec2 restSize) const noexcept {
        const bool HORIZONTAL = runsHorizontally(m_frame.edge);
        const double HALF_T = 0.5 * (HORIZONTAL ? restSize.x : restSize.y);
        const double HALF_N = 0.5 * (HORIZONTAL ? restSize.y : restSize.x);
        if (!active()) {
            return {.x = 0.5 * restSize.x, .y = 0.5 * restSize.y};
        }
        const double EXTENT_T = m_radius + ((HALF_T - m_radius) * m_spreadT.value);
        const double EXTENT_N = m_radius + ((HALF_N - m_radius) * m_spreadN.value);
        return HORIZONTAL ? Vec2{.x = EXTENT_T, .y = EXTENT_N} : Vec2{.x = EXTENT_N, .y = EXTENT_T};
    }

    EdgeTransition::RestMap EdgeTransition::restMap(Vec2 size) const noexcept {
        RestMap mapping;
        if (!active() || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0.0 ||
            size.y <= 0.0) {
            return mapping;
        }
        mapping.identity = false;
        const bool HORIZONTAL = runsHorizontally(m_frame.edge);
        mapping.horizontal = HORIZONTAL;
        mapping.along = HORIZONTAL ? Vec2{.x = 1.0, .y = 0.0} : Vec2{.x = 0.0, .y = 1.0};
        mapping.inward = inwardNormal(m_frame.edge);
        mapping.halfT = 0.5 * (HORIZONTAL ? size.x : size.y);
        mapping.halfN = 0.5 * (HORIZONTAL ? size.y : size.x);

        const double SPREAD_T = m_spreadT.value;
        const double SPREAD_N = m_spreadN.value;
        const double ROUNDNESS =
            std::pow(1.0 - std::clamp(std::min(SPREAD_T, SPREAD_N), 0.0, 1.0), 1.5);
        mapping.rectangle =
            !(ROUNDNESS >= 2e-4);  // exponent > 1e4 is indistinguishable from a rectangle
        if (!mapping.rectangle) {
            mapping.exponent = 2.0 / ROUNDNESS;
            mapping.invExponent = 1.0 / mapping.exponent;
        }

        const double CENTRE_T = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
        mapping.tBase = CENTRE_T * (1.0 - SPREAD_T);
        mapping.tScale = m_radius + ((mapping.halfT - m_radius) * SPREAD_T);
        // The normal extent comes from `size`, like the tangential one: the frame's capture is
        // taken when the open begins, before decorations (shadow) report their extents, and a stale
        // extent leaves the window short of its rest box.
        mapping.nBase = m_nc - m_frame.edgeDistance;
        mapping.nScale = m_radius + ((mapping.halfN - m_radius) * SPREAD_N);
        return mapping;
    }

    Vec2 EdgeTransition::RestMap::map(Vec2 fromCentre) const noexcept {
        if (identity || !std::isfinite(fromCentre.x) || !std::isfinite(fromCentre.y)) {
            return fromCentre;
        }
        // Rest point in units of the capture half-extents.
        const double REST_T = (horizontal ? fromCentre.x : fromCentre.y) / halfT;
        const double REST_N = ((fromCentre.x * inward.x) + (fromCentre.y * inward.y)) / halfN;
        const Vec2 SHAPE = rectangle ? Vec2{.x = REST_T, .y = REST_N}
                                     : outline({.x = REST_T, .y = REST_N}, exponent, invExponent);
        const double T_OUT = tBase + (tScale * SHAPE.x);
        const double N_OUT = nBase + (nScale * SHAPE.y);
        return {.x = (along.x * T_OUT) + (inward.x * N_OUT),
            .y = (along.y * T_OUT) + (inward.y * N_OUT)};
    }

    Vec2 EdgeTransition::map(Vec2 fromCentre, Vec2 size) const noexcept {
        return restMap(size).map(fromCentre);
    }

    Vec2 EdgeTransition::takeTravel() noexcept {
        const double DELTA = m_phase != Phase::Idle
                                 ? m_nc - m_taken
                                 : 0.0;  // Closed keeps the final substep's travel
        m_taken = m_nc;
        const Vec2 INWARD = inwardNormal(m_frame.edge);
        return {.x = INWARD.x * DELTA, .y = INWARD.y * DELTA};
    }

    double EdgeTransition::eNormal() const noexcept {
        return m_radius + ((m_halfN - m_radius) * m_spreadN.value);
    }

    double EdgeTransition::nearSide() const noexcept {
        return m_nc - eNormal();
    }
    double EdgeTransition::farSide() const noexcept {
        return m_nc + eNormal();
    }

    bool EdgeTransition::advance(double elapsedSeconds) noexcept {
        if (!active()) {
            return false;
        }
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
            elapsedSeconds > STALL_SECONDS) {
            abort();
            return false;
        }
        m_accumulator += elapsedSeconds;
        while (active() && m_accumulator >= SUBSTEP_SECONDS - kAccumulatorTolerance) {
            m_accumulator -= SUBSTEP_SECONDS;
            step();
            refreshProfile();  // per substep, so a snap always starts from the state one substep
                               // earlier
        }
        return active();
    }

    bool EdgeTransition::quiet() const noexcept {
        const bool HOP_QUIET = std::abs(m_hop.value - m_hover) < kQuietPixels &&
                               std::abs(m_hop.velocity) < kQuietPixelsPerSecond;
        const bool GROW_QUIET = std::abs(m_spreadT.value - 1.0) < kQuietSpread &&
                                std::abs(m_spreadT.velocity) < kQuietSpreadPerSecond &&
                                std::abs(m_spreadN.value - 1.0) < kQuietSpread &&
                                std::abs(m_spreadN.velocity) < kQuietSpreadPerSecond;
        const bool LIQUID_QUIET = m_recoiling && std::abs(m_recoil.value) < kQuietPixels &&
                                  std::abs(m_recoil.velocity) < kQuietPixelsPerSecond;
        return m_growing && HOP_QUIET && GROW_QUIET && LIQUID_QUIET;
    }

    void EdgeTransition::step() noexcept {
        if (m_phase == Phase::Opening) {
            const double BEFORE = m_hop.velocity;
            spring(m_hop, m_hover, m_hopOmega, m_hopZeta);
            if (m_growing) {
                spring(m_spreadT, 1.0, m_growOmega, m_growZeta);
                spring(m_spreadN, 1.0, m_growOmega * kGrowNormalRatio, m_growZeta);
            }
            // The centre carries the normal spread's overshoot: it sits at the hop until the grow
            // starts.
            m_nc = m_hop.value + ((m_frame.edgeDistance - m_hover) * m_spreadN.value);
            if (m_recoiling) {
                advanceRecoil();
            } else if (nearSide() >= 0.0) {
                const bool APEX = BEFORE > 0.0 && m_hop.velocity <= 0.0;
                const bool RESTING = std::abs(m_hop.value - m_hover) < kRestHopPixels &&
                                     std::abs(m_hop.velocity) < kRestHopPixelsPerSecond;
                if (nearSide() >= m_pinch || APEX || RESTING) {
                    startRecoil(m_profile.lobes[0]);  // m_profile still holds the attached profile
                                                      // from before this substep
                    m_growing = true;
                }
            }
            if (quiet()) {
                reset();
            }
            return;
        }

        if (!m_drained) {
            m_time += SUBSTEP_SECONDS;
            const double PROGRESS = std::min(m_time / m_duration, 1.0);
            // Shrink: the pill folds back into a circle, normal first, then tangential.
            m_spreadN.value = m_sN0 * (1.0 - easeInOutCubic(PROGRESS / kShrinkEnd));
            m_spreadT.value =
                m_sT0 *
                (1.0 - easeInOutCubic((PROGRESS - kShrinkTangentStart) / kShrinkTangentSpan));
            // One continuous trajectory: shrinking must not stop the centre at a hover point.
            // Momentum advances the dive without introducing a second launch.
            const double SQUARE = PROGRESS * PROGRESS;
            const double CUBIC = SQUARE * PROGRESS;
            const double DIVE =
                m_closeMomentum < 0.5
                    ? lerp(CUBIC, SQUARE, m_closeMomentum * 2.0)
                    : lerp(SQUARE, (2.0 * SQUARE) - CUBIC, (m_closeMomentum - 0.5) * 2.0);
            m_nc = lerp(m_nc0, -m_radius - kDiveMargin, DIVE);
            if (m_recoiling) {
                advanceRecoil();  // residual recoil from an interrupted opening keeps falling
            }
            if (m_time < m_duration - kAccumulatorTolerance) {
                return;
            }
            m_drained = true;
            m_splashing = m_closeSplash;  // the splash starts with the next substep
        } else {
            if (m_recoiling) {
                advanceRecoil();
            }
            if (m_splashing) {
                m_splashTime += SUBSTEP_SECONDS;
                m_splashing = m_splashTime < m_splashDuration - kAccumulatorTolerance;
            }
        }
        if (!m_splashing && recoilSettled()) {
            m_phase = Phase::Closed;
        }
    }

    EdgeTransition::Lobe EdgeTransition::liveLobe() const noexcept {
        const double NEAR_SIDE = nearSide();
        if (NEAR_SIDE < 0.0) {
            // Crossing: fillets hug the bead where it passes through the edge. They ramp in as the
            // bead's far side breaks the surface.
            return {.height = m_tuck * smoothstep(0.0, 2.0 * m_tuck, farSide()),
                .plateau = m_radius,
                .skirt = m_crossSkirt};
        }
        if (NEAR_SIDE <= m_pinch) {
            // Attached: the tip stays tucked under the near side while the contact patch necks
            // down.
            const double PINCH_BLEND = smoothstep(0.0, m_pinch, NEAR_SIDE);
            return {.height = NEAR_SIDE + m_tuck,
                .plateau = m_radius * (1.0 - PINCH_BLEND),
                .skirt = lerp(m_crossSkirt, m_neckSkirt, PINCH_BLEND)};
        }
        return {};
    }

    EdgeTransition::Lobe EdgeTransition::recoilLobe() const noexcept {
        const double RECOIL_TIME = m_recoilTime;
        return {.height = std::max(0.0, m_recoil.value),
            .plateau = m_snap.plateau * std::exp(-RECOIL_TIME / kRecoilPlateauSeconds),
            .skirt =
                m_snap.skirt * (1.0 + (kRecoilSkirtGrowth *
                                          (1.0 - std::exp(-RECOIL_TIME / kRecoilSkirtSeconds))))};
    }

    EdgeTransition::Lobe EdgeTransition::splashLobe() const noexcept {
        const double PROGRESS = m_splashTime / m_splashDuration;
        return {.height = kSplashPerRadius * m_radius * splashImpulse(PROGRESS),
            .plateau = 0.0,
            .skirt = kSplashSkirt * m_radius};
    }

    void EdgeTransition::refreshProfile() noexcept {
        m_profile = {};
        if (!active()) {
            return;
        }
        // Open liquid (attached neck or crossing fillets) until the snap; the recoil after a snap
        // (or an interrupted open's residual during a close); the splash once a close has drained.
        if (m_phase == Phase::Opening && !m_recoiling) {
            m_profile.add(liveLobe());
        }
        if (m_recoiling) {
            m_profile.add(recoilLobe());
        }
        if (m_phase == Phase::Closing && m_drained && m_splashing) {
            m_profile.add(splashLobe());
        }
    }

    void EdgeTransition::startRecoil(const Lobe &snap) noexcept {
        m_snap = snap;
        m_recoil = {.value = snap.height, .velocity = 0.0};
        m_recoilTime = 0.0;
        m_recoiling = true;
    }

    bool EdgeTransition::recoilSettled() const noexcept {
        return !m_recoiling || (std::abs(m_recoil.value) < kQuietPixels &&
                                   std::abs(m_recoil.velocity) < kQuietPixelsPerSecond);
    }

    void EdgeTransition::advanceRecoil() noexcept {
        m_recoilTime += SUBSTEP_SECONDS;
        spring(m_recoil, 0.0, m_recoilOmega, m_recoilZeta);
    }

    double EdgeTransition::meniscus(double t) const noexcept {
        const double CENTRE = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
        double peak_height = 0.0;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double SKIRT_DISTANCE = std::abs(t - CENTRE) - lobe.plateau;
            if (SKIRT_DISTANCE <= 0.0) {
                peak_height = std::max(peak_height, lobe.height);
                continue;
            }
            const double SKIRT_FRACTION = SKIRT_DISTANCE / lobe.skirt;
            if (SKIRT_FRACTION >= 1.0) {
                continue;
            }
            const double SKIRT_WEIGHT = 1.0 - (SKIRT_FRACTION * SKIRT_FRACTION);
            peak_height = std::max(peak_height,
                lobe.height * SKIRT_WEIGHT * SKIRT_WEIGHT);  // tangent to the edge at the skirt's
                                                             // foot, like the bar's concave flanks
        }
        return peak_height;
    }

    double EdgeTransition::meniscusLow() const noexcept {
        const double CENTRE = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
        double low = CENTRE;
        bool any = false;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double LOW_EDGE = CENTRE - lobe.plateau - lobe.skirt;
            low = any ? std::min(low, LOW_EDGE) : LOW_EDGE;
            any = true;
        }
        return low;
    }

    double EdgeTransition::meniscusHigh() const noexcept {
        const double CENTRE = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
        double high = CENTRE;
        bool any = false;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double HIGH_EDGE = CENTRE + lobe.plateau + lobe.skirt;
            high = any ? std::max(high, HIGH_EDGE) : HIGH_EDGE;
            any = true;
        }
        return high;
    }

    double EdgeTransition::meniscusPeak() const noexcept {
        return m_profile.peak();
    }

}  // namespace hyprimate
