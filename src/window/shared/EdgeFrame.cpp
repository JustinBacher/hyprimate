#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

#include "shared/Core.hpp"
#include "window/shared/EdgeFrame.hpp"

namespace hyprimate {
    namespace {

        constexpr double EDGE_TOLERANCE = 0.5;  // px a body may overhang an edge and still use it
        constexpr double TIE_TOLERANCE = 1.0;  // px
        constexpr double CONTACT_SLACK =
            2.0;  // px a neighbour may overlap a body side and still touch it
        constexpr double CONTACT_REACH = 64.0;  // px a facing gap may be and still count as contact

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
    }  // namespace

    std::optional<EdgeFrame> resolve_close_edge_frame(const Box &body,
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
        return resolve_edge_frame(body,
            capture,
            bounds,
            side == CloseSide::Nearest ? std::span<const Box>{} : neighbours,
            fallback,
            prefer);
    }

    EdgeSet resolve_edge_sides(
        const Box &body, const Box &screen, std::span<const Box> neighbours) noexcept {
        EdgeSet sides;
        if (!is_usable(body) || !is_usable(screen)) {
            return sides;
        }

        // Indexed by the Edge value.
        constexpr std::array<Edge, 4> K_EDGES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
        const auto SLOT = [](Edge edge) { return static_cast<std::size_t>(edge); };
        std::array<double, 4> contact{};
        for (const Box &adjacent : neighbours) {
            if (!is_usable(adjacent)) {
                continue;
            }
            auto *amount = contact.begin();
            for (const Edge SIDE : K_EDGES) {
                *amount += contact_length(body, SIDE, adjacent);
                ++amount;
            }
        }
        const double MOST = *std::ranges::max_element(contact);
        const auto FREE = [&](Edge edge) { return !(contact[SLOT(edge)] > 0.0); };
        const double BODY_CENTRE_X = body.x + (0.5 * body.w);
        const double BODY_CENTRE_Y = body.y + (0.5 * body.h);
        const std::array<double, 4> REACH{
            BODY_CENTRE_X - screen.x,
            screen.x + screen.w - BODY_CENTRE_X,
            BODY_CENTRE_Y - screen.y,
            screen.y + screen.h - BODY_CENTRE_Y,
        };

        if (MOST > 0.0) {
            const auto *amount = contact.cbegin();
            for (const Edge SIDE : K_EDGES) {
                if (*amount >= MOST - TIE_TOLERANCE && FREE(opposite(SIDE))) {
                    sides.add(opposite(SIDE));
                }
                ++amount;
            }
            if (!sides.empty()) {
                return sides;
            }
        }

        // Nearest edges among the free ones; among all four when every side is touched.
        const bool ANY_FREE = std::ranges::any_of(K_EDGES, FREE);
        const auto ALLOWED = [&](Edge edge) { return !ANY_FREE || FREE(edge); };
        double best = std::numeric_limits<double>::infinity();
        const auto *reach = REACH.cbegin();
        for (const Edge EDGE_OPTION : K_EDGES) {
            if (ALLOWED(EDGE_OPTION)) {
                best = std::min(best, *reach);
            }
            ++reach;
        }
        reach = REACH.cbegin();
        for (const Edge EDGE : K_EDGES) {
            if (ALLOWED(EDGE) && *reach <= best + TIE_TOLERANCE) {
                sides.add(EDGE);
            }
            ++reach;
        }
        return sides;
    }

    Vec2 inward_normal(Edge edge) noexcept {
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

    Vec2 edge_point(const EdgeFrame &frame, double tangent, double n) noexcept {
        const double EDGE_DISTANCE = frame.edge_distance;
        switch (frame.edge) {
            case Edge::Left:
                return {.x = n - EDGE_DISTANCE, .y = tangent};
            case Edge::Right:
                return {.x = EDGE_DISTANCE - n, .y = tangent};
            case Edge::Top:
                return {.x = tangent, .y = n - EDGE_DISTANCE};
            case Edge::Bottom:
                return {.x = tangent, .y = EDGE_DISTANCE - n};
        }
        return {.x = tangent, .y = EDGE_DISTANCE - n};
    }

    double edge_normal(const EdgeFrame &frame, Vec2 point) noexcept {
        const double EDGE_DISTANCE = frame.edge_distance;
        switch (frame.edge) {
            case Edge::Left:
                return EDGE_DISTANCE + point.x;
            case Edge::Right:
                return EDGE_DISTANCE - point.x;
            case Edge::Top:
                return EDGE_DISTANCE + point.y;
            case Edge::Bottom:
                return EDGE_DISTANCE - point.y;
        }
        return EDGE_DISTANCE - point.y;
    }

    double edge_tangent(const EdgeFrame &frame, Vec2 point) noexcept {
        return runs_horizontally(frame.edge) ? point.x : point.y;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Edge selection preserves tie
    // priority and FP/NaN evaluation order.
    std::optional<EdgeFrame> resolve_edge_frame(const Box &body,
        const Box &capture,
        const Box &screen,
        std::span<const Box> neighbours,
        Edge fallback,
        bool prefer_fallback) noexcept {
        if (!is_usable(body) || !is_usable(capture) || !is_usable(screen)) {
            return std::nullopt;
        }

        constexpr std::array<Edge, 4> K_ORDER{Edge::Bottom, Edge::Right, Edge::Left, Edge::Top};
        const auto SLOT = [](auto &values, Edge edge) -> auto & {
            switch (edge) {
                case Edge::Left:
                    return std::get<0>(values);
                case Edge::Right:
                    return std::get<1>(values);
                case Edge::Top:
                    return std::get<2>(values);
                case Edge::Bottom:
                    return std::get<3>(values);
            }
            return std::get<0>(values);
        };
        const double RIGHT = screen.x + screen.w;
        const double BOTTOM = screen.y + screen.h;
        const double CENTRE_X = body.x + (0.5 * body.w);
        const double CENTRE_Y = body.y + (0.5 * body.h);
        const auto GAP = [&](Edge edge) {
            switch (edge) {
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
        const auto REACH = [&](Edge edge) {
            switch (edge) {
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
        const auto VALID = [&](Edge edge) { return GAP(edge) >= -EDGE_TOLERANCE; };

        // Nearest valid edge among `allowed`; ties within TIE_TOLERANCE go to fallback, then
        // kOrder.
        const auto NEAREST_AMONG = [&](const std::array<bool, 4> &allowed) -> std::optional<Edge> {
            const auto IS_ALLOWED = [&](Edge edge) { return SLOT(allowed, edge) && VALID(edge); };
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
                SLOT(contact, SIDE) += contact_length(body, SIDE, neighbour);
            }
        }
        const double MOST = *std::ranges::max_element(contact);
        const bool ANY_CONTACT = MOST > 0.0;
        std::array<bool, 4> candidate{};
        if (ANY_CONTACT) {
            for (const Edge SIDE : K_ORDER) {
                if (SLOT(contact, SIDE) > 0.0 && SLOT(contact, SIDE) >= MOST - TIE_TOLERANCE) {
                    SLOT(candidate, opposite(SIDE)) = true;
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
                frame.edge_distance = CCX - screen.x;
                break;
            case Edge::Right:
                frame.edge_distance = RIGHT - CCX;
                break;
            case Edge::Top:
                frame.edge_distance = CCY - screen.y;
                break;
            case Edge::Bottom:
                frame.edge_distance = BOTTOM - CCY;
                break;
        }
        frame.capture_far = frame.edge_distance + (0.5 * (HORIZONTAL ? capture.h : capture.w));
        frame.body_near = std::max(0.0, GAP(EDGE));
        frame.body_far = frame.body_near + (HORIZONTAL ? body.h : body.w);
        frame.body_t0 = HORIZONTAL ? body.x - CCX : body.y - CCY;
        frame.body_t1 = frame.body_t0 + (HORIZONTAL ? body.w : body.h);
        return frame;
    }
}  // namespace hyprimate
