#include <cstdint>

#include "../src/animations/shared/Core.hpp"
#include "Check.hpp"
#include "WindowTracker.hpp"

using hyprimate::Box;
using hyprimate::TrackAction;
using hyprimate::TrackDecision;
using hyprimate::WindowSnapshot;
using hyprimate::WindowTracker;

namespace {

    constexpr std::uintptr_t MONITOR = 1;
    constexpr double FRAME_SECONDS = 1.0 / 60.0;

    // A deformable window whose capture is its body grown by a 10 px margin.
    WindowSnapshot snap(double position_x, double position_y, double width, double height) {
        WindowSnapshot snapshot;
        snapshot.current = Box{.x = position_x, .y = position_y, .w = width, .h = height};
        snapshot.capture =
            Box{.x = position_x - 10, .y = position_y - 10, .w = width + 20, .h = height + 20};
        snapshot.deformable = true;
        snapshot.open_enabled = true;
        snapshot.movement_enabled = true;
        snapshot.monitor = MONITOR;
        snapshot.elapsed = FRAME_SECONDS;
        return snapshot;
    }

    // A tracker that has observed the window once and is eligible.
    WindowTracker eligible_at(double position_x, double position_y, double width, double height) {
        WindowTracker tracker;
        const WindowSnapshot SNAPSHOT = snap(position_x, position_y, width, height);
        tracker.track(SNAPSHOT.current, SNAPSHOT.capture, MONITOR);
        tracker.update(
            SNAPSHOT);  // registers eligibility; the rebaseline decision is not under test here
        return tracker;
    }

}  // namespace

int main() {
    // A window that is not deformable stays idle, and is dormant so the runtime can skip it.
    {
        WindowTracker tracker;
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.deformable = false;
        tracker.track(snapshot.current, snapshot.capture, MONITOR);
        const TrackDecision DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Idle);
        CHECK(!tracker.eligible());
        CHECK(tracker.dormant(false));
    }

    // A window that becomes eligible rebaselines, but does not open unless it was animating in.
    {
        WindowTracker tracker;
        const WindowSnapshot SNAPSHOT = snap(0, 0, 400, 300);
        tracker.track(SNAPSHOT.current, SNAPSHOT.capture, MONITOR);
        const TrackDecision DECISION = tracker.update(SNAPSHOT);
        CHECK(DECISION.action == TrackAction::Rebaseline);
        CHECK(!DECISION.begin_open);
        CHECK(tracker.eligible());
        CHECK(!tracker.dormant(false));
    }

    // A window already animating in when it becomes eligible begins the opening pop-out on its
    // first update.
    {
        WindowTracker tracker;
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.animating_in = true;
        tracker.track(snapshot.current, snapshot.capture, MONITOR);
        const TrackDecision DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Rebaseline);
        CHECK(DECISION.begin_open);
    }

    // A rising opening edge on an eligible window starts the opening without detaching.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.animating_in = true;
        const TrackDecision DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Opening);
    }

    // While the native open animation runs, geometry changes are not movement impulses.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot opening = snap(0, 0, 400, 300);
        opening.animating_in = true;
        tracker.update(opening);
        WindowSnapshot moved = snap(50, 0, 400, 300);
        moved.animating_in = true;
        CHECK(tracker.update(moved).action == TrackAction::Busy);
    }

    // A transition running in the transformer makes geometry changes non-impulses too.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(50, 0, 400, 300);
        snapshot.transition_busy = true;
        CHECK(tracker.update(snapshot).action == TrackAction::Busy);
    }

    // Translation is recorded as movement with the baseline it was compared against.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        const TrackDecision DECISION = tracker.update(snap(30, 0, 400, 300));
        CHECK(DECISION.action == TrackAction::Move);
        CHECK(DECISION.translated);
        CHECK(!DECISION.resized);
        CHECK(DECISION.previous_box.x == 0.0);
    }

    // A resize without translation is movement too, and reports the resize.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        const TrackDecision DECISION = tracker.update(snap(0, 0, 420, 300));
        CHECK(DECISION.action == TrackAction::Move);
        CHECK(!DECISION.translated);
        CHECK(DECISION.resized);
    }

    // Unchanged geometry is idle.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        CHECK(tracker.update(snap(0, 0, 400, 300)).action == TrackAction::Idle);
    }

    // A jump larger than 1.5 times the window's largest side is a teleport, so it rebaselines.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        CHECK(tracker.update(snap(900, 0, 400, 300)).action == TrackAction::Rebaseline);
        // The new position is now the baseline, so a small step afterwards is a normal move.
        CHECK(tracker.update(snap(930, 0, 400, 300)).action == TrackAction::Move);
    }

    // A stall (a frame gap beyond the stall limit) rebaselines and keeps the window eligible.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(30, 0, 400, 300);
        snapshot.elapsed = 1.0;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
        CHECK(tracker.eligible());
    }

    // A window that moved to another monitor rebaselines.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.monitor = MONITOR + 1;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
    }

    // Losing deformability (for example entering fullscreen) rebaselines once and turns the window
    // dormant.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot gone = snap(0, 0, 400, 300);
        gone.deformable = false;
        CHECK(tracker.update(gone).action == TrackAction::Rebaseline);
        CHECK(!tracker.eligible());
        CHECK(tracker.dormant(false));
        CHECK(tracker.update(gone).action == TrackAction::Idle);
    }

    // A material change rebaselines, and the window stays eligible.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.material_changed = true;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
        CHECK(tracker.eligible());
    }

    // Disabling opening cancels an opening in progress.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.open_enabled = false;
        CHECK(tracker.update(snapshot).cancel_opening);
    }

    // Disabling movement cancels cloth movement in progress, and the change is still reported as
    // movement.
    {
        WindowTracker tracker = eligible_at(0, 0, 400, 300);
        WindowSnapshot snapshot = snap(0, 0, 400, 300);
        snapshot.movement_enabled = false;
        snapshot.handoff_moving = true;
        const TrackDecision DECISION = tracker.update(snapshot);
        CHECK(DECISION.cancel_movement);
        CHECK(DECISION.action == TrackAction::Move);
    }

    // A window whose body box is unusable (zero width) is not eligible.
    {
        WindowTracker tracker;
        WindowSnapshot snapshot = snap(0, 0, 0, 300);
        tracker.track(snapshot.current, snapshot.capture, MONITOR);
        CHECK(tracker.update(snapshot).action == TrackAction::Idle);
        CHECK(!tracker.eligible());
    }

    // Workspace slides move the displayed window even when its layout box stays fixed.
    {
        WindowTracker tracker = eligible_at(100, 80, 400, 300);
        auto snapshot = snap(100, 80, 400, 300);
        snapshot.workspace_moving = true;
        snapshot.workspace_offset = {.x = 1600, .y = 0};
        CHECK(tracker.update(snapshot).action ==
              TrackAction::Move);  // outgoing motion must not detach sprites
        snapshot.workspace_offset = {.x = 1500, .y = 0};
        const auto DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Move);
        CHECK(DECISION.translated);
        CHECK(DECISION.current_box.x - DECISION.previous_box.x == -100);
        CHECK(DECISION.current_capture.x - DECISION.previous_capture.x == -100);
        // Layout baselining after a simulation step must retain workspace history.
        tracker.baseline(snapshot.current, snapshot.capture, MONITOR);
        snapshot.workspace_offset = {.x = 1500, .y = 35};
        const auto VERTICAL = tracker.update(snapshot);
        CHECK(VERTICAL.current_box.y - VERTICAL.previous_box.y == 35);
        snapshot.workspace_offset = {.x = 1530, .y = 35};
        const auto REVERSED = tracker.update(snapshot);
        CHECK(REVERSED.current_box.x == 1630);  // interrupted/reversed slide
    }

    // An incoming workspace was ineligible while hidden: its offscreen placement is a baseline,
    // not a screen-wide movement impulse.
    {
        WindowTracker tracker = eligible_at(100, 80, 400, 300);
        auto snapshot = snap(100, 80, 400, 300);
        snapshot.deformable = false;
        tracker.update(snapshot);
        snapshot.deformable = true;
        snapshot.workspace_moving = true;
        snapshot.workspace_offset = {.x = 1600, .y = 0};
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
        snapshot.workspace_offset = {.x = 1500, .y = 0};
        CHECK(tracker.update(snapshot).action == TrackAction::Move);
    }

    // The global opt-out removes only workspace movement, not ordinary window motion.
    {
        WindowTracker tracker = eligible_at(100, 80, 400, 300);
        auto snapshot = snap(100, 80, 400, 300);
        snapshot.workspace_movement = false;
        snapshot.workspace_moving = true;
        snapshot.workspace_offset = {.x = 1600, .y = 0};
        tracker.update(snapshot);  // live setting change rebaselines without an impulse
        snapshot.workspace_offset = {.x = 1200, .y = 0};
        CHECK(tracker.update(snapshot).action == TrackAction::Idle);
        snapshot.current.x += 20;
        snapshot.capture.x += 20;
        const auto DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Move);
        CHECK(DECISION.current_box.x - DECISION.previous_box.x == 20);
        snapshot.workspace_movement = true;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
    }

    // Arrival at zero offset retains settlement; fade-only transitions stay idle.
    {
        WindowTracker tracker = eligible_at(100, 80, 400, 300);
        auto snapshot = snap(100, 80, 400, 300);
        snapshot.workspace_moving = true;
        snapshot.workspace_offset = {.x = 10, .y = 0};
        tracker.update(snapshot);
        snapshot.workspace_offset = {};
        snapshot.workspace_moving = false;
        const auto ARRIVAL = tracker.update(snapshot);
        CHECK(ARRIVAL.action == TrackAction::Move);
        CHECK(ARRIVAL.current_box.x - ARRIVAL.previous_box.x == -10);
        CHECK(tracker.update(snapshot).action == TrackAction::Idle);
    }

    // A fast compositor slide is not a client teleport, even for a small window.
    {
        WindowTracker tracker = eligible_at(100, 80, 120, 100);
        auto snapshot = snap(100, 80, 120, 100);
        snapshot.workspace_moving = true;
        snapshot.workspace_offset = {.x = 3000, .y = 0};
        tracker.update(snapshot);
        snapshot.workspace_offset = {.x = 2300, .y = 0};
        snapshot.current.x += 20;
        snapshot.capture.x += 20;
        const auto DECISION = tracker.update(snapshot);
        CHECK(DECISION.action == TrackAction::Move);
        CHECK(DECISION.current_box.x - DECISION.previous_box.x == -680);
        // Moving a window between workspaces is a discontinuity, not a slide.
        snapshot.workspace = 2;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
        snapshot.deformable = false;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
        snapshot.workspace_moving = false;
        snapshot.workspace_offset = {};
        CHECK(tracker.update(snapshot).action == TrackAction::Idle);
        snapshot.deformable = true;
        CHECK(tracker.update(snapshot).action == TrackAction::Rebaseline);
    }

    return g_failures == 0 ? 0 : 1;
}
