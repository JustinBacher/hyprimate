#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "SheetClosePass.hpp"
#include "animations/shared/PaperRenderer.hpp"

CPaperClosePassElement::CPaperClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CPaperClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.tex || !m_data.vertices || m_data.vertices->empty()) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &m_data.damage;
    textureData.a = m_data.alpha;
    textureData.allowCustomUV = true;

    // Shade the sheet in its own layer so the tint covers only its pixels; without a layer it draws
    // unlit.
    const Vector2D canvas{std::ceil(m_data.box.w), std::ceil(m_data.box.h)};
    const SPaperDraw paper{
        .window = m_data.tex,
        .shade = m_data.shade,
        .canvas = canvas,
        .meshBox = CBox{0.0, 0.0, m_data.box.w, m_data.box.h},
        .windowVertices = m_data.vertices,
        .shadeVertices = m_data.shadeVertices,
        .alpha = 1.F,
    };
    const auto layer = renderPaper(ctx, paper);
    if (!layer) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, m_data.tex, m_data.box, *m_data.vertices, textureData);
        return {};
    }

    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx,
        layer->getTexture(),
        CBox{m_data.box.x, m_data.box.y, canvas.x, canvas.y},
        paperCanvasQuad(),
        textureData);
    return {};
}

bool CPaperClosePassElement::needsLiveBlur(Render::CRenderContext &) {
    return false;
}

bool CPaperClosePassElement::needsPrecomputeBlur(Render::CRenderContext &) {
    return false;
}

const char *CPaperClosePassElement::passName() {
    return "hyprimate-paper-close";
}

ePassElementType CPaperClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CPaperClosePassElement::boundingBox(Render::CRenderContext &) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CPaperClosePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
