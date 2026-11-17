#pragma once

#include "animations/shared/EdgeTransition.hpp"
#include "config_generated.hpp"

namespace hyprimate {
    inline CloseSide closeSide(config::Side side) noexcept {
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

    inline Edge configuredEdge(config::DefaultAnimationEdge edge) noexcept {
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
    std::optional<EdgeFrame> resolveStyleOpenEdge(
        const EdgeInput &input, const StyleConfig &style) noexcept {
        return resolveEdgeFrame(input.body,
            input.capture,
            style.edgeBounds == config::EdgeBounds::WorkArea ? input.workArea : input.screen,
            input.neighbours,
            configuredEdge(style.defaultAnimationEdge),
            input.lastWindow);
    }

    template <typename StyleConfig>
    std::optional<EdgeFrame> resolveStyleCloseEdge(
        const EdgeInput &input, const StyleConfig &style) noexcept {
        return resolveCloseEdgeFrame(input.body,
            input.capture,
            style.edgeBounds == config::EdgeBounds::WorkArea ? input.workArea : input.screen,
            input.neighbours,
            configuredEdge(style.defaultAnimationEdge),
            closeSide(style.side),
            input.origin,
            input.lastWindow);
    }
}  // namespace hyprimate
