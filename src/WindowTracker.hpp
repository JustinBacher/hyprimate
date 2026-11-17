#pragma once

#include <cstdint>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // One window's observations for one frame, taken on the window's own monitor.
    struct WindowSnapshot {
        Box current;  // body box (GEOMETRIC_CURRENT), logical px
        Box capture;  // decorated + shadow box, logical px
        bool deformable = false;  // plugin enabled, not inert, mapped, not fullscreen
        bool animatingIn = false;  // the host's open animation is running
        bool handoff =
            false;  // the style (cloth, swarm) draws its own open or movement at the goal box
        bool transitionBusy = false;  // a liquid edge transition or sheet motion is running
        bool materialChanged =
            false;  // the style or material changed under an existing transformer
        bool handoffMoving = false;  // that style's movement is running on the transformer
        bool openEnabled = false;
        bool movementEnabled = false;
        std::uintptr_t monitor = 0;  // identity of the window's monitor
        double elapsed = 0.0;  // seconds since this window's previous update
        Vec2 workspaceOffset{};  // presentation displacement, never client geometry
        bool workspaceMoving = false;
        bool workspaceMovement = true;  // global opt-in, independent of movementEnabled
        std::uintptr_t workspace = 0;
    };

    // What the runtime does for a window this frame. The tracker never touches the transformer
    // itself.
    enum class TrackAction {
        Idle,  // nothing changed; an active transformer still steps
        Rebaseline,  // detach any deformation and adopt the snapshot as the new baseline
        Opening,  // adopt the snapshot and begin the opening pop-out from the window's edge
        Busy,  // adopt the snapshot without a movement impulse (native open or running transition)
        Move,  // adopt the snapshot and record the observed movement
    };

    struct TrackDecision {
        TrackAction action = TrackAction::Idle;
        // Rebaseline only: begin the opening after detaching (the window became eligible while
        // opening).
        bool beginOpen = false;
        // Cancel any opening in progress; the open setting is off.
        bool cancelOpening = false;
        // A handoff style's movement is in progress while movement is off; cancel it.
        bool cancelMovement = false;
        // Movement since the previous baseline. Move uses these to record the impulse.
        bool translated = false;
        bool resized = false;
        bool captureChanged = false;
        Box previousBox{};  // baseline body box the snapshot was compared against
        Box previousCapture{};  // baseline capture box the snapshot was compared against
        Box currentBox{};  // screen-space body and capture, including workspace displacement
        Box currentCapture{};
        bool workspaceMoving = false;
    };

    // Per-window tracking state. Eligibility is one rule: the window is deformable, its body and
    // capture boxes are usable. The baseline is the last observed body and capture box on the
    // monitor the window was last seen on.
    class WindowTracker {
      public:
        // Starts tracking a window at its current observation. Its opening edge is registered on
        // the first update, so a window that is already animating in counts as a rising edge then.
        void track(Box current, Box capture, std::uintptr_t monitor) noexcept;

        // Re-adopts the baseline without a decision (layout reset, e.g. a config change).
        void baseline(Box current, Box capture, std::uintptr_t monitor) noexcept;

        // Classifies this frame's snapshot and adopts it as the baseline when the action requires
        // it.
        TrackDecision update(const WindowSnapshot &snapshot) noexcept;

        // True when the window is ineligible and its opening-edge state is clear: nothing can
        // change until its eligibility rises or it begins animating.
        bool dormant(bool animatingIn) const noexcept;

        bool eligible() const noexcept {
            return m_eligible;
        }

      private:
        void adopt(const WindowSnapshot &snapshot, bool eligible) noexcept;

        Box m_lastBox{};
        Box m_lastCapture{};
        std::uintptr_t m_monitor = 0;
        bool m_eligible = false;
        bool m_wasAnimatingIn = false;
        Vec2 m_workspaceOffset{};
        bool m_workspaceMoving = false;
        bool m_workspaceMovement = true;
        std::uintptr_t m_workspace = 0;
    };

}  // namespace hyprimate
