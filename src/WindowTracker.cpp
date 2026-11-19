#include "WindowTracker.hpp"

#include <algorithm>
#include <cmath>

namespace hyprimate::animation {
namespace {
Box translated(Box box, Vec2 offset) noexcept {
    box.x += offset.x;
    box.y += offset.y;
    return box;
}
bool same_box(const Box &first, const Box &second) noexcept {
    return first.x == second.x && first.y == second.y && first.w == second.w && first.h == second.h;
}
}

std::optional<Motion> observe_motion(const Observation &previous, const Observation &current,
    bool previous_workspace_movement, bool workspace_movement) noexcept {
    if (previous.monitor != current.monitor || previous.workspace != current.workspace ||
        previous_workspace_movement != workspace_movement ||
        !is_usable(previous.body) || !is_usable(previous.capture) ||
        !is_usable(current.body) || !is_usable(current.capture)) {
        return std::nullopt;
    }
    const auto OLD_OFFSET = workspace_movement ? previous.workspace_offset : Vec2{};
    const auto NEW_OFFSET = workspace_movement ? current.workspace_offset : Vec2{};
    const auto OLD_BODY = translated(previous.body, OLD_OFFSET);
    const auto NEW_BODY = translated(current.body, NEW_OFFSET);
    const auto OLD_CAPTURE = translated(previous.capture, OLD_OFFSET);
    const auto NEW_CAPTURE = translated(current.capture, NEW_OFFSET);
    const bool MOVED = OLD_BODY.x != NEW_BODY.x || OLD_BODY.y != NEW_BODY.y;
    const bool RESIZED = OLD_BODY.w != NEW_BODY.w || OLD_BODY.h != NEW_BODY.h;
    const bool CAPTURE_CHANGED = !same_box(OLD_CAPTURE, NEW_CAPTURE);
    const double LARGEST = std::max({previous.body.w, previous.body.h, current.body.w, current.body.h});
    // Workspace slides can be arbitrarily large; only client geometry can teleport.
    const double JUMP = std::hypot(current.body.x - previous.body.x, current.body.y - previous.body.y);
    if (JUMP > 1.5 * LARGEST || (!MOVED && !RESIZED && !CAPTURE_CHANGED)) {
        return std::nullopt;
    }
    return Motion{.from_body = OLD_BODY, .from_capture = OLD_CAPTURE, .body = NEW_BODY,
        .capture = NEW_CAPTURE, .translated = MOVED, .resized = RESIZED,
        .capture_changed = CAPTURE_CHANGED, .layout_animated = current.layout_animated,
        .workspace_moving = workspace_movement && current.workspace_moving};
}
} // namespace hyprimate::animation
