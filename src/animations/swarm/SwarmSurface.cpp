#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <span>
#include <utility>

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
        if (SHADER == 0U) {
            std::fprintf(stderr, "swarm surface: cannot create shader\n");
            return 0;
        }
        glShaderSource(SHADER, 1, &source, nullptr);
        glCompileShader(SHADER);
        GLint compile_status = 0;
        glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compile_status);
        if (compile_status == 0) {
            std::array<char, 2048> log{};
            glGetShaderInfoLog(SHADER, log.size(), nullptr, log.data());
            std::fprintf(stderr, "swarm surface shader: %s\n", log.data());
            glDeleteShader(SHADER);
            return 0;
        }
        return SHADER;
    }

    GLuint link_program(const char *vertex, const char *fragment) {
        const GLuint VERTEX_SHADER = compile(GL_VERTEX_SHADER, vertex);
        const GLuint FRAGMENT_SHADER = compile(GL_FRAGMENT_SHADER, fragment);
        GLuint program = 0;
        if ((VERTEX_SHADER != 0U) && (FRAGMENT_SHADER != 0U)) {
            program = glCreateProgram();
            if (program != 0U) {
                glAttachShader(program, VERTEX_SHADER);
                glAttachShader(program, FRAGMENT_SHADER);
                glLinkProgram(program);
                GLint link_status = 0;
                glGetProgramiv(program, GL_LINK_STATUS, &link_status);
                if (link_status == 0) {
                    std::array<char, 2048> log{};
                    glGetProgramInfoLog(program, log.size(), nullptr, log.data());
                    std::fprintf(stderr, "swarm surface program: %s\n", log.data());
                    glDeleteProgram(program);
                    program = 0;
                }
            }
        }
        if (program == 0U) {
            std::fprintf(stderr, "swarm surface: cannot create program\n");
        }
        if (VERTEX_SHADER != 0U) {
            glDeleteShader(VERTEX_SHADER);
        }
        if (FRAGMENT_SHADER != 0U) {
            glDeleteShader(FRAGMENT_SHADER);
        }
        return program;
    }
}  // namespace

CSwarmSurface::~CSwarmSurface() {
    if (((m_program == 0U) && (m_field_program == 0U) && (m_vao == 0U) && (m_quad == 0U) &&
            (m_instances == 0U) && (m_field_framebuffer == 0U) && (m_field_texture == 0U)) ||
        !Render::GL::g_pHyprOpenGL) {
        return;
    }
    const EGLDisplay DISPLAY = eglGetCurrentDisplay();
    const EGLContext CONTEXT = eglGetCurrentContext();
    const EGLSurface DRAW = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface READ = eglGetCurrentSurface(EGL_READ);
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    renderer.makeEGLCurrent();
    if (m_program != 0U) {
        glDeleteProgram(m_program);
    }
    if (m_field_program != 0U) {
        glDeleteProgram(m_field_program);
    }
    if (m_vao != 0U) {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_quad != 0U) {
        glDeleteBuffers(1, &m_quad);
    }
    if (m_instances != 0U) {
        glDeleteBuffers(1, &m_instances);
    }
    if (m_field_framebuffer != 0U) {
        glDeleteFramebuffers(1, &m_field_framebuffer);
    }
    if (m_field_texture != 0U) {
        glDeleteTextures(1, &m_field_texture);
    }
    if (DISPLAY != EGL_NO_DISPLAY && CONTEXT != EGL_NO_CONTEXT) {
        eglMakeCurrent(DISPLAY, DRAW, READ, CONTEXT);
    } else if (CONTEXT == EGL_NO_CONTEXT) {
        eglMakeCurrent(renderer.m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}

bool CSwarmSurface::initialize() {
    if ((m_program != 0U) && (m_field_program != 0U) && (m_vao != 0U) && (m_quad != 0U) &&
        (m_instances != 0U) && (m_field_framebuffer != 0U) && (m_field_texture != 0U)) {
        return true;
    }
    if (m_program == 0U) {
        m_program = link_program(
            swarm_shader::SURFACE_VERTEX.data(), swarm_shader::SURFACE_FRAGMENT.data());
    }
    if (m_field_program == 0U) {
        m_field_program =
            link_program(swarm_shader::FIELD_VERTEX.data(), swarm_shader::FIELD_FRAGMENT.data());
    }
    if (m_vao == 0U) {
        glGenVertexArrays(1, &m_vao);
    }
    if (m_quad == 0U) {
        glGenBuffers(1, &m_quad);
    }
    if (m_instances == 0U) {
        glGenBuffers(1, &m_instances);
    }
    if (m_field_framebuffer == 0U) {
        glGenFramebuffers(1, &m_field_framebuffer);
    }
    if (m_field_texture == 0U) {
        glGenTextures(1, &m_field_texture);
    }
    if ((m_program == 0U) || (m_field_program == 0U) || (m_vao == 0U) || (m_quad == 0U) ||
        (m_instances == 0U) || (m_field_framebuffer == 0U) || (m_field_texture == 0U)) {
        std::fprintf(stderr, "swarm surface: cannot create GPU resources\n");
        return false;
    }
    constexpr std::array<float, 8> QUAD = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD.data(), GL_STATIC_DRAW);
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
        reinterpret_cast<const void *>(offsetof(hyprimate::SwarmSurfaceBody, corner_radius)));
    glVertexAttribDivisor(2, 1);
    m_field_size_uniform = glGetUniformLocation(m_field_program, "fieldSize");
    m_field_k = glGetUniformLocation(m_field_program, "k");
    m_tex = glGetUniformLocation(m_program, "tex");
    m_field = glGetUniformLocation(m_program, "field");
    m_size = glGetUniformLocation(m_program, "fieldSize");
    m_standing = glGetUniformLocation(m_program, "standingAt");
    m_k = glGetUniformLocation(m_program, "k");
    m_canvas_radius = glGetUniformLocation(m_program, "canvasRadius");
    return true;
}

SP<Render::IFramebuffer> CSwarmSurface::filter(Render::CRenderContext &ctx,
    const SP<Render::IFramebuffer> &input,
    const Vector2D &size,
    double standing_at,
    double radius,
    double corner_radius,
    std::span<const hyprimate::SwarmSurfaceBody> bodies) {
    if (radius == 0.0) {
        return input;
    }
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !input || !input->getTexture() ||
        (input->getTexture()->m_texID == 0U) || !std::isfinite(radius) || radius < 0 ||
        !std::isfinite(corner_radius) || corner_radius < 0 || !std::isfinite(standing_at) ||
        !std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.y < 1 ||
        size.x > std::numeric_limits<GLsizei>::max() ||
        size.y > std::numeric_limits<GLsizei>::max() || std::floor(size.x) != size.x ||
        std::floor(size.y) != size.y || standing_at < 0 || standing_at > size.x ||
        radius > std::numeric_limits<GLint>::max() / 4.0 ||
        bodies.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        bodies.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) /
                            sizeof(hyprimate::SwarmSurfaceBody)) {
        std::fprintf(stderr, "swarm surface: invalid input or dimensions\n");
        return nullptr;
    }
    const int STANDING = static_cast<int>(std::ceil(standing_at));
    const GLsizei WIDTH = static_cast<GLsizei>(size.x) - STANDING;
    const auto HEIGHT = static_cast<GLsizei>(size.y);
    if (WIDTH == 0) {
        return input;
    }
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    const auto OUTPUT = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!OUTPUT || !OUTPUT->getTexture() || (OUTPUT->getTexture()->m_texID == 0U) ||
        OUTPUT->getTexture()->m_texID == input->getTexture()->m_texID) {
        std::fprintf(stderr, "swarm surface: unable to allocate distinct work buffer\n");
        return nullptr;
    }

    GLint program = 0;
    GLint vao = 0;
    GLint array_buffer = 0;
    GLint active = 0;
    std::array<GLint, 4> viewport;
    std::array<GLint, 2> textures;
    std::array<GLint, 2> samplers;
    GLint src = 0;
    GLint dst = 0;
    GLint src_alpha = 0;
    GLint dst_alpha = 0;
    GLint equation = 0;
    GLint equation_alpha = 0;
    std::array<GLint, 4> scissor_box;
    std::array<GLfloat, 4> clear_color;
    std::array<GLboolean, 4> mask;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box.data());
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color.data());
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != 0U;
    const bool STENCIL = glIsEnabled(GL_STENCIL_TEST) != 0U;
    const bool DEPTH = glIsEnabled(GL_DEPTH_TEST) != 0U;
    const bool CULL = glIsEnabled(GL_CULL_FACE) != 0U;
    const bool BLEND = renderer.blendEnabled();
    auto *texture = textures.begin();
    auto *sampler = samplers.begin();
    for (int i = 0; i < 2; ++i, ++texture, ++sampler) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &*texture);
        glGetIntegeri_v(GL_SAMPLER_BINDING, i, &*sampler);
    }

    bool ready = initialize();
    if (ready) {
        renderer.setCapStatus(GL_SCISSOR_TEST, false);
        renderer.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        renderer.setActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_field_texture);
        renderer.bindFramebuffer(GL_FRAMEBUFFER, m_field_framebuffer);
        // One surface serves differently sized windows in the same frame. Retain capacity,
        // rather than reallocating its texture every time the next window is smaller.
        if (WIDTH > m_field_width || HEIGHT > m_field_height) {
            const GLsizei CAPACITY_W = std::max(WIDTH, m_field_width);
            const GLsizei CAPACITY_H = std::max(HEIGHT, m_field_height);
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
            m_field_width = m_field_height = 0;
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_field_texture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr, "swarm surface: half-float framebuffer unavailable\n");
                ready = false;
            } else {
                m_field_width = CAPACITY_W;
                m_field_height = CAPACITY_H;
            }
        }
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instances);
        const std::size_t BYTES = bodies.size_bytes();
        if (BYTES > m_instance_capacity) {
            const auto CAPACITY = std::bit_ceil(BYTES);
            glBufferData(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(CAPACITY), nullptr, GL_STREAM_DRAW);
            GLint allocated = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
            if (std::cmp_less(allocated, BYTES)) {
                std::fprintf(stderr, "swarm surface: cannot allocate instance buffer\n");
                m_instance_capacity = 0;
                ready = false;
            } else {
                m_instance_capacity = static_cast<std::size_t>(allocated);
            }
        }
        if (ready && (BYTES != 0U)) {
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
            glUseProgram(m_field_program);
            glUniform2f(
                m_field_size_uniform, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
            glUniform1f(m_field_k, SMOOTH_RADIUS);
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
            glUniform1f(m_canvas_radius, static_cast<float>(corner_radius));
            glBindSampler(0, 0);
            glBindSampler(1, 0);
            renderer.setActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, input->getTexture()->m_texID);
            renderer.setActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_field_texture);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    texture = textures.begin();
    sampler = samplers.begin();
    for (int i = 0; i < 2; ++i, ++texture, ++sampler) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(*texture));
        glBindSampler(i, static_cast<GLuint>(*sampler));
    }
    renderer.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
    glUseProgram(static_cast<GLuint>(program));
    glClearColor(std::get<0>(clear_color),
        std::get<1>(clear_color),
        std::get<2>(clear_color),
        std::get<3>(clear_color));
    glColorMask(std::get<0>(mask), std::get<1>(mask), std::get<2>(mask), std::get<3>(mask));
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
    glScissor(std::get<0>(scissor_box),
        std::get<1>(scissor_box),
        std::get<2>(scissor_box),
        std::get<3>(scissor_box));
    renderer.setCapStatus(GL_SCISSOR_TEST, SCISSOR);
    renderer.setCapStatus(GL_STENCIL_TEST, STENCIL);
    renderer.blend(BLEND);
    renderer.blendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
    glBlendFuncSeparate(static_cast<GLenum>(src),
        static_cast<GLenum>(dst),
        static_cast<GLenum>(src_alpha),
        static_cast<GLenum>(dst_alpha));
    glBlendEquationSeparate(static_cast<GLenum>(equation), static_cast<GLenum>(equation_alpha));
    renderer.setViewport(
        std::get<0>(viewport), std::get<1>(viewport), std::get<2>(viewport), std::get<3>(viewport));
    return ready ? OUTPUT : nullptr;
}
