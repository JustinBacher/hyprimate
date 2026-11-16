#pragma once

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

// Draws what Hyprland's native window fadeout draws for fadeouts the close renderer does not
// deform: the backdrop behind it (dim around, pre-blur) and its texture. The close renderer decides
// which fadeout draws here and in what batch order; this class only builds the native pass
// elements.
class CNativeFadeoutPass {
  public:
    CNativeFadeoutPass(Render::CRenderContext &ctx, PHLMONITOR monitor);

    // The region a native fadeout is clipped to: the whole monitor.
    CRegion damage() const;

    // Dim-around and pre-blur, drawn behind the fadeout's pixels while it is live.
    void drawBackdrop(const Desktop::SFadeoutRenderEffects &effects);

    // The fadeout's texture at its native box, with its native texture blur.
    void drawTexture(const Desktop::IFadeout &fadeout,
        const SP<Render::ITexture> &tex,
        const Desktop::SFadeoutRenderEffects &effects);

  private:
    Render::CRenderContext &m_ctx;
    PHLMONITOR m_monitor;
};
