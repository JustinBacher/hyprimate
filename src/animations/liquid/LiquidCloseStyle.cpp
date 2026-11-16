#include <algorithm>

#include <hyprland/src/render/OpenGL.hpp>

#include "LiquidCloseStyle.hpp"

CLiquidClosePassElement::CLiquidClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CLiquidClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.tex || !m_data.vertices || m_data.vertices->empty()) return {};

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &m_data.damage;
    textureData.a = m_data.alpha;
    textureData.allowCustomUV = true;
    // Edge liquid first, so the window mesh draws over the meniscus where they meet.
    if (m_data.liquid && m_data.solid)
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, m_data.solid, m_data.liquidBox, *m_data.liquid, textureData);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, m_data.tex, m_data.box, *m_data.vertices, textureData);
    return {};
}

bool CLiquidClosePassElement::needsLiveBlur(Render::CRenderContext &) {
    return false;
}

bool CLiquidClosePassElement::needsPrecomputeBlur(Render::CRenderContext &) {
    return false;
}

const char *CLiquidClosePassElement::passName() {
    return "hyprimate-close";
}

ePassElementType CLiquidClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CLiquidClosePassElement::boundingBox(Render::CRenderContext &) {
    // Same convention as CTexPassElement: monitor-local logical coordinates. The edge liquid widens
    // the box.
    CBox box = m_data.box;
    if (m_data.liquid && m_data.solid) {
        const double x0 = std::min(box.x, m_data.liquidBox.x);
        const double y0 = std::min(box.y, m_data.liquidBox.y);
        const double x1 = std::max(box.x + box.w, m_data.liquidBox.x + m_data.liquidBox.w);
        const double y1 = std::max(box.y + box.h, m_data.liquidBox.y + m_data.liquidBox.h);
        box = CBox{x0, y0, x1 - x0, y1 - y0};
    }
    return box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CLiquidClosePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
