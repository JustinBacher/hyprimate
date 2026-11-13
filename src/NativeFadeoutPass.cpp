#include "NativeFadeoutPass.hpp"

#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <utility>

CNativeFadeoutPass::CNativeFadeoutPass(Render::CRenderContext &ctx, PHLMONITOR monitor)
    : m_ctx(ctx), m_monitor(std::move(monitor)) {}

CRegion CNativeFadeoutPass::damage() const {
    return CRegion{0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
}

void CNativeFadeoutPass::drawBackdrop(const Desktop::SFadeoutRenderEffects &effects) {
    if (effects.dimAroundAlpha > 0.F) {
        CRectPassElement::SRectData data;
        data.box = {0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
        data.color = CHyprColor(0, 0, 0, effects.dimAroundAlpha);
        Render::IHyprRenderer::addPassElement(m_ctx, makeUnique<CRectPassElement>(data));
    }

    if (effects.preBlur) {
        CRectPassElement::SRectData data;
        data.box = effects.preBlur->box;
        data.color = CHyprColor(0, 0, 0, 0);
        data.blur = true;
        data.blurA = effects.preBlur->alpha;
        data.round = effects.preBlur->round;
        data.roundingPower = effects.preBlur->roundingPower;
        data.xray = effects.preBlur->xray;
        Render::IHyprRenderer::addPassElement(m_ctx, makeUnique<CRectPassElement>(data));
    }
}

void CNativeFadeoutPass::drawTexture(const Desktop::IFadeout &fadeout, const SP<Render::ITexture> &tex,
                                     const Desktop::SFadeoutRenderEffects &effects) {
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

    Render::IHyprRenderer::addPassElement(m_ctx, makeUnique<CTexPassElement>(std::move(data)));
}
