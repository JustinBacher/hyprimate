#pragma once

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include <array>

struct SBubbleDraw {
    SP<Render::ITexture>      window;
    SP<SBubbleWallpaper>      wallpaper;
    liquid_glass::BubbleFrame frame;
    Vector2D                  canvas;
    CBox                      source;
    liquid_glass::Vec2        textureOrigin;
    liquid_glass::Vec2        textureSize;
    liquid_glass::Vec2        outputOrigin;
};

class CBubbleRenderer {
  public:
    CBubbleRenderer() = default;
    ~CBubbleRenderer();
    CBubbleRenderer(const CBubbleRenderer &) = delete;
    CBubbleRenderer         &operator=(const CBubbleRenderer &) = delete;
    SP<Render::IFramebuffer> render(Render::CRenderContext &, const SBubbleDraw &);

  private:
    bool                initialize();
    std::array<int, 29> m_uniforms{};
    unsigned int        m_program = 0;
    unsigned int        m_vao = 0;
    bool                m_tried = false;
};

class CBubbleClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<CBubbleRenderer> renderer;
        SBubbleDraw         draw;
        CBox                box;
        float               alpha = 1.F;
        double              scale = 1.0;
        CRegion             damage;
    };
    explicit CBubbleClosePassElement(SData data);
    std::vector<UP<IPassElement>> draw(Render::CRenderContext &) override;
    bool                          needsLiveBlur(Render::CRenderContext &) override;
    bool                          needsPrecomputeBlur(Render::CRenderContext &) override;
    const char                   *passName() override;
    ePassElementType              type() override;
    std::optional<CBox>           boundingBox(Render::CRenderContext &) override;
    CRegion                       opaqueRegion(Render::CRenderContext &) override;

  private:
    SData m_data;
};
