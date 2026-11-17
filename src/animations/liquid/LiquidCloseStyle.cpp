#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>

#include "LiquidClosePass.hpp"

CLiquidClosePassElement::CLiquidClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CLiquidClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.tex || (m_data.vertices == nullptr) || m_data.vertices->empty()) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &m_data.damage;
    textureData.a = m_data.alpha;
    textureData.allowCustomUV = true;
    // Edge liquid first, so the window mesh draws over the meniscus where they meet.
    if ((m_data.liquid != nullptr) && m_data.solid) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, m_data.solid, m_data.liquidBox, *m_data.liquid, textureData);
    }
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
    if ((m_data.liquid != nullptr) && m_data.solid) {
        const double MIN_X = std::min(box.x, m_data.liquidBox.x);
        const double MIN_Y = std::min(box.y, m_data.liquidBox.y);
        const double MAX_X = std::max(box.x + box.w, m_data.liquidBox.x + m_data.liquidBox.w);
        const double MAX_Y = std::max(box.y + box.h, m_data.liquidBox.y + m_data.liquidBox.h);
        box = CBox{MIN_X, MIN_Y, MAX_X - MIN_X, MAX_Y - MIN_Y};
    }
    return box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CLiquidClosePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
