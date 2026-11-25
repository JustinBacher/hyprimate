#include <optional>

#include "WindowAnimationGeometry.hpp"

namespace hyprimate::animation::detail {
    namespace {
        Box translated(Box box, Vec2 offset) {
            box.x += offset.x;
            box.y += offset.y;
            return box;
        }
    }  // namespace

    Box presented_body(const Observation &observation) {
        return translated(observation.body,
            {observation.workspace_offset.x + observation.floating_offset.x,
                observation.workspace_offset.y + observation.floating_offset.y});
    }

    Box source_device(const Observation &observation) {
        const Vec2 OFFSET{observation.workspace_offset.x + observation.floating_offset.x -
                observation.screen.x,
            observation.workspace_offset.y + observation.floating_offset.y -
                observation.screen.y};
        const auto SOURCE = translated(observation.capture, OFFSET);
        return {SOURCE.x * observation.scale,
            SOURCE.y * observation.scale,
            SOURCE.w * observation.scale,
            SOURCE.h * observation.scale};
    }

    Box rest_capture(const Observation &observation, const Box &goal) {
        return {.x = goal.x + observation.capture.x - observation.body.x,
            .y = goal.y + observation.capture.y - observation.body.y,
            .w = goal.w + observation.capture.w - observation.body.w,
            .h = goal.h + observation.capture.h - observation.body.h};
    }

    Place place(const Observation &observation, Neighbourhood near, std::uint64_t seed) {
        if (near.cursor) {
            near.cursor->x -= observation.workspace_offset.x + observation.floating_offset.x;
            near.cursor->y -= observation.workspace_offset.y + observation.floating_offset.y;
        }
        return {.body = observation.body,
            .capture = observation.capture,
            .goal_body = observation.goal_body,
            .rest_capture = rest_capture(observation, observation.goal_body),
            .screen = observation.screen,
            .work_area = observation.work_area,
            .near = near,
            .seed = seed,
            .opening_edge = std::nullopt,
            .monitor = observation.monitor,
            .workspace = observation.workspace,
            .pinned = observation.pinned,
            .scale = observation.scale,
            .fullscreen = observation.fullscreen};
    }

    Step dying_step(const Place &place, Box source_device, double captured_scale,
        bool public_owned, MonitorId monitor, double elapsed) {
        auto screen = place.screen;
        if (public_owned) {
            // The model uses unoffset geometry; the snapshot includes workspace/floating offsets.
            screen.x = place.capture.x - source_device.x / captured_scale;
            screen.y = place.capture.y - source_device.y / captured_scale;
        }
        return {.elapsed = elapsed,
            .stalled = elapsed > STALL_SECONDS,
            .body = place.body,
            .capture = place.capture,
            .goal_body = place.goal_body,
            .rest_capture = place.rest_capture,
            .screen = screen,
            .work_area = place.work_area,
            .motion = std::nullopt,
            .dying = true,
            .monitor = monitor,
            .workspace = place.workspace,
            .pinned = place.pinned,
            .scale = place.scale,
            .fullscreen = place.fullscreen};
    }

    Box dying_footprint(const Box &bounds, const Box &screen, double captured_scale) {
        return {screen.x + (bounds.x / captured_scale),
            screen.y + (bounds.y / captured_scale),
            bounds.w / captured_scale,
            bounds.h / captured_scale};
    }
}  // namespace hyprimate::animation::detail
