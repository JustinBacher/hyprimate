#include <algorithm>
#include <cmath>

#include "WindowTracker.hpp"

namespace hyprimate {
    namespace {

        // A translation larger than this many times the window's largest side is a teleport, not a
        // movement.
        constexpr double kMaxJumpFactor = 1.5;

        bool sameSize(const Box &a, const Box &b) noexcept {
            return a.w == b.w && a.h == b.h;
        }

        Box translated(Box box, Vec2 offset) noexcept {
            box.x += offset.x;
            box.y += offset.y;
            return box;
        }

    }  // namespace

    void WindowTracker::track(Box current, Box capture, std::uintptr_t monitor) noexcept {
        m_lastBox = current;
        m_lastCapture = capture;
        m_monitor = monitor;
        m_eligible = false;
        m_wasAnimatingIn = false;
        m_workspaceOffset = {};
        m_workspaceMoving = false;
        m_workspaceMovement = true;
        m_workspace = 0;
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
        m_workspaceOffset = snapshot.workspaceMovement ? snapshot.workspaceOffset : Vec2{};
        m_workspaceMoving = snapshot.workspaceMovement && snapshot.workspaceMoving;
        m_workspaceMovement = snapshot.workspaceMovement;
        m_workspace = snapshot.workspace;
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
        if (!eligibleNow && !m_eligible) return d;

        const Vec2 offset = s.workspaceMovement ? s.workspaceOffset : Vec2{};
        d.previousBox = translated(m_lastBox, m_workspaceOffset);
        d.previousCapture = translated(m_lastCapture, m_workspaceOffset);
        d.currentBox = translated(s.current, offset);
        d.currentCapture = translated(s.capture, offset);
        d.workspaceMoving = s.workspaceMovement && s.workspaceMoving;

        const bool reset = !eligibleNow || !m_eligible || !isUsable(m_lastBox) ||
                           !isUsable(m_lastCapture) || s.monitor != m_monitor ||
                           s.elapsed > kStallSeconds || s.workspace != m_workspace ||
                           s.workspaceMovement != m_workspaceMovement ||
                           (d.workspaceMoving && !m_workspaceMoving &&
                               (offset.x != 0.0 || offset.y != 0.0));
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

        // Native opening and a running transition are not movement impulses. A handoff style's
        // opening box is already fixed at its goal, so what it displays takes over later geometry
        // changes.
        const bool nativeOpen = (s.animatingIn || wasAnimating) && !s.handoff;
        if (nativeOpen || s.transitionBusy) {
            d.action = TrackAction::Busy;
            adopt(s, true);
            return d;
        }

        const Vec2 delta{
            d.currentBox.x - d.previousBox.x, d.currentBox.y - d.previousBox.y};
        d.translated = delta.x != 0.0 || delta.y != 0.0;
        d.resized = !sameSize(s.current, d.previousBox);
        d.captureChanged = d.currentCapture.x != d.previousCapture.x ||
                           d.currentCapture.y != d.previousCapture.y ||
                           !sameSize(s.capture, d.previousCapture);
        const double largest =
            std::max({s.current.w, s.current.h, d.previousBox.w, d.previousBox.h});
        // Only client geometry can teleport. A legitimate workspace slide may travel several
        // window widths between frames, particularly for small windows on a large monitor.
        const Vec2 layoutDelta{s.current.x - m_lastBox.x, s.current.y - m_lastBox.y};
        adopt(s, true);

        if (std::hypot(layoutDelta.x, layoutDelta.y) > kMaxJumpFactor * largest) {
            d.action = TrackAction::Rebaseline;
            return d;
        }

        d.cancelMovement = s.handoffMoving && !s.movementEnabled;
        if (!d.translated && !d.resized && !d.captureChanged && !d.cancelMovement) return d;

        d.action = TrackAction::Move;
        return d;
    }

}  // namespace hyprimate
