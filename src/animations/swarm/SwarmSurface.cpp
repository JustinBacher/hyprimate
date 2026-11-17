#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <span>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "animations/swarm/SwarmSurface.hpp"
#include "animations/swarm/SwarmSurfaceBody.hpp"
#include "swarm_shaders_generated.hpp"

namespace {
    GLuint compile(GLenum type, const char *source) {
        const GLuint SHADER = glCreateShader(type);
        if (SHADER == 0u) {
            std::fprintf(stderr, "swarm surface: cannot create shader\n");
            return 0;
        }
        glShaderSource(SHADER, 1, &source, nullptr);
        glCompileShader(SHADER);
        GLint compileStatus = 0;
        glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == 0) {
            char log[2048]{};
            glGetShaderInfoLog(SHADER, sizeof(log), nullptr, log);
            std::fprintf(stderr, "swarm surface shader: %s\n", log);
            glDeleteShader(SHADER);
            return 0;
        }
        return SHADER;
    }

    GLuint linkProgram(const char *vertex, const char *fragment) {
        const GLuint VERTEX_SHADER = compile(GL_VERTEX_SHADER, vertex);
        const GLuint FRAGMENT_SHADER = compile(GL_FRAGMENT_SHADER, fragment);
        GLuint program = 0;
        if ((VERTEX_SHADER != 0u) && (FRAGMENT_SHADER != 0u)) {
            program = glCreateProgram();
            if (program != 0u) {
                glAttachShader(program, VERTEX_SHADER);
                glAttachShader(program, FRAGMENT_SHADER);
                glLinkProgram(program);
                GLint linkStatus = 0;
                glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
                if (linkStatus == 0) {
                    char log[2048]{};
                    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                    std::fprintf(stderr, "swarm surface program: %s\n", log);
                    glDeleteProgram(program);
                    program = 0;
                }
            }
        }
        if (program == 0u) {
            std::fprintf(stderr, "swarm surface: cannot create program\n");
        }
        if (VERTEX_SHADER != 0u) {
            glDeleteShader(VERTEX_SHADER);
        }
        if (FRAGMENT_SHADER != 0u) {
            glDeleteShader(FRAGMENT_SHADER);
        }
        return program;
    }
}  // namespace

CSwarmSurface::~CSwarmSurface() {
    if (((m_program == 0u) && (m_fieldProgram == 0u) && (m_vao == 0u) && (m_quad == 0u) &&
            (m_instances == 0u) && (m_fieldFramebuffer == 0u) && (m_fieldTexture == 0u)) ||
        !Render::GL::g_pHyprOpenGL) {
        return;
    }
    const EGLDisplay DISPLAY = eglGetCurrentDisplay();
    const EGLContext CONTEXT = eglGetCurrentContext();
    const EGLSurface DRAW = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface READ = eglGetCurrentSurface(EGL_READ);
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    renderer.makeEGLCurrent();
    if (m_program != 0u) {
        glDeleteProgram(m_program);
    }
    if (m_fieldProgram != 0u) {
        glDeleteProgram(m_fieldProgram);
    }
    if (m_vao != 0u) {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_quad != 0u) {
        glDeleteBuffers(1, &m_quad);
    }
    if (m_instances != 0u) {
        glDeleteBuffers(1, &m_instances);
    }
    if (m_fieldFramebuffer != 0u) {
        glDeleteFramebuffers(1, &m_fieldFramebuffer);
    }
    if (m_fieldTexture != 0u) {
        glDeleteTextures(1, &m_fieldTexture);
    }
    if (DISPLAY != EGL_NO_DISPLAY && CONTEXT != EGL_NO_CONTEXT) {
        eglMakeCurrent(DISPLAY, DRAW, READ, CONTEXT);
    } else if (CONTEXT == EGL_NO_CONTEXT) {
        eglMakeCurrent(renderer.m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}

bool CSwarmSurface::initialize() {
    if ((m_program != 0u) && (m_fieldProgram != 0u) && (m_vao != 0u) && (m_quad != 0u) &&
        (m_instances != 0u) && (m_fieldFramebuffer != 0u) && (m_fieldTexture != 0u)) {
        return true;
    }
    if (m_program == 0u) {
        m_program = linkProgram(swarm_shader::SURFACE_VERTEX, swarm_shader::SURFACE_FRAGMENT);
    }
    if (m_fieldProgram == 0u) {
        m_fieldProgram = linkProgram(swarm_shader::FIELD_VERTEX, swarm_shader::FIELD_FRAGMENT);
    }
    if (m_vao == 0u) {
        glGenVertexArrays(1, &m_vao);
    }
    if (m_quad == 0u) {
        glGenBuffers(1, &m_quad);
    }
    if (m_instances == 0u) {
        glGenBuffers(1, &m_instances);
    }
    if (m_fieldFramebuffer == 0u) {
        glGenFramebuffers(1, &m_fieldFramebuffer);
    }
    if (m_fieldTexture == 0u) {
        glGenTextures(1, &m_fieldTexture);
    }
    if ((m_program == 0u) || (m_fieldProgram == 0u) || (m_vao == 0u) || (m_quad == 0u) ||
        (m_instances == 0u) || (m_fieldFramebuffer == 0u) || (m_fieldTexture == 0u)) {
        std::fprintf(stderr, "swarm surface: cannot create GPU resources\n");
        return false;
    }
    constexpr float QUAD[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(hyprimate::SwarmSurfaceBody), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(hyprimate::SwarmSurfaceBody),
        reinterpret_cast<const void *>(offsetof(hyprimate::SwarmSurfaceBody, cornerRadius)));
    glVertexAttribDivisor(2, 1);
    m_fieldSizeUniform = glGetUniformLocation(m_fieldProgram, "fieldSize");
    m_fieldK = glGetUniformLocation(m_fieldProgram, "k");
    m_tex = glGetUniformLocation(m_program, "tex");
    m_field = glGetUniformLocation(m_program, "field");
    m_size = glGetUniformLocation(m_program, "fieldSize");
    m_standing = glGetUniformLocation(m_program, "standingAt");
    m_k = glGetUniformLocation(m_program, "k");
    m_canvasRadius = glGetUniformLocation(m_program, "canvasRadius");
    return true;
}

SP<Render::IFramebuffer> CSwarmSurface::filter(Render::CRenderContext &ctx,
    const SP<Render::IFramebuffer> &input,
    const Vector2D &size,
    double standingAt,
    double radius,
    double cornerRadius,
    std::span<const hyprimate::SwarmSurfaceBody> bodies) {
    if (radius == 0.0) {
        return input;
    }
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !input || !input->getTexture() ||
        (input->getTexture()->m_texID == 0u) || !std::isfinite(radius) || radius < 0 ||
        !std::isfinite(cornerRadius) || cornerRadius < 0 || !std::isfinite(standingAt) ||
        !std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.y < 1 ||
        size.x > std::numeric_limits<GLsizei>::max() ||
        size.y > std::numeric_limits<GLsizei>::max() || std::floor(size.x) != size.x ||
        std::floor(size.y) != size.y || standingAt < 0 || standingAt > size.x ||
        radius > std::numeric_limits<GLint>::max() / 4.0 ||
        bodies.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        bodies.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(bodies[0])) {
        std::fprintf(stderr, "swarm surface: invalid input or dimensions\n");
        return nullptr;
    }
    const int STANDING = static_cast<int>(std::ceil(standingAt));
    const GLsizei WIDTH = static_cast<GLsizei>(size.x) - STANDING;
    const auto HEIGHT = static_cast<GLsizei>(size.y);
    if (WIDTH == 0) {
        return input;
    }
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    const auto OUTPUT = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!OUTPUT || !OUTPUT->getTexture() || (OUTPUT->getTexture()->m_texID == 0u) ||
        OUTPUT->getTexture()->m_texID == input->getTexture()->m_texID) {
        std::fprintf(stderr, "swarm surface: unable to allocate distinct work buffer\n");
        return nullptr;
    }

    GLint program = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint active = 0;
    GLint viewport[4];
    GLint textures[2];
    GLint samplers[2];
    GLint src = 0;
    GLint dst = 0;
    GLint srcAlpha = 0;
    GLint dstAlpha = 0;
    GLint equation = 0;
    GLint equationAlpha = 0;
    GLint scissorBox[4];
    GLfloat clearColor[4];
    GLboolean mask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != 0u;
    const bool STENCIL = glIsEnabled(GL_STENCIL_TEST) != 0u;
    const bool DEPTH = glIsEnabled(GL_DEPTH_TEST) != 0u;
    const bool CULL = glIsEnabled(GL_CULL_FACE) != 0u;
    const bool BLEND = renderer.blendEnabled();
    for (int i = 0; i < 2; ++i) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, i, &samplers[i]);
    }

    bool ready = initialize();
    if (ready) {
        renderer.setCapStatus(GL_SCISSOR_TEST, false);
        renderer.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        renderer.setActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fieldTexture);
        renderer.bindFramebuffer(GL_FRAMEBUFFER, m_fieldFramebuffer);
        // One surface serves differently sized windows in the same frame. Retain capacity,
        // rather than reallocating its texture every time the next window is smaller.
        if (WIDTH > m_fieldWidth || HEIGHT > m_fieldHeight) {
            const GLsizei CAPACITY_W = std::max(WIDTH, m_fieldWidth);
            const GLsizei CAPACITY_H = std::max(HEIGHT, m_fieldHeight);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // A failed resize must not leave a complete, incorrectly sized old texture.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 0, 0, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGBA16F,
                CAPACITY_W,
                CAPACITY_H,
                0,
                GL_RGBA,
                GL_HALF_FLOAT,
                nullptr);
            m_fieldWidth = m_fieldHeight = 0;
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fieldTexture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr, "swarm surface: half-float framebuffer unavailable\n");
                ready = false;
            } else {
                m_fieldWidth = CAPACITY_W;
                m_fieldHeight = CAPACITY_H;
            }
        }
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instances);
        const std::size_t BYTES = bodies.size_bytes();
        if (BYTES > m_instanceCapacity) {
            const auto CAPACITY = std::bit_ceil(BYTES);
            glBufferData(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(CAPACITY), nullptr, GL_STREAM_DRAW);
            GLint allocated = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
            if (allocated < 0 || static_cast<std::size_t>(allocated) < BYTES) {
                std::fprintf(stderr, "swarm surface: cannot allocate instance buffer\n");
                m_instanceCapacity = 0;
                ready = false;
            } else {
                m_instanceCapacity = static_cast<std::size_t>(allocated);
            }
        }
        if (ready && (BYTES != 0u)) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(BYTES), bodies.data());
        }
        if (ready) {
            const float SMOOTH_RADIUS = static_cast<float>(std::max(radius, 0.5));
            renderer.setViewport(0, 0, WIDTH, HEIGHT);
            glScissor(0, 0, WIDTH, HEIGHT);
            renderer.setCapStatus(GL_SCISSOR_TEST, true);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.setCapStatus(GL_SCISSOR_TEST, false);
            renderer.blend(true);
            renderer.blendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
            glUseProgram(m_fieldProgram);
            glUniform2f(m_fieldSizeUniform, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
            glUniform1f(m_fieldK, SMOOTH_RADIUS);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(bodies.size()));
            g_pHyprRenderer->bindFB(ctx, OUTPUT);
            renderer.setViewport(0, 0, static_cast<GLsizei>(size.x), HEIGHT);
            renderer.blend(false);
            glUseProgram(m_program);
            glUniform1i(m_tex, 0);
            glUniform1i(m_field, 1);
            glUniform2i(m_size, WIDTH, HEIGHT);
            glUniform1i(m_standing, STANDING);
            glUniform1f(m_k, SMOOTH_RADIUS);
            glUniform1f(m_canvasRadius, static_cast<float>(cornerRadius));
            glBindSampler(0, 0);
            glBindSampler(1, 0);
            renderer.setActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, input->getTexture()->m_texID);
            renderer.setActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_fieldTexture);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    for (int i = 0; i < 2; ++i) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures[i]));
        glBindSampler(i, static_cast<GLuint>(samplers[i]));
    }
    renderer.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
    glUseProgram(static_cast<GLuint>(program));
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (DEPTH) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (CULL) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    renderer.setCapStatus(GL_SCISSOR_TEST, SCISSOR);
    renderer.setCapStatus(GL_STENCIL_TEST, STENCIL);
    renderer.blend(BLEND);
    renderer.blendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
    glBlendFuncSeparate(static_cast<GLenum>(src),
        static_cast<GLenum>(dst),
        static_cast<GLenum>(srcAlpha),
        static_cast<GLenum>(dstAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(equation), static_cast<GLenum>(equationAlpha));
    renderer.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ready ? OUTPUT : nullptr;
}
