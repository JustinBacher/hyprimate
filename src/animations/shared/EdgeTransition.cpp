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

        constexpr double EDGE_TOLERANCE = 0.5;  // px a body may overhang an edge and still use it
        constexpr double TIE_TOLERANCE = 1.0;  // px
        constexpr double CONTACT_SLACK =
            2.0;  // px a neighbour may overlap a body side and still touch it
        constexpr double CONTACT_REACH = 64.0;  // px a facing gap may be and still count as contact

        constexpr double ACCUMULATOR_TOLERANCE = 1e-9;
        constexpr double REFERENCE_STRENGTH = 0.65;
        // Bead.
        constexpr double RADIUS_PER_SIDE = 0.14, RADIUS_MIN = 20.0, RADIUS_MAX = 80.0;
        constexpr double RADIUS_FIT = 0.95;  // r <= this fraction of the capture half-extents
        constexpr double HOVER_PER_PINCH = 1.25;  // nh = r + HOVER_PER_PINCH * Lp
        // Hop: the bead centre rises from behind the edge to its hover point.
        constexpr double HOP_OMEGA =
            22.0;  // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
        constexpr double HOP_ZETA_STIFF = 0.8, HOP_ZETA_LOOSE = 0.45;  // momentum 0 / 1
        // Grow: the spreads take the bead to the window's extents (tangential first, then normal).
        constexpr double GROW_OMEGA = 15.0;  // rad/s at elasticity 0.5
        constexpr double GROW_ZETA_STIFF = 0.85, GROW_ZETA_LOOSE = 0.5;  // momentum 0 / 1
        constexpr double GROW_NORMAL_RATIO = 0.75;
        // Black edge liquid, scaled with the bead.
        constexpr double TUCK_PER_RADIUS = 0.35, TUCK_MIN = 5.0, TUCK_MAX = 20.0;
        constexpr double PINCH_PER_RADIUS = 1.1, PINCH_MIN = 16.0, PINCH_MAX = 72.0;
        constexpr double NECK_PER_RADIUS = 0.9, NECK_MIN = 12.0, NECK_MAX = 64.0;
        constexpr double CROSS_SKIRT_PER_TUCK = 2.5;
        // Recoil after the snap.
        constexpr double RECOIL_OMEGA = 28.0;
        constexpr double RECOIL_ZETA_STIFF = 0.6, RECOIL_ZETA_LOOSE = 0.2;
        constexpr double RECOIL_SKIRT_GROWTH = 0.6, RECOIL_SKIRT_SECONDS = 0.06,
                         RECOIL_PLATEAU_SECONDS = 0.03;
        // Snap fallbacks: hop at apex or at rest.
        constexpr double REST_HOP_PIXELS = 0.5, REST_HOP_PIXELS_PER_SECOND = 5.0;
        // Close.
        constexpr double CLOSE_SECONDS = 0.46;
        constexpr double SHRINK_END = 0.45;  // normal shrink ends here (fraction of the close)
        constexpr double SHRINK_TANGENT_START = 0.05, SHRINK_TANGENT_SPAN = 0.5;
        constexpr double DIVE_MARGIN =
            2.0;  // px beyond the bead's far side when the close completes
        constexpr double SPLASH_SECONDS =
            0.42;  // at elasticity 0.5; scaled by 2^(0.5 - elasticity)
        constexpr double SPLASH_PEAK_X = 0.22;  // impulse peak in normalised time
        constexpr double SPLASH_PER_RADIUS = 0.9;  // height at the peak, per radius
        constexpr double SPLASH_SKIRT = 0.55;  // skirt half-width per radius
        // Settling thresholds.
        constexpr double QUIET_PIXELS = 0.15, QUIET_PIXELS_PER_SECOND = 2.0;
        constexpr double QUIET_SPREAD = 1e-4,
                         QUIET_SPREAD_PER_SECOND =
                             1e-3;  // spreads are unitless; ~0.1 px on a 1000 px half-extent

        bool runs_horizontally(Edge edge) {
            return edge == Edge::Top || edge == Edge::Bottom;
        }

        // Overlap of two 1-D spans; 0 when disjoint.
        double overlap_length(
            double first_start, double first_end, double second_start, double second_end) {
            return std::max(
                0.0, std::min(first_end, second_end) - std::max(first_start, second_start));
        }

        Edge opposite(Edge edge) {
            switch (edge) {
                case Edge::Left:
                    return Edge::Right;
                case Edge::Right:
                    return Edge::Left;
                case Edge::Top:
                    return Edge::Bottom;
                case Edge::Bottom:
                    return Edge::Top;
            }
            return edge;
        }

        // Length a body side shares with neighbour n when n sits just beyond that side; 0 when it
        // does not touch.
        double contact_length(const Box &body, Edge side, const Box &neighbor) {
            double gap = 0.0;
            switch (side) {
                case Edge::Left:
                    gap = body.x - (neighbor.x + neighbor.w);
                    break;
                case Edge::Right:
                    gap = neighbor.x - (body.x + body.w);
                    break;
                case Edge::Top:
                    gap = body.y - (neighbor.y + neighbor.h);
                    break;
                case Edge::Bottom:
                    gap = neighbor.y - (body.y + body.h);
                    break;
            }
            if (gap < -CONTACT_SLACK || gap > CONTACT_REACH) {
                return 0.0;
            }
            return runs_horizontally(side)
                       ? overlap_length(
                             body.x, body.x + body.w, neighbor.x, neighbor.x + neighbor.w)
                       : overlap_length(
                             body.y, body.y + body.h, neighbor.y, neighbor.y + neighbor.h);
        }

        double lerp(double start, double end, double fraction) {
            return start + ((end - start) * fraction);
        }

        double ease_in_out_cubic(double progress) {
            progress = std::clamp(progress, 0.0, 1.0);
            return progress < 0.5 ? 4.0 * progress * progress * progress
                                  : 1.0 - (std::pow((-2.0 * progress) + 2.0, 3.0) / 2.0);
        }

        // Splash impulse in normalised time x: peaks at 1 when x = SPLASH_PEAK_X, is 0 outside (0,
        // 1) and ends at 0 at x = 1.
        double splash_impulse(double progress) {
            if (progress <= 0.0 || progress >= 1.0) {
                return 0.0;
            }
            const double PEAK_RATIO = progress / SPLASH_PEAK_X;
            return PEAK_RATIO * std::exp(1.0 - PEAK_RATIO) * (1.0 - smoothstep(0.7, 1.0, progress));
        }

        // Semi-implicit Euler toward `target`. Stable for omega * dt < 2; the largest omega used
        // (recoil, ~40 rad/s) gives omega * dt ~ 0.17 at 240 Hz substeps.
        template <typename S>
        void spring(S &state, double target, double omega, double zeta) {
            const double ACCEL =
                (-omega * omega * (state.value - target)) - (2.0 * zeta * omega * state.velocity);
            state.velocity += ACCEL * SUBSTEP_SECONDS;
            state.value += state.velocity * SUBSTEP_SECONDS;
        }

        bool valid_frame(const EdgeFrame &frame) {
            const std::array VALUES = {frame.edgeDistance,
                frame.captureFar,
                frame.bodyNear,
                frame.bodyFar,
                frame.bodyT0,
                frame.bodyT1};
            for (const double VALUE : VALUES) {
                if (!std::isfinite(VALUE)) {
                    return false;
                }
            }
            return frame.edgeDistance > 0.0 && frame.captureFar > frame.edgeDistance &&
                   frame.bodyNear >= 0.0 && frame.bodyFar > frame.bodyNear &&
                   frame.bodyT1 > frame.bodyT0;
        }

        // Radially homogeneous outline map in units of the capture half-extents. The rectangle
        // outline (max(|x|,|y|) = 1) is carried onto the superellipse of exponent n = 2 /
        // roundness; every ray from the centre maps onto itself, so the image cannot fold. `inv_n`
        // is 1/n. Callers pass the rectangle through unchanged (roundness below 2e-4).
        Vec2 outline(Vec2 point, double n, double inv_n) {
            const double ABS_X = std::abs(point.x);
            const double ABS_Y = std::abs(point.y);
            const double RHO = std::max(ABS_X, ABS_Y);
            if (!(RHO > 0.0)) {
                return point;
            }
            const double DIR_X = ABS_X / RHO;
            const double DIR_Y = ABS_Y / RHO;  // direction on the rectangle outline, max = 1
            const double OUTLINE_SCALE =
                std::pow(std::pow(DIR_X, n) + std::pow(DIR_Y, n), inv_n);  // in [1, 2^(1/n)]
            return {.x = point.x / OUTLINE_SCALE, .y = point.y / OUTLINE_SCALE};
        }

    }  // namespace

    std::optional<EdgeFrame> resolveCloseEdgeFrame(const Box &body,
        const Box &capture,
        const Box &bounds,
        std::span<const Box> neighbours,
        Edge fallback,
        CloseSide side,
        std::optional<Edge> origin,
        bool last_window) noexcept {
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
                prefer = last_window;
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
                contact[SLOT(SIDE)] += contact_length(body, SIDE, adjacent);
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
                if (contact[SLOT(SIDE)] >= MOST - TIE_TOLERANCE && FREE(opposite(SIDE))) {
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
            if (ALLOWED(EDGE) && REACH[SLOT(EDGE)] <= best + TIE_TOLERANCE) {
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
        return runs_horizontally(f.edge) ? p.x : p.y;
    }

    std::optional<EdgeFrame> resolveEdgeFrame(const Box &body,
        const Box &capture,
        const Box &screen,
        std::span<const Box> neighbours,
        Edge fallback,
        bool prefer_fallback) noexcept {
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
        const auto VALID = [&](Edge e) { return GAP(e) >= -EDGE_TOLERANCE; };

        // Nearest valid edge among `allowed`; ties within TIE_TOLERANCE go to fallback, then
        // kOrder.
        const auto NEAREST_AMONG = [&](const std::array<bool, 4> &allowed) -> std::optional<Edge> {
            const auto IS_ALLOWED = [&](Edge e) { return allowed[SLOT(e)] && VALID(e); };
            double best = std::numeric_limits<double>::infinity();
            for (const Edge CANDIDATE : K_ORDER) {
                if (IS_ALLOWED(CANDIDATE)) {
                    best = std::min(best, REACH(CANDIDATE));
                }
            }
            if (IS_ALLOWED(fallback) && REACH(fallback) <= best + TIE_TOLERANCE) {
                return fallback;
            }
            for (Edge edge_option : K_ORDER) {
                if (IS_ALLOWED(edge_option) && REACH(edge_option) <= best + TIE_TOLERANCE) {
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
                contact[SLOT(SIDE)] += contact_length(body, SIDE, neighbour);
            }
        }
        const double MOST = *std::max_element(contact.begin(), contact.end());
        const bool ANY_CONTACT = MOST > 0.0;
        std::array<bool, 4> candidate{};
        if (ANY_CONTACT) {
            for (const Edge SIDE : K_ORDER) {
                if (contact[SLOT(SIDE)] > 0.0 && contact[SLOT(SIDE)] >= MOST - TIE_TOLERANCE) {
                    candidate[SLOT(opposite(SIDE))] = true;
                }
            }
        }

        std::optional<Edge> chosen;
        if (prefer_fallback && VALID(fallback)) {
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
        const bool HORIZONTAL = runs_horizontally(EDGE);
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
        bool close_splash) noexcept {
        const bool CONTINUING = kind == Transition::Close && m_phase == Phase::Opening;
        const EdgeTransition PREVIOUS = *this;
        reset();
        const EdgeFrame &chosen = CONTINUING ? PREVIOUS.m_frame : frame;
        if (!valid_frame(chosen) || !std::isfinite(params.elasticity) ||
            !std::isfinite(params.momentum) || !std::isfinite(strength) || strength <= 0.0) {
            return;
        }

        m_frame = chosen;
        const double ELASTICITY = std::clamp(params.elasticity, 0.0, 1.0);
        const double MOMENTUM = std::clamp(params.momentum, 0.0, 1.0);
        const double STRENGTH_SCALE = std::clamp(strength / REFERENCE_STRENGTH, 0.25, 1.5);
        const double WIDTH = m_frame.bodyT1 - m_frame.bodyT0;
        const double DEPTH = m_frame.bodyFar - m_frame.bodyNear;
        m_halfN = m_frame.captureFar - m_frame.edgeDistance;
        // The capture contains the body, so its tangential half-extent is at least width / 2; using
        // that bound keeps the bead inside both capture half-extents without needing the capture
        // size here.
        const double FIT = RADIUS_FIT * std::min(m_halfN, 0.5 * WIDTH);
        m_radius =
            std::min(std::clamp(RADIUS_PER_SIDE * std::min(WIDTH, DEPTH), RADIUS_MIN, RADIUS_MAX) *
                         STRENGTH_SCALE,
                FIT);
        m_tuck = std::clamp(TUCK_PER_RADIUS * m_radius, TUCK_MIN, TUCK_MAX);
        m_pinch = std::clamp(PINCH_PER_RADIUS * m_radius, PINCH_MIN, PINCH_MAX);
        m_neckSkirt = std::clamp(NECK_PER_RADIUS * m_radius, NECK_MIN, NECK_MAX);
        m_crossSkirt = CROSS_SKIRT_PER_TUCK * m_tuck;
        m_hover = m_radius + (HOVER_PER_PINCH * m_pinch);
        m_recoilOmega = RECOIL_OMEGA * std::exp2(ELASTICITY - 0.5);
        m_recoilZeta = lerp(RECOIL_ZETA_STIFF, RECOIL_ZETA_LOOSE, MOMENTUM);
        m_growOmega = GROW_OMEGA * std::exp2(ELASTICITY - 0.5);
        m_growZeta = lerp(GROW_ZETA_STIFF, GROW_ZETA_LOOSE, MOMENTUM);

        if (kind == Transition::Open) {
            m_hopOmega = HOP_OMEGA * std::exp2(ELASTICITY - 0.5);
            m_hopZeta = lerp(HOP_ZETA_STIFF, HOP_ZETA_LOOSE, MOMENTUM);
            m_hop = {.value = -m_radius, .velocity = 0.0};  // bead fully behind the edge
            m_nc = -m_radius;
            m_phase = Phase::Opening;
        } else {
            m_duration = CLOSE_SECONDS * std::exp2(0.5 - ELASTICITY);
            m_closeMomentum = MOMENTUM;
            m_closeSplash = close_splash;
            m_splashDuration = SPLASH_SECONDS * std::exp2(0.5 - ELASTICITY);
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
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
        const Vec2 INWARD = inwardNormal(m_frame.edge);
        const Vec2 ALONG = HORIZONTAL ? Vec2{.x = 1.0, .y = 0.0} : Vec2{.x = 0.0, .y = 1.0};
        const double TANGENT_CENTRE =
            0.5 * (m_frame.bodyT0 + m_frame.bodyT1) * (1.0 - m_spreadT.value);
        const double NORMAL_CENTRE = m_nc - m_frame.edgeDistance;
        return {.x = (ALONG.x * TANGENT_CENTRE) + (INWARD.x * NORMAL_CENTRE),
            .y = (ALONG.y * TANGENT_CENTRE) + (INWARD.y * NORMAL_CENTRE)};
    }

    Vec2 EdgeTransition::beadHalf(Vec2 rest_size) const noexcept {
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
        const double HALF_T = 0.5 * (HORIZONTAL ? rest_size.x : rest_size.y);
        const double HALF_N = 0.5 * (HORIZONTAL ? rest_size.y : rest_size.x);
        if (!active()) {
            return {.x = 0.5 * rest_size.x, .y = 0.5 * rest_size.y};
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
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
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

    Vec2 EdgeTransition::RestMap::map(Vec2 from_centre) const noexcept {
        if (identity || !std::isfinite(from_centre.x) || !std::isfinite(from_centre.y)) {
            return from_centre;
        }
        // Rest point in units of the capture half-extents.
        const double REST_T = (horizontal ? from_centre.x : from_centre.y) / halfT;
        const double REST_N = ((from_centre.x * inward.x) + (from_centre.y * inward.y)) / halfN;
        const Vec2 SHAPE = rectangle ? Vec2{.x = REST_T, .y = REST_N}
                                     : outline({.x = REST_T, .y = REST_N}, exponent, invExponent);
        const double T_OUT = tBase + (tScale * SHAPE.x);
        const double N_OUT = nBase + (nScale * SHAPE.y);
        return {.x = (along.x * T_OUT) + (inward.x * N_OUT),
            .y = (along.y * T_OUT) + (inward.y * N_OUT)};
    }

    Vec2 EdgeTransition::map(Vec2 from_centre, Vec2 size) const noexcept {
        return restMap(size).map(from_centre);
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

    bool EdgeTransition::advance(double elapsed_seconds) noexcept {
        if (!active()) {
            return false;
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0 ||
            elapsed_seconds > STALL_SECONDS) {
            abort();
            return false;
        }
        m_accumulator += elapsed_seconds;
        while (active() && m_accumulator >= SUBSTEP_SECONDS - ACCUMULATOR_TOLERANCE) {
            m_accumulator -= SUBSTEP_SECONDS;
            step();
            refreshProfile();  // per substep, so a snap always starts from the state one substep
                               // earlier
        }
        return active();
    }

    bool EdgeTransition::quiet() const noexcept {
        const bool HOP_QUIET = std::abs(m_hop.value - m_hover) < QUIET_PIXELS &&
                               std::abs(m_hop.velocity) < QUIET_PIXELS_PER_SECOND;
        const bool GROW_QUIET = std::abs(m_spreadT.value - 1.0) < QUIET_SPREAD &&
                                std::abs(m_spreadT.velocity) < QUIET_SPREAD_PER_SECOND &&
                                std::abs(m_spreadN.value - 1.0) < QUIET_SPREAD &&
                                std::abs(m_spreadN.velocity) < QUIET_SPREAD_PER_SECOND;
        const bool LIQUID_QUIET = m_recoiling && std::abs(m_recoil.value) < QUIET_PIXELS &&
                                  std::abs(m_recoil.velocity) < QUIET_PIXELS_PER_SECOND;
        return m_growing && HOP_QUIET && GROW_QUIET && LIQUID_QUIET;
    }

    void EdgeTransition::step() noexcept {
        if (m_phase == Phase::Opening) {
            const double BEFORE = m_hop.velocity;
            spring(m_hop, m_hover, m_hopOmega, m_hopZeta);
            if (m_growing) {
                spring(m_spreadT, 1.0, m_growOmega, m_growZeta);
                spring(m_spreadN, 1.0, m_growOmega * GROW_NORMAL_RATIO, m_growZeta);
            }
            // The centre carries the normal spread's overshoot: it sits at the hop until the grow
            // starts.
            m_nc = m_hop.value + ((m_frame.edgeDistance - m_hover) * m_spreadN.value);
            if (m_recoiling) {
                advanceRecoil();
            } else if (nearSide() >= 0.0) {
                const bool APEX = BEFORE > 0.0 && m_hop.velocity <= 0.0;
                const bool RESTING = std::abs(m_hop.value - m_hover) < REST_HOP_PIXELS &&
                                     std::abs(m_hop.velocity) < REST_HOP_PIXELS_PER_SECOND;
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
            m_spreadN.value = m_sN0 * (1.0 - ease_in_out_cubic(PROGRESS / SHRINK_END));
            m_spreadT.value =
                m_sT0 *
                (1.0 - ease_in_out_cubic((PROGRESS - SHRINK_TANGENT_START) / SHRINK_TANGENT_SPAN));
            // One continuous trajectory: shrinking must not stop the centre at a hover point.
            // Momentum advances the dive without introducing a second launch.
            const double SQUARE = PROGRESS * PROGRESS;
            const double CUBIC = SQUARE * PROGRESS;
            const double DIVE =
                m_closeMomentum < 0.5
                    ? lerp(CUBIC, SQUARE, m_closeMomentum * 2.0)
                    : lerp(SQUARE, (2.0 * SQUARE) - CUBIC, (m_closeMomentum - 0.5) * 2.0);
            m_nc = lerp(m_nc0, -m_radius - DIVE_MARGIN, DIVE);
            if (m_recoiling) {
                advanceRecoil();  // residual recoil from an interrupted opening keeps falling
            }
            if (m_time < m_duration - ACCUMULATOR_TOLERANCE) {
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
                m_splashing = m_splashTime < m_splashDuration - ACCUMULATOR_TOLERANCE;
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
            .plateau = m_snap.plateau * std::exp(-RECOIL_TIME / RECOIL_PLATEAU_SECONDS),
            .skirt =
                m_snap.skirt * (1.0 + (RECOIL_SKIRT_GROWTH *
                                          (1.0 - std::exp(-RECOIL_TIME / RECOIL_SKIRT_SECONDS))))};
    }

    EdgeTransition::Lobe EdgeTransition::splashLobe() const noexcept {
        const double PROGRESS = m_splashTime / m_splashDuration;
        return {.height = SPLASH_PER_RADIUS * m_radius * splash_impulse(PROGRESS),
            .plateau = 0.0,
            .skirt = SPLASH_SKIRT * m_radius};
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
        return !m_recoiling || (std::abs(m_recoil.value) < QUIET_PIXELS &&
                                   std::abs(m_recoil.velocity) < QUIET_PIXELS_PER_SECOND);
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
