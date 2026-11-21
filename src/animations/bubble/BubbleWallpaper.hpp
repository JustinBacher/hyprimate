#pragma once

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include "WindowAnimationFacts.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeResource.hpp"

// A GPU scene capture retained by the animation while it needs a backdrop.
struct SBubbleWallpaper {
    ~SBubbleWallpaper() { hyprimate::gl::release_native(framebuffer, owner); }
    // A capture can outlive the backend that created it; reject stale exposure.
    mutable hyprimate::gl::CompositorContextOwner owner;
    SP<Render::IFramebuffer> framebuffer;
    Vector2D origin;
    Vector2D axis_x;
    Vector2D axis_y;
    bool queued = false;
};

void queue_bubble_wallpaper(Render::CRenderContext &ctx,
    const SP<SBubbleWallpaper> &wallpaper,
    const CBox &source,
    bool live,
    const CBox &bounds, hyprimate::animation::PassKind pass);
