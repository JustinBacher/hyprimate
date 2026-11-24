#pragma once

namespace hyprimate::workspace {

    enum class CaptureKind { SourceIsolated, DestinationLive, DestinationIsolated };

    // Only the live destination stands in for a native output scene.
    constexpr bool blocks_surface_feedback(CaptureKind kind) {
        return kind != CaptureKind::DestinationLive;
    }

}  // namespace hyprimate::workspace
