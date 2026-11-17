#include <algorithm>
#include <cmath>
#include <cstdint>

#include "WindowTracker.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {

        // A translation larger than this many times the window's largest side is a teleport, not a
        // movement.
        constexpr double MAX_JUMP_FACTOR = 1.5;

        bool same_size(const Box &first, const Box &second) noexcept {
            return first.w == second.w && first.h == second.h;
        }

        Box translated(Box box, Vec2 offset) noexcept {
            box.x += offset.x;
            box.y += offset.y;
            return box;
        }

    }  // namespace

    void WindowTracker::track(Box current, Box capture, std::uintptr_t monitor) noexcept {
        m_last_box = current;
        m_last_capture = capture;
        m_monitor = monitor;
        m_eligible = false;
        m_was_animating_in = false;
        m_workspace_offset = {};
        m_workspace_movement = true;
        m_workspace = 0;
    }

    void WindowTracker::baseline(Box current, Box capture, std::uintptr_t monitor) noexcept {
        m_last_box = current;
        m_last_capture = capture;
        m_monitor = monitor;
    }

    void WindowTracker::adopt(const WindowSnapshot &snapshot, bool eligible) noexcept {
        m_last_box = snapshot.current;
        m_last_capture = snapshot.capture;
        m_monitor = snapshot.monitor;
        m_eligible = eligible;
        m_workspace_offset = snapshot.workspace_movement ? snapshot.workspace_offset : Vec2{};
        m_workspace_movement = snapshot.workspace_movement;
        m_workspace = snapshot.workspace;
    }

    bool WindowTracker::dormant(bool animating_in) const noexcept {
        return !m_eligible && !m_was_animating_in && !animating_in;
    }

    TrackDecision WindowTracker::update(const WindowSnapshot &snapshot) noexcept {
        const bool WAS_ANIMATING = m_was_animating_in;
        const bool OPENING_EDGE = snapshot.animating_in && !WAS_ANIMATING;
        m_was_animating_in = snapshot.animating_in;

        const bool ELIGIBLE_NOW =
            snapshot.deformable && is_usable(snapshot.current) && is_usable(snapshot.capture);

        TrackDecision decision;
        if (!ELIGIBLE_NOW && !m_eligible) {
            return decision;
        }

        const Vec2 OFFSET = snapshot.workspace_movement ? snapshot.workspace_offset : Vec2{};
        decision.previous_box = translated(m_last_box, m_workspace_offset);
        decision.previous_capture = translated(m_last_capture, m_workspace_offset);
        decision.current_box = translated(snapshot.current, OFFSET);
        decision.current_capture = translated(snapshot.capture, OFFSET);
        decision.workspace_moving = snapshot.workspace_movement && snapshot.workspace_moving;

        const bool RESET = !ELIGIBLE_NOW || !m_eligible || !is_usable(m_last_box) ||
                           !is_usable(m_last_capture) || snapshot.monitor != m_monitor ||
                           snapshot.elapsed > STALL_SECONDS || snapshot.workspace != m_workspace ||
                           snapshot.workspace_movement != m_workspace_movement;
        if (RESET) {
            decision.action = TrackAction::Rebaseline;
            decision.begin_open = OPENING_EDGE && ELIGIBLE_NOW;
            adopt(snapshot, ELIGIBLE_NOW);
            return decision;
        }

        if (snapshot.material_changed) {
            decision.action = TrackAction::Rebaseline;
            adopt(snapshot, ELIGIBLE_NOW);
            return decision;
        }

        decision.cancel_opening = !snapshot.open_enabled;
        if (OPENING_EDGE) {
            decision.action = TrackAction::Opening;
            adopt(snapshot, true);
            return decision;
        }

        // Native opening and a running transition are not movement impulses. A handoff style's
        // opening box is already fixed at its goal, so what it displays takes over later geometry
        // changes.
        const bool NATIVE_OPEN = (snapshot.animating_in || WAS_ANIMATING) && !snapshot.handoff;
        if (NATIVE_OPEN || snapshot.transition_busy) {
            decision.action = TrackAction::Busy;
            adopt(snapshot, true);
            return decision;
        }

        const Vec2 DELTA{
            .x = decision.current_box.x - decision.previous_box.x,
            .y = decision.current_box.y - decision.previous_box.y,
        };
        decision.translated = DELTA.x != 0.0 || DELTA.y != 0.0;
        decision.resized = !same_size(snapshot.current, decision.previous_box);
        decision.capture_changed = decision.current_capture.x != decision.previous_capture.x ||
                                   decision.current_capture.y != decision.previous_capture.y ||
                                   !same_size(snapshot.capture, decision.previous_capture);
        const double LARGEST = std::max({
            snapshot.current.w,
            snapshot.current.h,
            decision.previous_box.w,
            decision.previous_box.h,
        });
        // Only client geometry can teleport. A legitimate workspace slide may travel several
        // window widths between frames, particularly for small windows on a large monitor.
        const Vec2 LAYOUT_DELTA{
            .x = snapshot.current.x - m_last_box.x,
            .y = snapshot.current.y - m_last_box.y,
        };
        adopt(snapshot, true);

        if (std::hypot(LAYOUT_DELTA.x, LAYOUT_DELTA.y) > MAX_JUMP_FACTOR * LARGEST) {
            decision.action = TrackAction::Rebaseline;
            return decision;
        }

        decision.cancel_movement = snapshot.handoff_moving && !snapshot.movement_enabled;
        if (!decision.translated && !decision.resized && !decision.capture_changed &&
            !decision.cancel_movement) {
            return decision;
        }

        decision.action = TrackAction::Move;
        return decision;
    }

}  // namespace hyprimate
