#pragma once

#include <hyprutils/math/Vector2D.hpp>

#include "shared/Core.hpp"
#include "AnimationCloseRenderer.hpp"

namespace hyprimate::animation::close_detail {
    bool capture_or_replay(const Render::CRenderContext &ctx);
    bool physical_monitor(const Render::CRenderContext &ctx, const PHLMONITOR &monitor);
    bool supported_close_effects(const Desktop::SFadeoutRenderEffects &effects);
    bool same_box(const hyprimate::Box &a, const hyprimate::Box &b);
    hyprimate::Box monitor_box(const PHLMONITOR &monitor);
    bool same_extent(const Vector2D &extent, const hyprimate::Vec2 &expected);
    bool usable_monitor_image(const SP<Render::IFramebuffer> &image, const hyprimate::Vec2 &extent);
    hyprimate::Box clip_monitor(const hyprimate::Box &box, const hyprimate::Vec2 &extent);
}  // namespace hyprimate::animation::close_detail
