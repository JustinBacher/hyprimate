#pragma once

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

// A single background-only GPU capture, shared with the close record if opening is interrupted.
struct SBubbleWallpaper {
    SP<Render::IFramebuffer> framebuffer;
    Vector2D origin;
    Vector2D axisX;
    Vector2D axisY;
    bool queued = false;
};

void queueBubbleWallpaper(
    Render::CRenderContext &, const SP<SBubbleWallpaper> &, const CBox &source);
