#include <algorithm>
#include <utility>

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/shared/PaperRenderer.hpp"

namespace {
    // Runs work with the compositor's EGL context current, then restores whatever was current
    // before.
    template <class F>
    void withCompositorContext(F &&work) {
        const EGLDisplay display = eglGetCurrentDisplay();
        const EGLContext context = eglGetCurrentContext();
        const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW),
                         read = eglGetCurrentSurface(EGL_READ);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        work();
        if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
            eglMakeCurrent(display, draw, read, context);
        else if (context == EGL_NO_CONTEXT)
            eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
    }
}  // namespace

CBubbleRenderer::~CBubbleRenderer() {
    if (Render::GL::g_pHyprOpenGL) withCompositorContext([&] { m_material.release(); });
}

void CBubbleRenderer::warmUp() {
    if (m_warmed || !Render::GL::g_pHyprOpenGL) return;
    m_warmed = true;
    withCompositorContext([&] {
        // initialize() binds its lookup texture on unit 2, a vertex array and the instance buffer:
        // leave every one of them, and the active unit Hyprland caches, exactly as found.
        GLint active, texture, vao, arrayBuffer;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glActiveTexture(GL_TEXTURE2);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        m_material.initialize();
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glActiveTexture(static_cast<GLenum>(active));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
    });
}

SP<Render::IFramebuffer> CBubbleRenderer::render(
    Render::CRenderContext &ctx, const SBubbleDraw &d) {
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
        (!d.onGlass && (!d.window || !d.window->m_texID ||
                           (d.window->m_type != Render::TEXTURE_RGBA &&
                               d.window->m_type != Render::TEXTURE_RGBX) ||
                           !(d.textureSize.x >= 1 && d.textureSize.y >= 1))) ||
        !(d.canvas.x > 0 && d.canvas.y > 0 && d.source.w > 0 && d.source.h > 0 &&
            d.frame.size.x > 0 && d.frame.size.y > 0))
        return nullptr;
    auto &gl = *Render::GL::g_pHyprOpenGL;
    const auto wallpaper = d.wallpaper && d.wallpaper->framebuffer && !ctx.readOnlyEffects() &&
                                   !ctx.m_renderingCapture && !ctx.m_renderingSnapshot
                               ? d.wallpaper->framebuffer->getTexture()
                               : SP<Render::ITexture>{};
    Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto state = ctx.saveDrawState();
    const auto layer = g_pHyprRenderer->getWorkBuffer(ctx, d.canvas);
    const GLuint image = d.window ? d.window->m_texID : 0;
    if (!layer || !layer->getTexture() || layer->getTexture()->m_texID == image ||
        (wallpaper && layer->getTexture()->m_texID == wallpaper->m_texID))
        return nullptr;
    GLint program, vao, arrayBuffer, active, textures[4][2], viewport[4], src, dst, srcAlpha,
        dstAlpha;
    GLboolean mask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
    const bool blend = gl.blendEnabled();
    for (int i = 0; i < 4; ++i) {
        gl.setActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i][0]);
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &textures[i][1]);
    }
    const bool ready = m_material.initialize();
    if (ready) {
        g_pHyprRenderer->bindFB(ctx, layer);
        gl.setViewport(0, 0, static_cast<GLsizei>(d.canvas.x), static_cast<GLsizei>(d.canvas.y));
        gl.setCapStatus(GL_SCISSOR_TEST, false);
        gl.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        gl.blend(false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        SBubbleTarget target{{d.canvas.x, d.canvas.y},
            d.outputOrigin,
            {d.source.x, d.source.y},
            {d.source.w, d.source.h}};
        SBubbleImages images;
        images.window = image;
        images.textureOrigin = d.textureOrigin;
        images.textureSize = d.onGlass ? hyprimate::Vec2{1, 1} : d.textureSize;
        if (wallpaper) {
            images.wallpaper = wallpaper->m_texID;
            images.wallpaperOrigin = {d.wallpaper->origin.x, d.wallpaper->origin.y};
            images.wallpaperX = {d.wallpaper->axisX.x, d.wallpaper->axisX.y};
            images.wallpaperY = {d.wallpaper->axisY.x, d.wallpaper->axisY.y};
            images.wallpaperPixel = {1.0 / wallpaper->m_size.x, 1.0 / wallpaper->m_size.y};
        }
        m_material.drawMembranes(d.frame, target, images);
        gl.blend(true);
        gl.blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        // The wrapper skips the call when its cached RGB pair already matches; separate alpha
        // factors may differ.
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (d.onGlass)
            m_material.drawParticles(
                std::span{d.glassParticles}.first(std::min(d.glassCount, d.glassParticles.size())),
                d.frame.size,
                target,
                true);
        else {
            const auto shellCount = hyprimate::bubbleParticles(
                d.frame.shell, std::span{m_particles}.first(hyprimate::kBubbleMaxParticles));
            const auto windowCount = hyprimate::bubbleParticles(d.frame.window,
                std::span{m_particles}.subspan(shellCount, hyprimate::kBubbleMaxParticles));
            m_material.drawParticles(std::span{m_particles}.first(shellCount + windowCount),
                d.frame.size,
                target,
                false);
        }
    }
    for (int i = 0; i < 4; ++i) {
        gl.setActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i][0]);
        glBindTexture(GL_TEXTURE_3D, textures[i][1]);
    }
    gl.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
    glUseProgram(static_cast<GLuint>(program));
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (depthTest)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    gl.setCapStatus(GL_SCISSOR_TEST, scissor);
    gl.setCapStatus(GL_STENCIL_TEST, stencil);
    gl.blend(blend);
    gl.blendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
    // Hyprland's cache tracks only the RGB pair; restore separate alpha factors underneath it.
    if (srcAlpha != src || dstAlpha != dst)
        glBlendFuncSeparate(static_cast<GLenum>(src),
            static_cast<GLenum>(dst),
            static_cast<GLenum>(srcAlpha),
            static_cast<GLenum>(dstAlpha));
    gl.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ready ? layer : nullptr;
}

CBubblePassElement::CBubblePassElement(SData data) : m_data(std::move(data)) {
}
std::vector<UP<IPassElement>> CBubblePassElement::draw(Render::CRenderContext &ctx) {
    const auto layer =
        m_data.renderer ? m_data.renderer->render(ctx, m_data.draw) : SP<Render::IFramebuffer>{};
    Render::GL::CHyprOpenGLImpl::STextureRenderData td{};
    td.damage = &m_data.damage;
    td.a = m_data.alpha;
    td.allowCustomUV = true;
    td.allowDim = false;
    if (layer) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, layer->getTexture(), m_data.box, paperCanvasQuad(), td);
    } else {
        // Preserve the native snapshot when the material cannot run. The
        // capture may occupy only a sub-rectangle of a larger texture.
        const auto &d = m_data.draw;
        if (!d.window || !(d.canvas.x > 0 && d.canvas.y > 0 && d.source.w > 0 && d.source.h > 0 &&
                             d.textureSize.x > 0 && d.textureSize.y > 0))
            return {};
        const float u0 = float((d.source.x - d.textureOrigin.x) / d.textureSize.x);
        const float v0 = float((d.source.y - d.textureOrigin.y) / d.textureSize.y);
        const float u1 = float((d.source.x + d.source.w - d.textureOrigin.x) / d.textureSize.x);
        const float v1 = float((d.source.y + d.source.h - d.textureOrigin.y) / d.textureSize.y);
        const CBox box{m_data.box.x + (d.source.x - d.outputOrigin.x) * m_data.box.w / d.canvas.x,
            m_data.box.y + (d.source.y - d.outputOrigin.y) * m_data.box.h / d.canvas.y,
            d.source.w * m_data.box.w / d.canvas.x,
            d.source.h * m_data.box.h / d.canvas.y};
        const std::vector<SMeshRenderVertex> quad{
            {0.F, 0.F, u0, v0},
            {0.F, 1.F, u0, v1},
            {1.F, 0.F, u1, v0},
            {1.F, 0.F, u1, v0},
            {0.F, 1.F, u0, v1},
            {1.F, 1.F, u1, v1},
        };
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, d.window, box, quad, td);
    }
    return {};
}
bool CBubblePassElement::needsLiveBlur(Render::CRenderContext &) {
    return false;
}
bool CBubblePassElement::needsPrecomputeBlur(Render::CRenderContext &) {
    return false;
}
const char *CBubblePassElement::passName() {
    return "hyprimate-bubble";
}
ePassElementType CBubblePassElement::type() {
    return EK_CUSTOM;
}
std::optional<CBox> CBubblePassElement::boundingBox(Render::CRenderContext &) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}
CRegion CBubblePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
