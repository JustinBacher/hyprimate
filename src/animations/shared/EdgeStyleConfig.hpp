#pragma once

#include "animations/shared/EdgeTransition.hpp"
#include "config_settings_generated.hpp"

namespace hyprimate {
    inline CloseSide close_side(config::Side side) noexcept {
        switch (side) {
            case config::Side::Auto:
                return CloseSide::Auto;
            case config::Side::Nearest:
                return CloseSide::Nearest;
            case config::Side::Origin:
                return CloseSide::Origin;
            case config::Side::Top:
                return CloseSide::Top;
            case config::Side::Bottom:
                return CloseSide::Bottom;
            case config::Side::Left:
                return CloseSide::Left;
            case config::Side::Right:
                return CloseSide::Right;
        }
        return CloseSide::Auto;
    }

    inline Edge configured_edge(config::DefaultAnimationEdge edge) noexcept {
        switch (edge) {
            case config::DefaultAnimationEdge::Top:
                return Edge::Top;
            case config::DefaultAnimationEdge::Bottom:
                return Edge::Bottom;
            case config::DefaultAnimationEdge::Left:
                return Edge::Left;
            case config::DefaultAnimationEdge::Right:
                return Edge::Right;
        }
        return Edge::Bottom;
    }

    template <typename StyleConfig>
    std::optional<EdgeFrame> resolve_style_open_edge(
        const EdgeInput &input, const StyleConfig &style) noexcept {
        return resolve_edge_frame(input.body,
            input.capture,
            style.edgeBounds == config::EdgeBounds::WorkArea ? input.work_area : input.screen,
            input.neighbours,
            configured_edge(style.defaultAnimationEdge),
            input.last_window);
    }

    template <typename StyleConfig>
    std::optional<EdgeFrame> resolve_style_close_edge(
        const EdgeInput &input, const StyleConfig &style) noexcept {
        return resolve_close_edge_frame(input.body,
            input.capture,
            style.edgeBounds == config::EdgeBounds::WorkArea ? input.work_area : input.screen,
            input.neighbours,
            configured_edge(style.defaultAnimationEdge),
            close_side(style.side),
            input.origin,
            input.last_window);
    }
}  // namespace hyprimate
