#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Region.hpp>

#include "HyprAnimationCanvas.hpp"
#include "HyprWindowAnimations.hpp"
#include "WindowAnimationEntry.hpp"

class CWindowAnimationPass final : public IPassElement {
  public:
    struct SData {
        CHyprAnimationCanvas::Resources *resources = nullptr;
        HyprWindowAnimations *animations = nullptr;
        hyprimate::animation::Handle handle{};
        CHyprAnimationCanvas::Image image;
        hyprimate::Box source, output, canvas_box;
        hyprimate::Vec2 image_origin, image_size;
        double scale = 1;
        float alpha = 1;
        hyprimate::animation::FrameId frame = 0;
        hyprimate::animation::MonitorId monitor = 0;
        CRegion damage;
        bool retained_tail = false;
        bool no_screen_share = false;
    };
    explicit CWindowAnimationPass(SData data);
    std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override;
    bool needsLiveBlur(Render::CRenderContext &ctx) override;
    bool needsPrecomputeBlur(Render::CRenderContext &ctx) override;
    const char *passName() override;
    ePassElementType type() override;
    std::optional<CBox> boundingBox(Render::CRenderContext &ctx) override;
    CRegion opaqueRegion(Render::CRenderContext &ctx) override;
  private:
    SData m_data;
};
