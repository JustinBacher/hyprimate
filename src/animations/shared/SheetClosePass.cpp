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
#include "animations/shared/SheetRenderer.hpp"

CPaperClosePassElement::CPaperClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CPaperClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.tex || (m_data.vertices == nullptr) || m_data.vertices->empty()) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &m_data.damage;
    texture_data.a = m_data.alpha;
    texture_data.allowCustomUV = true;

    // Shade the sheet in its own layer so the tint covers only its pixels; without a layer it draws
    // unlit.
    const Vector2D CANVAS{std::ceil(m_data.box.w), std::ceil(m_data.box.h)};
    const SSheetDraw SHEET{
        .window = m_data.tex,
        .shade = m_data.shade,
        .canvas = CANVAS,
        .mesh_box = CBox{0.0, 0.0, m_data.box.w, m_data.box.h},
        .window_vertices = m_data.vertices,
        .shade_vertices = m_data.shade_vertices,
        .alpha = 1.F,
    };
    const auto LAYER = render_sheet(ctx, SHEET);
    if (!LAYER) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, m_data.tex, m_data.box, *m_data.vertices, texture_data);
        return {};
    }

    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx,
        LAYER->getTexture(),
        CBox{m_data.box.x, m_data.box.y, CANVAS.x, CANVAS.y},
        sheet_canvas_quad(),
        texture_data);
    return {};
}

bool CPaperClosePassElement::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

bool CPaperClosePassElement::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

const char *CPaperClosePassElement::passName() {
    return "hyprimate-paper-close";
}

ePassElementType CPaperClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CPaperClosePassElement::boundingBox(
    [[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CPaperClosePassElement::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
