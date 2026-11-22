#pragma once

#include <functional>

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

struct CloseRenderGuard;
// Draws what Hyprland's native window fadeout draws for fadeouts the close renderer does not
// deform: the backdrop behind it (dim around, pre-blur) and its texture. The close renderer decides
// which fadeout draws here and in what batch order; this class only builds the native pass
// elements.
class CNativeFadeoutPass {
  public:
    CNativeFadeoutPass(Render::CRenderContext &ctx, PHLMONITOR monitor);

    // The region a native fadeout is clipped to: the whole monitor.
    [[nodiscard]] CRegion damage() const;

    // Dim-around and pre-blur, drawn behind the fadeout's pixels while it is live.
    void draw_backdrop(
        const Desktop::SFadeoutRenderEffects &effects, const CloseRenderGuard &guard);

    // Build (but do not submit) the native content before queuing any backdrop.
    [[nodiscard]] UP<IPassElement> prepare_texture(const Desktop::IFadeout &fadeout,
        const SP<Render::ITexture> &tex,
        const Desktop::SFadeoutRenderEffects &effects,
        const CloseRenderGuard &guard);

  private:
    std::reference_wrapper<Render::CRenderContext> m_ctx;
    PHLMONITOR m_monitor;
};
