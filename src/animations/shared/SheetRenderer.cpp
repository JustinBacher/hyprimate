#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <vector>

#include <GLES3/gl32.h>
#include <drm_fourcc.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>

#include "animations/shared/SheetRenderer.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {
    constexpr int RAMP_SIZE = 256;
    constexpr int RAMP_CENTRE = 128;
    constexpr double RAMP_SPAN = 127.0;  // texels from centre to either end, for k in [-1, 1]
    constexpr double SHADOW_STRENGTH = 0.6;  // peak black alpha for k = -1
    constexpr double HIGHLIGHT_STRENGTH = 0.45;  // peak white alpha for k = +1

    uint8_t ramp_byte(double unit) noexcept {
        return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
    }

    // Ramp U for a shade in [-1, 1]; the centre texel is neutral. Continuous, so a triangle's
    // corners interpolate.
    float shade_u(double shade) noexcept {
        const double TEXEL = RAMP_CENTRE + (std::clamp(shade, -1.0, 1.0) * RAMP_SPAN);
        return static_cast<float>((TEXEL + 0.5) / RAMP_SIZE);
    }
}  // namespace

SP<Render::ITexture> CSheetShade::texture() {
    if (m_texture || !g_pHyprRenderer) {
        return m_texture;
    }

    // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
    static_assert(sizeof(std::array<uint8_t, 4>) == 4);
    std::array<std::array<uint8_t, 4>, RAMP_SIZE> pixels{};
    int pixel_index = 0;
    for (auto &pixel : pixels) {
        const double RAMP_COORD = std::max((pixel_index - RAMP_CENTRE) / RAMP_SPAN, -1.0);
        uint8_t colour =
            0;  // black for shadow; white highlight is premultiplied, so colour equals alpha
        uint8_t alpha = 0;
        if (RAMP_COORD < 0.0) {
            alpha = ramp_byte(-RAMP_COORD * SHADOW_STRENGTH);
        } else if (RAMP_COORD > 0.0) {
            colour = alpha = ramp_byte(RAMP_COORD * HIGHLIGHT_STRENGTH);
        }

        std::get<0>(pixel) = colour;
        std::get<1>(pixel) = colour;
        std::get<2>(pixel) = colour;
        std::get<3>(pixel) = alpha;
        ++pixel_index;
    }

    m_texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        pixels.front().data(),
        RAMP_SIZE * 4,
        Vector2D{RAMP_SIZE, 1},
        false,
        false);
    return m_texture;
}

CBox sheet_extents(const hyprimate::SheetFrame &frame, const CBox &logical_box) {
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (const auto &sheet_point : frame.position) {
        const double POS_X = logical_box.x + (logical_box.w * (0.5 + sheet_point.x));
        const double POS_Y = logical_box.y + (logical_box.h * (0.5 + sheet_point.y));
        min_x = std::min(min_x, POS_X);
        min_y = std::min(min_y, POS_Y);
        max_x = std::max(max_x, POS_X);
        max_y = std::max(max_y, POS_Y);
    }
    return CBox{min_x, min_y, std::max(1.0, max_x - min_x), std::max(1.0, max_y - min_y)};
}

void fill_sheet_meshes(const hyprimate::SheetFrame &frame,
    const CBox &source_device,
    const CBox &output_device,
    hyprimate::Vec2 texture_origin,
    hyprimate::Vec2 texture_size,
    std::vector<SMeshRenderVertex> &window,
    std::vector<SMeshRenderVertex> &shade) {
    if (window.size() != SHEET_VERTEX_COUNT) {
        window.resize(SHEET_VERTEX_COUNT);
    }
    if (shade.size() != SHEET_VERTEX_COUNT) {
        shade.resize(SHEET_VERTEX_COUNT);
    }

    const double CENTRE_X = source_device.x + (0.5 * source_device.w);
    const double CENTRE_Y = source_device.y + (0.5 * source_device.h);
    auto window_vertex = window.begin();
    auto shade_vertex = shade.begin();
    for (const std::size_t TRI : frame.order) {
        const auto &corners = hyprimate::sheet_triangle(TRI);
        for (auto &&[vertex_index, corner_shade] : std::views::zip(corners, frame.shade[TRI])) {
            const auto &sheet_point = frame.position[vertex_index];
            const auto TEX_COORD = hyprimate::sheet_uv(vertex_index);
            const auto POS_X = static_cast<float>(
                (CENTRE_X + (sheet_point.x * source_device.w) - output_device.x) / output_device.w);
            const auto POS_Y = static_cast<float>(
                (CENTRE_Y + (sheet_point.y * source_device.h) - output_device.y) / output_device.h);

            *window_vertex = {
                .x = POS_X,
                .y = POS_Y,
                .u = static_cast<float>(
                    (source_device.x + (source_device.w * TEX_COORD.x) - texture_origin.x) /
                    texture_size.x),
                .v = static_cast<float>(
                    (source_device.y + (source_device.h * TEX_COORD.y) - texture_origin.y) /
                    texture_size.y),
            };
            *shade_vertex = {
                .x = POS_X,
                .y = POS_Y,
                .u = shade_u(corner_shade),
                .v = 0.5F,
            };
            ++window_vertex;
            ++shade_vertex;
        }
    }
}

const std::vector<SMeshRenderVertex> &sheet_canvas_quad() {
    static const std::vector<SMeshRenderVertex> QUAD{
        {.x = 0.F, .y = 0.F, .u = 0.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 1.F, .u = 1.F, .v = 1.F},
    };
    return QUAD;
}

SP<Render::IFramebuffer> render_sheet(Render::CRenderContext &ctx, const SSheetDraw &draw) {
    if (!draw.window || (draw.window_vertices == nullptr) || draw.window_vertices->empty() ||
        (!(draw.canvas.x > 0.0) || !(draw.canvas.y > 0.0))) {
        return nullptr;
    }

    // Entry blend state, restored on exit. The wrapper does not expose the blend function, so GL is
    // queried for it. Hyprland sets blend factors only through its wrapper, which never separates
    // RGB from alpha.
    const bool ENTRY_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entry_src = 0;
    GLint entry_dst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entry_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &entry_dst);

    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    auto &data = ctx.m_data;

    auto paper_layer = g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas);
    if (!paper_layer) {
        return nullptr;
    }

    // Requested while paper_layer is held, so the pool cannot return the same buffer twice.
    const bool LIT =
        draw.shade && (draw.shade_vertices != nullptr) && !draw.shade_vertices->empty();
    const auto SHADE_LAYER =
        LIT ? g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas) : SP<Render::IFramebuffer>{};

    // Binds the framebuffer as the target and clears it under the canvas damage.
    const auto BEGIN_TARGET = [&](const SP<Render::IFramebuffer> &framebuffer) {
        g_pHyprRenderer->bindFB(ctx, framebuffer);
        data.damage = CRegion{0.0, 0.0, draw.canvas.x, draw.canvas.y};
        data.fbSize = draw.canvas;
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    };

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &data.damage;
    texture_data.allowCustomUV = true;

    // Shade layer: no blending, so the frontmost triangle's shade wins each pixel and hidden
    // triangles never tint visible ones.
    if (SHADE_LAYER) {
        BEGIN_TARGET(SHADE_LAYER);
        const bool WAS_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
        Render::GL::g_pHyprOpenGL->blend(false);
        texture_data.a = 1.F;
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, draw.shade, draw.mesh_box, *draw.shade_vertices, texture_data);
        Render::GL::g_pHyprOpenGL->blend(WAS_BLEND);
    }

    // Window layer: triangles are sorted back to front and drawn without blending, so each pixel
    // takes the frontmost triangle only. Paper is opaque: with blending, a translucent window let
    // the layers folded behind show through, and the crumple read as tissue. The window's own
    // translucency against what is behind it is kept.
    BEGIN_TARGET(paper_layer);
    Render::GL::g_pHyprOpenGL->blend(false);
    texture_data.a = draw.alpha;
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, draw.window, draw.mesh_box, *draw.window_vertices, texture_data);
    Render::GL::g_pHyprOpenGL->blend(true);

    // Tint: DST_ALPHA weights the shade by window coverage, so transparent pixels stay transparent
    // and alpha is preserved.
    if (SHADE_LAYER) {
        Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        texture_data.a = 1.F;
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            SHADE_LAYER->getTexture(),
            CBox{0.0, 0.0, draw.canvas.x, draw.canvas.y},
            sheet_canvas_quad(),
            texture_data);
    }

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    Render::GL::g_pHyprOpenGL->blend(ENTRY_BLEND);
    Render::GL::g_pHyprOpenGL->blendFunc(
        static_cast<GLenum>(entry_src), static_cast<GLenum>(entry_dst));

    return paper_layer;
}
