#pragma once

namespace hyprimate::workspace {

    enum class CaptureKind { SourceIsolated, DestinationLive, DestinationIsolated };

    // Only the live destination stands in for a native output scene.
    constexpr bool blocks_surface_feedback(CaptureKind kind) {
        return kind != CaptureKind::DestinationLive;
    }

    // Inventory previews are isolated captures, but not either endpoint of a turn.
    constexpr bool is_workspace_switch_capture(CaptureKind kind) {
        return kind != CaptureKind::DestinationIsolated;
    }

    constexpr bool native_workspace_switch(bool offset_animating, bool alpha_animating,
        bool gesture, bool participating, bool displaced) {
        return offset_animating || alpha_animating || (gesture && participating && displaced);
    }

}  // namespace hyprimate::workspace
