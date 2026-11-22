#pragma once

#include "config_settings_generated.hpp"
#include "window/shared/EdgeTransition.hpp"
#include "window/smoke/Smoke.hpp"

namespace hyprimate {

    // Where opening smoke enters or closing smoke leaves. A configured screen edge is used when
    // the window faces it, as liquid's preferred edges are; otherwise liquid's edge choice
    // decides. No usable edge falls back to inside.
    inline SmokeRoute smoke_route(config::Direction direction, const EdgeInput &input) noexcept {
        Edge preferred = Edge::Bottom;
        switch (direction) {
            case config::Direction::Inside:
                return {};
            case config::Direction::Top:
                preferred = Edge::Top;
                break;
            case config::Direction::Bottom:
                preferred = Edge::Bottom;
                break;
            case config::Direction::Left:
                preferred = Edge::Left;
                break;
            case config::Direction::Right:
                preferred = Edge::Right;
                break;
        }
        const auto FRAME = resolve_edge_frame(
            input.body, input.capture, input.screen, input.neighbours, preferred, true);
        if (!FRAME) {
            return {};
        }
        SmokeRoute route{.edge = true, .direction = {}, .reach = FRAME->edge_distance};
        switch (FRAME->edge) {
            case Edge::Top:
                route.direction = {.x = 0, .y = -1};
                break;
            case Edge::Bottom:
                route.direction = {.x = 0, .y = 1};
                break;
            case Edge::Left:
                route.direction = {.x = -1, .y = 0};
                break;
            case Edge::Right:
                route.direction = {.x = 1, .y = 0};
                break;
        }
        return route;
    }

}  // namespace hyprimate
