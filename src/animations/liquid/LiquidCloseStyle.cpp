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

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &m_data.damage;
    texture_data.a = m_data.alpha;
    texture_data.allowCustomUV = true;
    // Edge liquid first, so the window mesh draws over the meniscus where they meet.
    if ((m_data.liquid != nullptr) && m_data.solid) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, m_data.solid, m_data.liquid_box, *m_data.liquid, texture_data);
    }
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, m_data.tex, m_data.box, *m_data.vertices, texture_data);
    return {};
}

bool CLiquidClosePassElement::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

bool CLiquidClosePassElement::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

const char *CLiquidClosePassElement::passName() {
    return "hyprimate-close";
}

ePassElementType CLiquidClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CLiquidClosePassElement::boundingBox(
    [[maybe_unused]] Render::CRenderContext &ctx) {
    // Same convention as CTexPassElement: monitor-local logical coordinates. The edge liquid widens
    // the box.
    CBox box = m_data.box;
    if ((m_data.liquid != nullptr) && m_data.solid) {
        const double MIN_X = std::min(box.x, m_data.liquid_box.x);
        const double MIN_Y = std::min(box.y, m_data.liquid_box.y);
        const double MAX_X = std::max(box.x + box.w, m_data.liquid_box.x + m_data.liquid_box.w);
        const double MAX_Y = std::max(box.y + box.h, m_data.liquid_box.y + m_data.liquid_box.h);
        box = CBox{MIN_X, MIN_Y, MAX_X - MIN_X, MAX_Y - MIN_Y};
    }
    return box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CLiquidClosePassElement::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
