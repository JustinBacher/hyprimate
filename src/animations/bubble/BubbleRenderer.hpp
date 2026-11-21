#pragma once

#include <array>
#include <memory>
#include <optional>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/Shader.hpp>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"

struct SBubbleDraw {
    SP<Render::ITexture> window;
    SP<SBubbleWallpaper> wallpaper;
    hyprimate::BubbleFrame frame;
    Vector2D canvas;
    CBox source;
    hyprimate::Vec2 texture_origin;
    hyprimate::Vec2 texture_size;
    hyprimate::Vec2 output_origin;
    std::array<hyprimate::BubbleParticle, hyprimate::K_BUBBLE_MAX_IMPACTS> glass_particles{};
    std::size_t glass_count = 0;
    bool on_glass = false;  // screen-space Vestige: no window imagery, frame is the deposit's box
    std::optional<hyprimate::BubbleWakeView> wake;  // on the glass: a film trail to draw
    hyprimate::animation::PassKind pass = hyprimate::animation::PassKind::Inert;
};

class CBubbleRenderer {
  public:
    CBubbleRenderer() = default;
    ~CBubbleRenderer();
    CBubbleRenderer(const CBubbleRenderer &) = delete;
    CBubbleRenderer &operator=(const CBubbleRenderer &) = delete;
    CBubbleRenderer(CBubbleRenderer &&) = delete;
    CBubbleRenderer &operator=(CBubbleRenderer &&) = delete;
    SP<Render::IFramebuffer> render(Render::CRenderContext &ctx, const SBubbleDraw &draw);
    // Compiles the material ahead of the first bubble. Compilation takes long enough to read as a
    // frame stall, which would otherwise cancel the opening that first needs it. Safe to call
    // often; only the first call does work.
    bool warm_up();

  private:
    bool initialize();
    std::array<std::unique_ptr<CShader>, 5> m_shaders;
    CBubbleMaterial m_material;
    std::array<hyprimate::BubbleParticle, 2 * hyprimate::K_BUBBLE_MAX_PARTICLES> m_particles{};
    bool m_ready = false;
    bool m_warmed = false;
};

// Deferred screen-only Vestige drawing; window animation draws use the generic pass.
class CBubblePassElement final : public IPassElement {
  public:
    struct SData {
        SP<CBubbleRenderer> renderer;
        SBubbleDraw draw;
        CBox box;
        double scale = 1.0;
        CRegion damage;
    };
    explicit CBubblePassElement(SData data);
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
