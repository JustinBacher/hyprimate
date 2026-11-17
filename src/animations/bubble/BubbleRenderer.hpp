#pragma once

#include <array>
#include <optional>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"

struct SBubbleDraw {
    SP<Render::ITexture> window;
    SP<SBubbleWallpaper> wallpaper;
    hyprimate::BubbleFrame frame;
    Vector2D canvas;
    CBox source;
    hyprimate::Vec2 textureOrigin;
    hyprimate::Vec2 textureSize;
    hyprimate::Vec2 outputOrigin;
    std::array<hyprimate::BubbleParticle, hyprimate::kBubbleMaxImpacts> glassParticles{};
    std::size_t glassCount = 0;
    bool onGlass = false;  // screen-space residue: no window imagery, frame is the deposit's box
    std::optional<hyprimate::BubbleWakeView> wake{};  // on the glass: a film trail to draw
};

class CBubbleRenderer {
  public:
    CBubbleRenderer() = default;
    ~CBubbleRenderer();
    CBubbleRenderer(const CBubbleRenderer &) = delete;
    CBubbleRenderer &operator=(const CBubbleRenderer &) = delete;
    CBubbleRenderer(CBubbleRenderer &&) = delete;
    CBubbleRenderer &operator=(CBubbleRenderer &&) = delete;
    SP<Render::IFramebuffer> render(Render::CRenderContext &, const SBubbleDraw &);
    // Compiles the material ahead of the first bubble. Compilation takes long enough to read as a
    // frame stall, which would otherwise cancel the opening that first needs it. Safe to call
    // often; only the first call does work.
    void warmUp();

  private:
    CBubbleMaterial m_material;
    std::array<hyprimate::BubbleParticle, 2 * hyprimate::kBubbleMaxParticles> m_particles{};
    bool m_warmed = false;
};

class CBubblePassElement final : public IPassElement {
  public:
    struct SData {
        SP<CBubbleRenderer> renderer;
        SBubbleDraw draw;
        CBox box;
        float alpha = 1.F;
        double scale = 1.0;
        CRegion damage;
    };
    explicit CBubblePassElement(SData data);
    std::vector<UP<IPassElement>> draw(Render::CRenderContext &) override;
    bool needsLiveBlur(Render::CRenderContext &) override;
    bool needsPrecomputeBlur(Render::CRenderContext &) override;
    const char *passName() override;
    ePassElementType type() override;
    std::optional<CBox> boundingBox(Render::CRenderContext &) override;
    CRegion opaqueRegion(Render::CRenderContext &) override;

  private:
    SData m_data;
};
