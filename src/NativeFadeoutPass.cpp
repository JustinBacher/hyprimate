#include <utility>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "NativeFadeoutPass.hpp"
#include "AnimationCloseRenderer.hpp"

namespace {
// Preserve native dispatch and its per-element damage. The host asks type() immediately
// before drawing native data; a revoked item instead becomes a no-op custom pass.
template<class Native>
class CGuardedClosePass final : public Native {
  public:
    template<class Data>
    CGuardedClosePass(Data &&data, const CloseRenderGuard &guard, Render::CRenderContext &ctx)
        : Native(std::forward<Data>(data)), m_guard(guard), m_context(ctx) {}
    ePassElementType type() override {
        return m_guard.valid(m_context.get(), true) ? Native::type() : EK_CUSTOM;
    }
    const char *passName() override { return "hyprimate-native-close"; }
    CRegion opaqueRegion(Render::CRenderContext &) override { return {}; }
    bool disableSimplification(Render::CRenderContext &) override { return true; }
    std::vector<UP<IPassElement>> draw(Render::CRenderContext &) override { return {}; }
  private:
    CloseRenderGuard m_guard;
    std::reference_wrapper<Render::CRenderContext> m_context;
};
}

CNativeFadeoutPass::CNativeFadeoutPass(Render::CRenderContext &ctx, PHLMONITOR monitor)
    : m_ctx(ctx), m_monitor(std::move(monitor)) {
}

CRegion CNativeFadeoutPass::damage() const {
    return CRegion{0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
}

void CNativeFadeoutPass::draw_backdrop(
    const Desktop::SFadeoutRenderEffects &effects, const CloseRenderGuard &guard) {
    if (!guard.valid(m_ctx.get(), true)) { return; }
    if (effects.dimAroundAlpha > 0.F) {
        CRectPassElement::SRectData data;
        data.box = {0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
        data.color = CHyprColor(0, 0, 0, effects.dimAroundAlpha);
        Render::IHyprRenderer::addPassElement(m_ctx.get(),
            makeUnique<CGuardedClosePass<CRectPassElement>>(data, guard, m_ctx.get()));
    }

    if (guard.valid(m_ctx.get(), true) && effects.preBlur) {
        CRectPassElement::SRectData data;
        data.box = effects.preBlur->box;
        data.color = CHyprColor(0, 0, 0, 0);
        data.blur = true;
        data.blurA = effects.preBlur->alpha;
        data.round = effects.preBlur->round;
        data.roundingPower = effects.preBlur->roundingPower;
        data.xray = effects.preBlur->xray;
        Render::IHyprRenderer::addPassElement(m_ctx.get(),
            makeUnique<CGuardedClosePass<CRectPassElement>>(data, guard, m_ctx.get()));
    }
}

UP<IPassElement> CNativeFadeoutPass::prepare_texture(const Desktop::IFadeout &fadeout,
    const SP<Render::ITexture> &tex,
    const Desktop::SFadeoutRenderEffects &effects, const CloseRenderGuard &guard) {
    CTexPassElement::SRenderData data;
    data.tex = tex;
    data.box = fadeout.renderBox();
    data.a = fadeout.alpha();
    data.damage = damage();
    data.blur = effects.textureBlur.enabled;
    data.blurA = effects.textureBlur.alpha;
    data.forceBlurBlend = effects.textureBlur.forceBlend;
    data.blurShapeInvalid = true;
    data.ignoreAlpha = effects.textureBlur.ignoreAlpha;
    data.blockBlurOptimization = effects.textureBlur.blockBlurOptimization;

    return makeUnique<CGuardedClosePass<CTexPassElement>>(std::move(data), guard, m_ctx.get());
}
