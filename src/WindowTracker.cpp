#include "WindowTracker.hpp"

#include <algorithm>
#include <cmath>

namespace liquid_glass {
namespace {

// A translation larger than this many times the window's largest side is a teleport, not a movement.
constexpr double kMaxJumpFactor = 1.5;

bool sameSize(const Box &a, const Box &b) noexcept { return a.w == b.w && a.h == b.h; }

} // namespace

void WindowTracker::track(Box current, Box capture, std::uintptr_t monitor) noexcept {
    m_lastBox = current;
    m_lastCapture = capture;
    m_monitor = monitor;
    m_eligible = false;
    m_wasAnimatingIn = false;
}

void WindowTracker::baseline(Box current, Box capture, std::uintptr_t monitor) noexcept {
    m_lastBox = current;
    m_lastCapture = capture;
    m_monitor = monitor;
}

void WindowTracker::adopt(const WindowSnapshot &snapshot, bool eligible) noexcept {
    m_lastBox = snapshot.current;
    m_lastCapture = snapshot.capture;
    m_monitor = snapshot.monitor;
    m_eligible = eligible;
}

bool WindowTracker::dormant(bool animatingIn) const noexcept {
    return !m_eligible && !m_wasAnimatingIn && !animatingIn;
}

TrackDecision WindowTracker::update(const WindowSnapshot &s) noexcept {
    const bool wasAnimating = m_wasAnimatingIn;
    const bool openingEdge = s.animatingIn && !wasAnimating;
    m_wasAnimatingIn = s.animatingIn;

    const bool eligibleNow = s.deformable && isUsable(s.current) && isUsable(s.capture);

    TrackDecision d;
    if (!eligibleNow && !m_eligible)
        return d;

    d.previousBox = m_lastBox;
    d.previousCapture = m_lastCapture;

    const bool reset = !eligibleNow || !m_eligible || !isUsable(m_lastBox) || !isUsable(m_lastCapture) ||
                       s.monitor != m_monitor || s.elapsed > kStallSeconds;
    if (reset) {
        d.action = TrackAction::Rebaseline;
        d.beginOpen = openingEdge && eligibleNow;
        adopt(s, eligibleNow);
        return d;
    }

    if (s.materialChanged) {
        d.action = TrackAction::Rebaseline;
        adopt(s, eligibleNow);
        return d;
    }

    d.cancelOpening = !s.openEnabled;
    if (openingEdge) {
        d.action = TrackAction::Opening;
        adopt(s, true);
        return d;
    }

    // Native opening and a running transition are not movement impulses. A handoff style's opening box is already
    // fixed at its goal, so what it displays takes over later geometry changes.
    const bool nativeOpen = (s.animatingIn || wasAnimating) && !s.handoff;
    if (nativeOpen || s.transitionBusy) {
        d.action = TrackAction::Busy;
        adopt(s, true);
        return d;
    }

    const Vec2 delta{s.current.x - d.previousBox.x, s.current.y - d.previousBox.y};
    d.translated = delta.x != 0.0 || delta.y != 0.0;
    d.resized = !sameSize(s.current, d.previousBox);
    d.captureChanged = s.capture.x != d.previousCapture.x || s.capture.y != d.previousCapture.y ||
                       !sameSize(s.capture, d.previousCapture);
    const double largest = std::max({s.current.w, s.current.h, d.previousBox.w, d.previousBox.h});
    adopt(s, true);

    if (std::hypot(delta.x, delta.y) > kMaxJumpFactor * largest) {
        d.action = TrackAction::Rebaseline;
        return d;
    }

    d.cancelMovement = s.handoffMoving && !s.movementEnabled;
    if (!d.translated && !d.resized && !d.captureChanged && !d.cancelMovement)
        return d;

    d.action = TrackAction::Move;
    return d;
}

} // namespace liquid_glass
