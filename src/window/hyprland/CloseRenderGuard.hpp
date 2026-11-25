#pragma once

#include <cstdint>
#include <optional>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "HyprWindowAnimations.hpp"

#include "window/CloseBatch.hpp"
#include "window/WindowAnimationEntry.hpp"

class CHyprWindowPort;

// Shared by the submission boundary and the queued pass. Raw model/port pointers are
// consulted only while both owner tickets remain valid.
struct CloseRenderGuard {
    hyprimate::animation::CallbackLifetime::Ticket renderer, model;
    HyprWindowAnimations *animations = nullptr;
    CHyprWindowPort *port = nullptr;
    SP<Desktop::IFadeout> native;
    PHLMONITOR monitor;
    PHLWORKSPACE workspace;
    Desktop::eFadeoutPlane plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
    std::uint64_t fadeout = 0;
    std::optional<hyprimate::animation::Handle> handle;
    bool no_screen_share = false;
    bool public_close_only = false;
    bool native_workspace_fallback = false;
    [[nodiscard]] bool valid(Render::CRenderContext &ctx, bool require_native = false) const;
    void physical_failure(Render::CRenderContext &ctx) const;
    void physical_presented(Render::CRenderContext &ctx, bool submitted) const;
};

