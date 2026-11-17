#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
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

#include "animations/shared/PaperRenderer.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {
    constexpr int RAMP_SIZE = 256;
    constexpr int RAMP_CENTRE = 128;
    constexpr double RAMP_SPAN = 127.0;  // texels from centre to either end, for k in [-1, 1]
    constexpr double SHADOW_STRENGTH = 0.6;  // peak black alpha for k = -1
    constexpr double HIGHLIGHT_STRENGTH = 0.45;  // peak white alpha for k = +1

    uint8_t rampByte(double unit) noexcept {
        return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
    }

    // Ramp U for a shade in [-1, 1]; the centre texel is neutral. Continuous, so a triangle's
    // corners interpolate.
    float shadeU(double shade) noexcept {
        const double texel = RAMP_CENTRE + (std::clamp(shade, -1.0, 1.0) * RAMP_SPAN);
        return static_cast<float>((texel + 0.5) / RAMP_SIZE);
    }
}  // namespace

SP<Render::ITexture> CPaperShade::texture() {
    if (m_texture || !g_pHyprRenderer) {
        return m_texture;
    }

    // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
    std::array<uint8_t, RAMP_SIZE * 4> pixels{};
    for (int i = 0; i < RAMP_SIZE; ++i) {
        const double k = std::clamp((i - RAMP_CENTRE) / RAMP_SPAN, -1.0, 1.0);
        uint8_t colour =
            0;  // black for shadow; white highlight is premultiplied, so colour equals alpha
        uint8_t alpha = 0;
        if (k < 0.0) {
            alpha = rampByte(-k * SHADOW_STRENGTH);
        } else if (k > 0.0) {
            colour = alpha = rampByte(k * HIGHLIGHT_STRENGTH);
        }

        pixels[(i * 4) + 0] = colour;
        pixels[(i * 4) + 1] = colour;
        pixels[(i * 4) + 2] = colour;
        pixels[(i * 4) + 3] = alpha;
    }

    m_texture = g_pHyprRenderer->createTexture(
        DRM_FORMAT_ARGB8888, pixels.data(), RAMP_SIZE * 4, Vector2D{RAMP_SIZE, 1}, false, false);
    return m_texture;
}

CBox sheetExtents(const hyprimate::SheetFrame &frame, const CBox &logicalBox) {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto &p : frame.position) {
        const double x = logicalBox.x + (logicalBox.w * (0.5 + p.x));
        const double y = logicalBox.y + (logicalBox.h * (0.5 + p.y));
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    return CBox{minX, minY, std::max(1.0, maxX - minX), std::max(1.0, maxY - minY)};
}

void fillSheetMeshes(const hyprimate::SheetFrame &frame,
    const CBox &sourceDevice,
    const CBox &outputDevice,
    hyprimate::Vec2 textureOrigin,
    hyprimate::Vec2 textureSize,
    std::vector<SMeshRenderVertex> &window,
    std::vector<SMeshRenderVertex> &shade) {
    if (window.size() != PAPER_VERTEX_COUNT) {
        window.resize(PAPER_VERTEX_COUNT);
    }
    if (shade.size() != PAPER_VERTEX_COUNT) {
        shade.resize(PAPER_VERTEX_COUNT);
    }

    const double centreX = sourceDevice.x + (0.5 * sourceDevice.w);
    const double centreY = sourceDevice.y + (0.5 * sourceDevice.h);
    std::size_t k = 0;
    for (std::size_t slot = 0; slot < hyprimate::PAPER_TRIANGLES; ++slot) {
        const std::size_t tri = frame.order[slot];
        const auto &corners = hyprimate::sheetTriangle(tri);
        for (std::size_t c = 0; c < 3; ++c) {
            const std::size_t vertex = corners[c];
            const auto &p = frame.position[vertex];
            const auto uv = hyprimate::sheetUv(vertex);
            const auto x = static_cast<float>(
                (centreX + (p.x * sourceDevice.w) - outputDevice.x) / outputDevice.w);
            const auto y = static_cast<float>(
                (centreY + (p.y * sourceDevice.h) - outputDevice.y) / outputDevice.h);

            window[k] = {
                .x = x,
                .y = y,
                .u = static_cast<float>(
                    (sourceDevice.x + (sourceDevice.w * uv.x) - textureOrigin.x) / textureSize.x),
                .v = static_cast<float>(
                    (sourceDevice.y + (sourceDevice.h * uv.y) - textureOrigin.y) / textureSize.y),
            };
            shade[k] = {.x = x, .y = y, .u = shadeU(frame.shade[tri][c]), .v = 0.5F};
            ++k;
        }
    }
}

const std::vector<SMeshRenderVertex> &paperCanvasQuad() {
    static const std::vector<SMeshRenderVertex> quad{
        {.x = 0.F, .y = 0.F, .u = 0.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 1.F, .u = 1.F, .v = 1.F},
    };
    return quad;
}

SP<Render::IFramebuffer> renderPaper(Render::CRenderContext &ctx, const SPaperDraw &d) {
    if (!d.window || !d.windowVertices || d.windowVertices->empty() ||
        !(d.canvas.x > 0.0 && d.canvas.y > 0.0)) {
        return nullptr;
    }

    // Entry blend state, restored on exit. The wrapper does not expose the blend function, so GL is
    // queried for it. Hyprland sets blend factors only through its wrapper, which never separates
    // RGB from alpha.
    const bool entryBlend = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entrySrc = 0;
    GLint entryDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entrySrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &entryDst);

    const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    const auto state = ctx.saveDrawState();
    auto &data = ctx.m_data;

    auto paperLayer = g_pHyprRenderer->getWorkBuffer(ctx, d.canvas);
    if (!paperLayer) {
        return nullptr;
    }

    // Requested while paperLayer is held, so the pool cannot return the same buffer twice.
    const bool lit = d.shade && d.shadeVertices && !d.shadeVertices->empty();
    const auto shadeLayer =
        lit ? g_pHyprRenderer->getWorkBuffer(ctx, d.canvas) : SP<Render::IFramebuffer>{};

    // Binds fb as the target and clears it to transparent under the canvas damage.
    const auto beginTarget = [&](const SP<Render::IFramebuffer> &fb) {
        g_pHyprRenderer->bindFB(ctx, fb);
        data.damage = CRegion{0.0, 0.0, d.canvas.x, d.canvas.y};
        data.fbSize = d.canvas;
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    };

    Render::GL::CHyprOpenGLImpl::STextureRenderData td{};
    td.damage = &data.damage;
    td.allowCustomUV = true;

    // Shade layer: no blending, so the frontmost triangle's shade wins each pixel and hidden
    // triangles never tint visible ones.
    if (shadeLayer) {
        beginTarget(shadeLayer);
        const bool wasBlend = Render::GL::g_pHyprOpenGL->blendEnabled();
        Render::GL::g_pHyprOpenGL->blend(false);
        td.a = 1.F;
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, d.shade, d.meshBox, *d.shadeVertices, td);
        Render::GL::g_pHyprOpenGL->blend(wasBlend);
    }

    // Window layer: triangles are sorted back to front and drawn without blending, so each pixel
    // takes the frontmost triangle only. Paper is opaque: with blending, a translucent window let
    // the layers folded behind show through, and the crumple read as tissue. The window's own
    // translucency against what is behind it is kept.
    beginTarget(paperLayer);
    Render::GL::g_pHyprOpenGL->blend(false);
    td.a = d.alpha;
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, d.window, d.meshBox, *d.windowVertices, td);
    Render::GL::g_pHyprOpenGL->blend(true);

    // Tint: DST_ALPHA weights the shade by window coverage, so transparent pixels stay transparent
    // and alpha is preserved.
    if (shadeLayer) {
        Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        td.a = 1.F;
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            shadeLayer->getTexture(),
            CBox{0.0, 0.0, d.canvas.x, d.canvas.y},
            paperCanvasQuad(),
            td);
    }

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    Render::GL::g_pHyprOpenGL->blend(entryBlend);
    Render::GL::g_pHyprOpenGL->blendFunc(
        static_cast<GLenum>(entrySrc), static_cast<GLenum>(entryDst));

    return paperLayer;
}
