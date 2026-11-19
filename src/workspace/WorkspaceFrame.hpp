#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include "workspace/WorkspaceJourney.hpp"

namespace hyprimate::workspace {
    // Full transformed-monitor textures, using the native framebuffer orientation.
    // RGB keeps the native accumulated backdrop (including blur and shadows).
    // Foreground premultiplied RGB = accumulated.rgb - backdrop.rgb * (1-alpha).
    struct WorkspaceLayer {
        SP<Render::IFramebuffer> accumulated;
        SP<Render::IFramebuffer> backdrop;
    };

    // Cropped planes sample local UV [0,1]. Rectangles and temporal geometry are
    // presentation-only: rendering must never rewrite the retained native capture.
    struct WorkspaceFloating {
        std::int64_t identity = 0;
        CBox rect; // Normalized monitor coordinates, x right and y down; includes decorations.
        WorkspaceLayer layer;
        bool cropped = true;
        float opacity = 1.F;
        WorkspaceLayer previous_layer;
        float live_mix = 1.F;
        double order = 0.0; // Native stacking order, interpolated only in presentation metadata.
    };

    struct WorkspaceCapture {
        std::int64_t identity = 0;
        JourneyEndpoint endpoint;
        SP<Render::IFramebuffer> composite;
        SP<Render::IFramebuffer> wallpaper;
        WorkspaceLayer bottom;
        WorkspaceLayer windows;
        WorkspaceLayer top;
        std::shared_ptr<std::vector<WorkspaceFloating>> floating;
        bool layered = false;
        std::shared_ptr<const WorkspaceCapture> previous;
        // At most one immutable prior generation; never a linked history.
        double live_mix = 1.0;
    };

    struct WorkspacePreview {
        // Both images, when present, are plugin-owned, mip-complete thumbnails.
        JourneyEndpoint endpoint;
        SP<Render::IFramebuffer> image;
        SP<Render::IFramebuffer> previous_image;
        float live_mix = 1.F;
    };

    struct WorkspaceFrame {
        WorkspaceCapture source;
        WorkspaceCapture destination;
        SP<Render::IFramebuffer> presentation;
        std::shared_ptr<const std::vector<WorkspacePreview>> neighbors;
        bool native_hud = false;
    };
}
