#include <cmath>
#include <cstdio>
#include <limits>

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/swarm/SwarmSurface.hpp"
#include "swarm_shaders_generated.hpp"

namespace {
    GLuint compile(GLenum type, const char *source) {
        const GLuint shader = glCreateShader(type);
        if (!shader) {
            std::fprintf(stderr, "swarm surface: cannot create shader\n");
            return 0;
        }
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::fprintf(stderr, "swarm surface shader: %s\n", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
}  // namespace

CSwarmSurface::~CSwarmSurface() {
    if ((!m_program && !m_vao) || !Render::GL::g_pHyprOpenGL) return;
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    auto &gl = *Render::GL::g_pHyprOpenGL;
    gl.makeEGLCurrent();
    if (m_program) glDeleteProgram(m_program);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
        eglMakeCurrent(display, draw, read, context);
    else if (context == EGL_NO_CONTEXT)
        eglMakeCurrent(gl.m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool CSwarmSurface::initialize() {
    if (m_program && m_vao) return true;
    const GLuint vs = compile(GL_VERTEX_SHADER, swarm_shader::SURFACE_VERTEX);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, swarm_shader::SURFACE_FRAGMENT);
    if (vs && fs) {
        m_program = glCreateProgram();
        if (m_program) {
            glAttachShader(m_program, vs);
            glAttachShader(m_program, fs);
            glLinkProgram(m_program);
            GLint ok = 0;
            glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048]{};
                glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
                std::fprintf(stderr, "swarm surface program: %s\n", log);
                glDeleteProgram(m_program);
                m_program = 0;
            }
        } else
            std::fprintf(stderr, "swarm surface: cannot create program\n");
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (m_program) glGenVertexArrays(1, &m_vao);
    if (!m_vao && m_program) {
        std::fprintf(stderr, "swarm surface: cannot create vertex array\n");
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_program && m_vao) {
        m_image = glGetUniformLocation(m_program, "image");
        m_original = glGetUniformLocation(m_program, "original");
        m_size = glGetUniformLocation(m_program, "imageSize");
        m_standing = glGetUniformLocation(m_program, "standingAt");
        m_radius = glGetUniformLocation(m_program, "radius");
        m_vertical = glGetUniformLocation(m_program, "vertical");
    }
    return m_program && m_vao;
}

SP<Render::IFramebuffer> CSwarmSurface::filter(Render::CRenderContext &ctx,
    const SP<Render::IFramebuffer> &input,
    const Vector2D &size,
    double standingAt,
    double radius) {
    if (radius == 0.0) return input;
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !input || !input->getTexture() ||
        !input->getTexture()->m_texID || !std::isfinite(radius) || radius < 0 ||
        !std::isfinite(standingAt) || !std::isfinite(size.x) || !std::isfinite(size.y) ||
        size.x < 1 || size.y < 1 || size.x > std::numeric_limits<GLsizei>::max() ||
        size.y > std::numeric_limits<GLsizei>::max() || std::floor(size.x) != size.x ||
        std::floor(size.y) != size.y || standingAt < 0 || standingAt > size.x ||
        radius > std::numeric_limits<GLint>::max() / 4.0) {
        std::fprintf(stderr, "swarm surface: invalid input or dimensions\n");
        return nullptr;
    }
    auto &gl = *Render::GL::g_pHyprOpenGL;
    Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto drawState = ctx.saveDrawState();
    const auto intermediate = g_pHyprRenderer->getWorkBuffer(ctx, size);
    const auto output = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!intermediate || !output || !intermediate->getTexture() || !output->getTexture() ||
        !intermediate->getTexture()->m_texID || !output->getTexture()->m_texID ||
        intermediate->getTexture()->m_texID == input->getTexture()->m_texID ||
        output->getTexture()->m_texID == input->getTexture()->m_texID ||
        output->getTexture()->m_texID == intermediate->getTexture()->m_texID) {
        std::fprintf(stderr, "swarm surface: unable to allocate distinct work buffers\n");
        return nullptr;
    }

    GLint program, vao, active, viewport[4], textures[2], samplers[2];
    GLint src, dst, srcAlpha, dstAlpha;
    GLboolean mask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
    const bool blend = gl.blendEnabled();
    for (int i = 0; i < 2; ++i) {
        gl.setActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, i, &samplers[i]);
    }

    const bool ready = initialize();
    if (ready) {
        gl.setCapStatus(GL_SCISSOR_TEST, false);
        gl.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        gl.blend(false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(m_program);
        glBindVertexArray(m_vao);
        glUniform1i(m_image, 0);
        glUniform1i(m_original, 1);
        glUniform2i(m_size, int(size.x), int(size.y));
        glUniform1i(m_standing, int(std::ceil(standingAt)));
        glUniform1f(m_radius, float(radius));
        glBindSampler(0, 0);
        glBindSampler(1, 0);
        gl.setActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, input->getTexture()->m_texID);
        gl.setActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input->getTexture()->m_texID);
        g_pHyprRenderer->bindFB(ctx, intermediate);
        gl.setViewport(0, 0, static_cast<GLsizei>(size.x), static_cast<GLsizei>(size.y));
        glUniform1i(m_vertical, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, intermediate->getTexture()->m_texID);
        g_pHyprRenderer->bindFB(ctx, output);
        gl.setViewport(0, 0, static_cast<GLsizei>(size.x), static_cast<GLsizei>(size.y));
        glUniform1i(m_vertical, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    for (int i = 0; i < 2; ++i) {
        gl.setActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures[i]));
        glBindSampler(i, static_cast<GLuint>(samplers[i]));
    }
    gl.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glUseProgram(static_cast<GLuint>(program));
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (depth)
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
    if (srcAlpha != src || dstAlpha != dst)
        glBlendFuncSeparate(static_cast<GLenum>(src),
            static_cast<GLenum>(dst),
            static_cast<GLenum>(srcAlpha),
            static_cast<GLenum>(dstAlpha));
    gl.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ready ? output : nullptr;
}
