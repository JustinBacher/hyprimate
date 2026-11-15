#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/shared/PaperRenderer.hpp"

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
#include "animations/bubble/BubbleShaders.hpp"

GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "hypr-bubble: shader compilation failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

bool CBubbleRenderer::initialize() {
    if (m_tried)
        return m_program && m_vao;
    m_tried = true;
    const GLuint vert = compile(GL_VERTEX_SHADER, VERTEX);
    const GLuint frag = compile(GL_FRAGMENT_SHADER, FRAGMENT);
    if (!vert || !frag) {
        if (vert)
            glDeleteShader(vert);
        if (frag)
            glDeleteShader(frag);
        return false;
    }
    m_program = glCreateProgram();
    if (m_program) {
        glAttachShader(m_program, vert);
        glAttachShader(m_program, frag);
        glLinkProgram(m_program);
        GLint ok = GL_FALSE;
        glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "hypr-bubble: shader link failed: %s\n", log);
            glDeleteProgram(m_program);
            m_program = 0;
        }
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (m_program)
        glGenVertexArrays(1, &m_vao);
    if (!m_vao && m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_program) {
        constexpr const char *names[] = {"image",          "wallpaperImage",  "sourcePos",    "sourceSize",
                                         "textureOrigin",  "textureSize",     "outputOrigin", "size",
                                         "seed",           "wallpaperOrigin", "wallpaperX",   "wallpaperY",
                                         "wallpaperReady", "wallpaperPixel",  "residueTime"};
        for (std::size_t i = 0; i < m_uniforms.size(); ++i)
            m_uniforms[i] = glGetUniformLocation(m_program, names[i]);
        constexpr const char *fields[] = {"halfSize",   "rupture", "core", "corner", "depth",   "content", "film",
                                          "visibility", "ripple",  "time", "tear",   "retract", "remnant", "closing"};
        constexpr const char *surfaces[] = {"windowSurface", "shellSurface"};
        for (std::size_t surface = 0; surface < m_surfaceUniforms.size(); ++surface) {
            for (std::size_t field = 0; field < m_surfaceUniforms[surface].size(); ++field) {
                char name[64];
                std::snprintf(name, sizeof(name), "%s.%s", surfaces[surface], fields[field]);
                m_surfaceUniforms[surface][field] = glGetUniformLocation(m_program, name);
            }
        }
    }
    return m_program && m_vao;
}

CBubbleRenderer::~CBubbleRenderer() {
    if (!m_program && !m_vao)
        return;
    if (!Render::GL::g_pHyprOpenGL)
        return;
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_program)
        glDeleteProgram(m_program);
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
        eglMakeCurrent(display, draw, read, context);
    else if (context == EGL_NO_CONTEXT)
        eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

SP<Render::IFramebuffer> CBubbleRenderer::render(Render::CRenderContext &ctx, const SBubbleDraw &d) {
    const bool residue = d.residueTime >= 0;
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
        (!residue && (!d.window || !d.window->m_texID ||
                      (d.window->m_type != Render::TEXTURE_RGBA && d.window->m_type != Render::TEXTURE_RGBX) ||
                      !(d.textureSize.x >= 1 && d.textureSize.y >= 1))) ||
        !(d.canvas.x > 0 && d.canvas.y > 0 && d.source.w > 0 && d.source.h > 0 && d.frame.size.x > 0 &&
          d.frame.size.y > 0))
        return nullptr;
    auto &gl = *Render::GL::g_pHyprOpenGL;
    if (!initialize())
        return nullptr;
    // A monitor's private background snapshot must never be reused in a separate capture context.
    const auto wallpaper = d.wallpaper && d.wallpaper->framebuffer && !ctx.readOnlyEffects() &&
                                   !ctx.m_renderingCapture && !ctx.m_renderingSnapshot
                               ? d.wallpaper->framebuffer->getTexture()
                               : SP<Render::ITexture>{};
    Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto                                 state = ctx.saveDrawState();
    const auto                           layer = g_pHyprRenderer->getWorkBuffer(ctx, d.canvas);
    const GLuint                         image = d.window ? d.window->m_texID : 0;
    if (!layer || !layer->getTexture() || layer->getTexture()->m_texID == image ||
        (wallpaper && layer->getTexture()->m_texID == wallpaper->m_texID))
        return nullptr;

    GLint     program, vao, active, tex, wallpaperTex, viewport[4];
    GLboolean mask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
    const bool blend = gl.blendEnabled();
    gl.setActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    gl.setActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &wallpaperTex);
    glBindTexture(GL_TEXTURE_2D, wallpaper ? wallpaper->m_texID : image);
    gl.setActiveTexture(GL_TEXTURE0);
    g_pHyprRenderer->bindFB(ctx, layer);
    gl.setViewport(0, 0, static_cast<GLsizei>(d.canvas.x), static_cast<GLsizei>(d.canvas.y));
    gl.setCapStatus(GL_SCISSOR_TEST, false);
    gl.setCapStatus(GL_STENCIL_TEST, false);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    gl.blend(false);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindTexture(GL_TEXTURE_2D, image);
    const auto u1 = [&](std::size_t index, double value) { glUniform1f(m_uniforms[index], static_cast<float>(value)); };
    const auto u2 = [&](std::size_t index, double x, double y) {
        glUniform2f(m_uniforms[index], static_cast<float>(x), static_cast<float>(y));
    };
    glUniform1i(m_uniforms[0], 0);
    glUniform1i(m_uniforms[1], 1);
    u2(2, d.source.x, d.source.y);
    u2(3, d.source.w, d.source.h);
    u2(4, d.textureOrigin.x, d.textureOrigin.y);
    u2(5, residue ? 1.0 : d.textureSize.x, residue ? 1.0 : d.textureSize.y);
    u2(6, d.outputOrigin.x, d.outputOrigin.y);
    u2(7, d.frame.size.x, d.frame.size.y);
    u1(8, d.frame.seed);
    const auto origin = wallpaper ? d.wallpaper->origin : Vector2D{};
    const auto axisX = wallpaper ? d.wallpaper->axisX : Vector2D{};
    const auto axisY = wallpaper ? d.wallpaper->axisY : Vector2D{};
    u2(9, origin.x, origin.y);
    u2(10, axisX.x, axisX.y);
    u2(11, axisY.x, axisY.y);
    glUniform1i(m_uniforms[12], wallpaper && wallpaper->m_texID ? 1 : 0);
    u2(13, wallpaper ? 1.0 / wallpaper->m_size.x : 1.0, wallpaper ? 1.0 / wallpaper->m_size.y : 1.0);
    u1(14, d.residueTime);
    if (!residue) {
        const auto upload = [](const std::array<int, 14> &uniforms, const liquid_glass::BubbleSurface &surface) {
            glUniform1f(uniforms[7], static_cast<float>(surface.visibility));
            if (surface.visibility <= 0)
                return;
            const auto vector = [&](std::size_t index, liquid_glass::Vec2 value) {
                glUniform2f(uniforms[index], static_cast<float>(value.x), static_cast<float>(value.y));
            };
            const auto scalar = [&](std::size_t index, double value) {
                glUniform1f(uniforms[index], static_cast<float>(value));
            };
            vector(0, surface.halfSize);
            vector(1, surface.rupture);
            vector(2, surface.core);
            scalar(3, surface.corner);
            scalar(4, surface.depth);
            scalar(5, surface.content);
            scalar(6, surface.film);
            scalar(8, surface.ripple);
            scalar(9, surface.time);
            scalar(10, surface.tear);
            scalar(11, surface.retract);
            scalar(12, surface.remnant);
            glUniform1i(uniforms[13], surface.closing ? 1 : 0);
        };
        upload(m_surfaceUniforms[0], d.frame.window);
        upload(m_surfaceUniforms[1], d.frame.shell);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex));
    gl.setActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(wallpaperTex));
    gl.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
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
    gl.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return layer;
}

CBubblePassElement::CBubblePassElement(SData data) : m_data(std::move(data)) {}
std::vector<UP<IPassElement>> CBubblePassElement::draw(Render::CRenderContext &ctx) {
    const auto layer = m_data.renderer ? m_data.renderer->render(ctx, m_data.draw) : SP<Render::IFramebuffer>{};
    Render::GL::CHyprOpenGLImpl::STextureRenderData td{};
    td.damage = &m_data.damage;
    td.a = m_data.alpha;
    td.allowCustomUV = true;
    td.allowDim = false;
    if (layer) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, layer->getTexture(), m_data.box, paperCanvasQuad(), td);
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
        const CBox  box{m_data.box.x + (d.source.x - d.outputOrigin.x) * m_data.box.w / d.canvas.x,
                        m_data.box.y + (d.source.y - d.outputOrigin.y) * m_data.box.h / d.canvas.y,
                        d.source.w * m_data.box.w / d.canvas.x, d.source.h * m_data.box.h / d.canvas.y};
        const std::vector<SMeshRenderVertex> quad{
            {0.F, 0.F, u0, v0}, {0.F, 1.F, u0, v1}, {1.F, 0.F, u1, v0},
            {1.F, 0.F, u1, v0}, {0.F, 1.F, u0, v1}, {1.F, 1.F, u1, v1},
        };
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, d.window, box, quad, td);
    }
    return {};
}
bool                CBubblePassElement::needsLiveBlur(Render::CRenderContext &) { return false; }
bool                CBubblePassElement::needsPrecomputeBlur(Render::CRenderContext &) { return false; }
const char         *CBubblePassElement::passName() { return "liquid-glass-bubble"; }
ePassElementType    CBubblePassElement::type() { return EK_CUSTOM; }
std::optional<CBox> CBubblePassElement::boundingBox(Render::CRenderContext &) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}
CRegion CBubblePassElement::opaqueRegion(Render::CRenderContext &) { return {}; }
