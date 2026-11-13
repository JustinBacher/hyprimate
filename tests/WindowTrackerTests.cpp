#include "Check.hpp"
#include "WindowTracker.hpp"

using liquid_glass::Box;
using liquid_glass::TrackAction;
using liquid_glass::TrackDecision;
using liquid_glass::WindowSnapshot;
using liquid_glass::WindowTracker;

namespace {

constexpr std::uintptr_t kMonitor = 1;
constexpr double         kFrame = 1.0 / 60.0;

// A deformable window whose capture is its body grown by a 10 px margin.
WindowSnapshot snap(double x, double y, double w, double h) {
    WindowSnapshot s;
    s.current = Box{x, y, w, h};
    s.capture = Box{x - 10, y - 10, w + 20, h + 20};
    s.deformable = true;
    s.openEnabled = true;
    s.movementEnabled = true;
    s.monitor = kMonitor;
    s.elapsed = kFrame;
    return s;
}

// A tracker that has observed the window once and is eligible.
WindowTracker eligibleAt(double x, double y, double w, double h) {
    WindowTracker        t;
    const WindowSnapshot s = snap(x, y, w, h);
    t.track(s.current, s.capture, kMonitor);
    t.update(s); // registers eligibility; the rebaseline decision is not under test here
    return t;
}

} // namespace

int main() {
    // A window that is not deformable stays idle, and is dormant so the runtime can skip it.
    {
        WindowTracker  t;
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.deformable = false;
        t.track(s.current, s.capture, kMonitor);
        const TrackDecision d = t.update(s);
        CHECK(d.action == TrackAction::Idle);
        CHECK(!t.eligible());
        CHECK(t.dormant(false));
    }

    // A window that becomes eligible rebaselines, but does not open unless it was animating in.
    {
        WindowTracker        t;
        const WindowSnapshot s = snap(0, 0, 400, 300);
        t.track(s.current, s.capture, kMonitor);
        const TrackDecision d = t.update(s);
        CHECK(d.action == TrackAction::Rebaseline);
        CHECK(!d.beginOpen);
        CHECK(t.eligible());
        CHECK(!t.dormant(false));
    }

    // A window already animating in when it becomes eligible begins the opening pop-out on its first update.
    {
        WindowTracker  t;
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.animatingIn = true;
        t.track(s.current, s.capture, kMonitor);
        const TrackDecision d = t.update(s);
        CHECK(d.action == TrackAction::Rebaseline);
        CHECK(d.beginOpen);
    }

    // A rising opening edge on an eligible window starts the opening without detaching.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.animatingIn = true;
        const TrackDecision d = t.update(s);
        CHECK(d.action == TrackAction::Opening);
    }

    // While the native open animation runs, geometry changes are not movement impulses.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot opening = snap(0, 0, 400, 300);
        opening.animatingIn = true;
        t.update(opening);
        WindowSnapshot moved = snap(50, 0, 400, 300);
        moved.animatingIn = true;
        CHECK(t.update(moved).action == TrackAction::Busy);
    }

    // A transition running in the transformer makes geometry changes non-impulses too.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(50, 0, 400, 300);
        s.transitionBusy = true;
        CHECK(t.update(s).action == TrackAction::Busy);
    }

    // Translation is recorded as movement with the baseline it was compared against.
    {
        WindowTracker       t = eligibleAt(0, 0, 400, 300);
        const TrackDecision d = t.update(snap(30, 0, 400, 300));
        CHECK(d.action == TrackAction::Move);
        CHECK(d.translated);
        CHECK(!d.resized);
        CHECK(d.previousBox.x == 0.0);
    }

    // A resize without translation is movement too, and reports the resize.
    {
        WindowTracker       t = eligibleAt(0, 0, 400, 300);
        const TrackDecision d = t.update(snap(0, 0, 420, 300));
        CHECK(d.action == TrackAction::Move);
        CHECK(!d.translated);
        CHECK(d.resized);
    }

    // Unchanged geometry is idle.
    {
        WindowTracker t = eligibleAt(0, 0, 400, 300);
        CHECK(t.update(snap(0, 0, 400, 300)).action == TrackAction::Idle);
    }

    // A jump larger than 1.5 times the window's largest side is a teleport, so it rebaselines.
    {
        WindowTracker t = eligibleAt(0, 0, 400, 300);
        CHECK(t.update(snap(900, 0, 400, 300)).action == TrackAction::Rebaseline);
        // The new position is now the baseline, so a small step afterwards is a normal move.
        CHECK(t.update(snap(930, 0, 400, 300)).action == TrackAction::Move);
    }

    // A stall (a frame gap beyond the stall limit) rebaselines and keeps the window eligible.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(30, 0, 400, 300);
        s.elapsed = 1.0;
        CHECK(t.update(s).action == TrackAction::Rebaseline);
        CHECK(t.eligible());
    }

    // A window that moved to another monitor rebaselines.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.monitor = kMonitor + 1;
        CHECK(t.update(s).action == TrackAction::Rebaseline);
    }

    // Losing deformability (for example entering fullscreen) rebaselines once and turns the window dormant.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot gone = snap(0, 0, 400, 300);
        gone.deformable = false;
        CHECK(t.update(gone).action == TrackAction::Rebaseline);
        CHECK(!t.eligible());
        CHECK(t.dormant(false));
        CHECK(t.update(gone).action == TrackAction::Idle);
    }

    // A material change rebaselines, and the window stays eligible.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.materialChanged = true;
        CHECK(t.update(s).action == TrackAction::Rebaseline);
        CHECK(t.eligible());
    }

    // Disabling opening cancels an opening in progress.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.openEnabled = false;
        CHECK(t.update(s).cancelOpening);
    }

    // Disabling movement cancels cloth movement in progress, and the change is still reported as movement.
    {
        WindowTracker  t = eligibleAt(0, 0, 400, 300);
        WindowSnapshot s = snap(0, 0, 400, 300);
        s.movementEnabled = false;
        s.clothMoving = true;
        const TrackDecision d = t.update(s);
        CHECK(d.cancelMovement);
        CHECK(d.action == TrackAction::Move);
    }

    // A window whose body box is unusable (zero width) is not eligible.
    {
        WindowTracker  t;
        WindowSnapshot s = snap(0, 0, 0, 300);
        t.track(s.current, s.capture, kMonitor);
        CHECK(t.update(s).action == TrackAction::Idle);
        CHECK(!t.eligible());
    }

    return g_failures == 0 ? 0 : 1;
}
