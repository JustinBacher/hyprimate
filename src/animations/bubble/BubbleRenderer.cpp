#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Region.hpp>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/shared/SheetRenderer.hpp"

namespace {
    // Runs work with the compositor's EGL context current, then restores whatever was current
    // before.
    template <class F>
    void with_compositor_context(F &&work) {
        const EGLDisplay DISPLAY = eglGetCurrentDisplay();
        const EGLContext CONTEXT = eglGetCurrentContext();
        const EGLSurface DRAW = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface READ = eglGetCurrentSurface(EGL_READ);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        std::forward<F>(work)();
        if (DISPLAY != EGL_NO_DISPLAY && CONTEXT != EGL_NO_CONTEXT) {
            eglMakeCurrent(DISPLAY, DRAW, READ, CONTEXT);
        } else if (CONTEXT == EGL_NO_CONTEXT) {
            eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
        }
    }
}  // namespace

CBubbleRenderer::~CBubbleRenderer() {
    if (Render::GL::g_pHyprOpenGL) {
        with_compositor_context([&] { m_material.release(); });
    }
}

void CBubbleRenderer::warm_up() {
    if (m_warmed || !Render::GL::g_pHyprOpenGL) {
        return;
    }
    m_warmed = true;
    with_compositor_context([&] {
        // initialize() binds its lookup texture on unit 2, a vertex array and the instance buffer:
        // leave every one of them, and the active unit Hyprland caches, exactly as found.
        GLint active = 0;
        GLint texture = 0;
        GLint vao = 0;
        GLint array_buffer = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
        glActiveTexture(GL_TEXTURE2);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        m_material.initialize();
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glActiveTexture(static_cast<GLenum>(active));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
    });
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): GL capture, drawing and restoration must retain their order.
SP<Render::IFramebuffer> CBubbleRenderer::render(
    Render::CRenderContext &ctx, const SBubbleDraw &draw) {
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
        (!draw.on_glass && (!draw.window || (draw.window->m_texID == 0U) ||
                               (draw.window->m_type != Render::TEXTURE_RGBA &&
                                   draw.window->m_type != Render::TEXTURE_RGBX) ||
                               (!(draw.texture_size.x >= 1) || !(draw.texture_size.y >= 1)))) ||
        (!(draw.canvas.x > 0) || !(draw.canvas.y > 0) || !(draw.source.w > 0) ||
            !(draw.source.h > 0) || !(draw.frame.size.x > 0) || !(draw.frame.size.y > 0))) {
        return nullptr;
    }
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    const auto WALLPAPER = draw.wallpaper && draw.wallpaper->framebuffer &&
                                   !ctx.readOnlyEffects() && !ctx.m_renderingCapture &&
                                   !ctx.m_renderingSnapshot
                               ? draw.wallpaper->framebuffer->getTexture()
                               : SP<Render::ITexture>{};
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    const auto LAYER = g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas);
    const GLuint IMAGE = draw.window ? draw.window->m_texID : 0;
    if (!LAYER || !LAYER->getTexture() || LAYER->getTexture()->m_texID == IMAGE ||
        (WALLPAPER && LAYER->getTexture()->m_texID == WALLPAPER->m_texID)) {
        return nullptr;
    }
    GLint program = 0;
    GLint vao = 0;
    GLint array_buffer = 0;
    GLint active = 0;
    GLint src = 0;
    GLint dst = 0;
    GLint src_alpha = 0;
    GLint dst_alpha = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): glGetIntegerv writes each texture binding before restoration.
    std::array<std::array<GLint, 2>, 4> textures;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): glGetIntegerv writes the viewport before restoration.
    std::array<GLint, 4> viewport;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): glGetBooleanv writes the color mask before restoration.
    std::array<GLboolean, 4> mask;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != 0U;
    const bool STENCIL = glIsEnabled(GL_STENCIL_TEST) != 0U;
    const bool DEPTH_TEST = glIsEnabled(GL_DEPTH_TEST) != 0U;
    const bool CULL = glIsEnabled(GL_CULL_FACE) != 0U;
    const bool BLEND = renderer.blendEnabled();
    int unit = 0;
    for (auto &texture : textures) {
        renderer.setActiveTexture(GL_TEXTURE0 + unit++);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &std::get<0>(texture));
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &std::get<1>(texture));
    }
    const bool READY = m_material.initialize();
    if (READY) {
        g_pHyprRenderer->bindFB(ctx, LAYER);
        renderer.setViewport(
            0, 0, static_cast<GLsizei>(draw.canvas.x), static_cast<GLsizei>(draw.canvas.y));
        renderer.setCapStatus(GL_SCISSOR_TEST, false);
        renderer.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        renderer.blend(false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const SBubbleTarget TARGET{
            .canvas = {.x = draw.canvas.x, .y = draw.canvas.y},
            .origin = draw.output_origin,
            .source_pos = {.x = draw.source.x, .y = draw.source.y},
            .source_size = {.x = draw.source.w, .y = draw.source.h},
        };
        SBubbleImages images;
        images.window = IMAGE;
        images.texture_origin = draw.texture_origin;
        images.texture_size = draw.on_glass ? hyprimate::Vec2{.x = 1, .y = 1} : draw.texture_size;
        if (WALLPAPER) {
            images.wallpaper = WALLPAPER->m_texID;
            images.wallpaper_origin = {
                .x = draw.wallpaper->origin.x,
                .y = draw.wallpaper->origin.y,
            };
            images.wallpaper_x = {.x = draw.wallpaper->axis_x.x, .y = draw.wallpaper->axis_x.y};
            images.wallpaper_y = {.x = draw.wallpaper->axis_y.x, .y = draw.wallpaper->axis_y.y};
            images.wallpaper_pixel = {
                .x = 1.0 / WALLPAPER->m_size.x,
                .y = 1.0 / WALLPAPER->m_size.y,
            };
        }
        m_material.draw_membranes(draw.frame, TARGET, images);
        renderer.blend(true);
        renderer.blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        // The wrapper skips the call when its cached RGB pair already matches; separate alpha
        // factors may differ.
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (draw.on_glass) {
            m_material.draw_particles(std::span{draw.glass_particles}.first(
                                          std::min(draw.glass_count, draw.glass_particles.size())),
                draw.frame.size,
                TARGET,
                true);
            if (draw.wake) {
                m_material.draw_wake(*draw.wake, TARGET);
            }
        } else {
            const auto SHELL_COUNT = hyprimate::bubble_particles(
                draw.frame.shell, std::span{m_particles}.first(hyprimate::K_BUBBLE_MAX_PARTICLES));
            const auto WINDOW_COUNT = hyprimate::bubble_particles(draw.frame.window,
                // NOLINTNEXTLINE(readability-suspicious-call-argument): Shell count is the offset; the particle limit is the count.
                std::span{m_particles}.subspan(SHELL_COUNT, hyprimate::K_BUBBLE_MAX_PARTICLES));
            m_material.draw_particles(std::span{m_particles}.first(SHELL_COUNT + WINDOW_COUNT),
                draw.frame.size,
                TARGET,
                false);
            m_material.draw_microbubbles(std::span{draw.frame.micro}.first(std::min(
                                             draw.frame.micro_count, draw.frame.micro.size())),
                draw.frame.size,
                TARGET);
        }
    }
    unit = 0;
    for (const auto &texture : textures) {
        renderer.setActiveTexture(GL_TEXTURE0 + unit++);
        glBindTexture(GL_TEXTURE_2D, std::get<0>(texture));
        glBindTexture(GL_TEXTURE_3D, std::get<1>(texture));
    }
    renderer.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
    glUseProgram(static_cast<GLuint>(program));
    glColorMask(std::get<0>(mask), std::get<1>(mask), std::get<2>(mask), std::get<3>(mask));
    if (DEPTH_TEST) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (CULL) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    renderer.setCapStatus(GL_SCISSOR_TEST, SCISSOR);
    renderer.setCapStatus(GL_STENCIL_TEST, STENCIL);
    renderer.blend(BLEND);
    renderer.blendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
    // Hyprland's cache tracks only the RGB pair; restore separate alpha factors underneath it.
    if (src_alpha != src || dst_alpha != dst) {
        glBlendFuncSeparate(static_cast<GLenum>(src),
            static_cast<GLenum>(dst),
            static_cast<GLenum>(src_alpha),
            static_cast<GLenum>(dst_alpha));
    }
    renderer.setViewport(
        std::get<0>(viewport), std::get<1>(viewport), std::get<2>(viewport), std::get<3>(viewport));
    return READY ? LAYER : nullptr;
}

CBubblePassElement::CBubblePassElement(SData data) : m_data(std::move(data)) {
}
std::vector<UP<IPassElement>> CBubblePassElement::draw(Render::CRenderContext &ctx) {
    const auto LAYER =
        m_data.renderer ? m_data.renderer->render(ctx, m_data.draw) : SP<Render::IFramebuffer>{};
    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &m_data.damage;
    texture_data.a = m_data.alpha;
    texture_data.allowCustomUV = true;
    texture_data.allowDim = false;
    if (LAYER) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, LAYER->getTexture(), m_data.box, sheet_canvas_quad(), texture_data);
    } else {
        // Preserve the native snapshot when the material cannot run. The
        // capture may occupy only a sub-rectangle of a larger texture.
        const auto &draw_data = m_data.draw;
        if (!draw_data.window || !(draw_data.canvas.x > 0) || !(draw_data.canvas.y > 0) ||
            !(draw_data.source.w > 0) || !(draw_data.source.h > 0) ||
            !(draw_data.texture_size.x > 0) || !(draw_data.texture_size.y > 0)) {
            return {};
        }
        const auto U_MIN = static_cast<float>(
            (draw_data.source.x - draw_data.texture_origin.x) / draw_data.texture_size.x);
        const auto V_MIN = static_cast<float>(
            (draw_data.source.y - draw_data.texture_origin.y) / draw_data.texture_size.y);
        const auto U_MAX = static_cast<float>(
            (draw_data.source.x + draw_data.source.w - draw_data.texture_origin.x) /
            draw_data.texture_size.x);
        const auto V_MAX = static_cast<float>(
            (draw_data.source.y + draw_data.source.h - draw_data.texture_origin.y) /
            draw_data.texture_size.y);
        const CBox BOX{m_data.box.x + ((draw_data.source.x - draw_data.output_origin.x) *
                                          m_data.box.w / draw_data.canvas.x),
            m_data.box.y + ((draw_data.source.y - draw_data.output_origin.y) * m_data.box.h /
                               draw_data.canvas.y),
            draw_data.source.w * m_data.box.w / draw_data.canvas.x,
            draw_data.source.h * m_data.box.h / draw_data.canvas.y};
        const std::vector<SMeshRenderVertex> QUAD{
            {.x = 0.F, .y = 0.F, .u = U_MIN, .v = V_MIN},
            {.x = 0.F, .y = 1.F, .u = U_MIN, .v = V_MAX},
            {.x = 1.F, .y = 0.F, .u = U_MAX, .v = V_MIN},
            {.x = 1.F, .y = 0.F, .u = U_MAX, .v = V_MIN},
            {.x = 0.F, .y = 1.F, .u = U_MIN, .v = V_MAX},
            {.x = 1.F, .y = 1.F, .u = U_MAX, .v = V_MAX},
        };
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, draw_data.window, BOX, QUAD, texture_data);
    }
    return {};
}
bool CBubblePassElement::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}
bool CBubblePassElement::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}
const char *CBubblePassElement::passName() {
    return "hyprimate-bubble";
}
ePassElementType CBubblePassElement::type() {
    return EK_CUSTOM;
}
std::optional<CBox> CBubblePassElement::boundingBox([[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}
CRegion CBubblePassElement::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
