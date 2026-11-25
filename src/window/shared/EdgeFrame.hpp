#pragma once

#include <optional>
#include <span>

#include "shared/Core.hpp"

namespace hyprimate {

    // Rest geometry in the edge frame. n = distance from the screen edge into the screen; t =
    // tangential coordinate. Every value is for points given relative to the capture (decorated +
    // shadow) centre at rest.
    struct EdgeFrame {
        Edge edge = Edge::Bottom;
        double edge_distance = 0.0;  // n of the capture centre (> 0)
        double capture_far = 0.0;  // n of the capture side farthest from the edge
        double body_near = 0.0;  // n of the decorated window side nearest the edge (>= 0)
        double body_far = 0.0;  // n of the decorated window side farthest from the edge
        double body_t0 = 0.0;  // tangential span of the decorated window
        double body_t1 = 0.0;
    };

    // Chooses the edge a window emerges from or drains into. An edge is valid when the decorated
    // body lies inside the screen along its normal. prefer_fallback (first window on its workspace,
    // or close_side=origin) picks `fallback` when valid. Otherwise the neighbours decide: each body
    // side sums the length it shares with neighbours whose facing gap is in [-2, 64] px. The sides
    // within 1 px of the largest contact point at the opposite screen edges, and the valid
    // candidate nearest the body centre wins. Without usable contact, the valid edge nearest the
    // body centre wins. Ties within 1 px go to `fallback`, then Bottom, Right, Left, Top. nullopt
    // for unusable boxes or when no edge is valid.
    std::optional<EdgeFrame> resolve_edge_frame(const Box &body,
        const Box &capture,
        const Box &screen,
        std::span<const Box> neighbours,
        Edge fallback,
        bool prefer_fallback) noexcept;

    // Where a closing window exits.
    enum class CloseSide { Auto, Nearest, Origin, Top, Bottom, Left, Right };

    // Compositor geometry passed to a style; the style selects its own edge policy.
    struct EdgeInput {
        Box body;
        Box capture;
        Box screen;
        Box work_area;
        std::span<const Box> neighbours;
        std::optional<Edge> origin;
        bool last_window = false;
    };

    // Shared liquid/cloth close policy. Auto prefers fallback for the last window; Nearest ignores
    // neighbours; Origin prefers the recorded opening edge; fixed sides prefer that edge. An
    // unavailable or invalid preference uses the usual resolver.
    std::optional<EdgeFrame> resolve_close_edge_frame(const Box &body,
        const Box &capture,
        const Box &bounds,
        std::span<const Box> neighbours,
        Edge fallback,
        CloseSide side,
        std::optional<Edge> origin = std::nullopt,
        bool last_window = false) noexcept;

    // Every screen edge a window appears from, for motion that may use several at once (swarm). A
    // side of the body is free when no neighbour touches it (same contact rule as
    // resolve_edge_frame). The free edges opposite the sides within 1 px of the largest contact
    // win; without those, the free edges within 1 px of the nearest one to the body centre; a body
    // touched on every side uses the nearest edges overall. Empty for unusable boxes.
    EdgeSet resolve_edge_sides(
        const Box &body, const Box &screen, std::span<const Box> neighbours) noexcept;

    // Edge frame <-> point relative to the capture centre.
    Vec2 edge_point(const EdgeFrame &frame, double tangent, double n) noexcept;
    double edge_normal(const EdgeFrame &frame, Vec2 point) noexcept;
    double edge_tangent(const EdgeFrame &frame, Vec2 point) noexcept;
    // Unit vector pointing from the edge into the screen.
    Vec2 inward_normal(Edge edge) noexcept;
    inline bool runs_horizontally(Edge edge) {
        return edge == Edge::Top || edge == Edge::Bottom;
    }
}  // namespace hyprimate
