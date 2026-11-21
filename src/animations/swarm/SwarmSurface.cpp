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
#include "gl/RawGlBracket.hpp"
#include "gl/Storage.hpp"
#include "swarm_shaders_generated.hpp"

CSwarmSurface::~CSwarmSurface() {
    if (!Render::GL::g_pHyprOpenGL) {
        // During compositor shutdown there is no backend/context in which CShader can delete GL
        // resources; deliberately release the wrappers and let context teardown reclaim them.
        [[maybe_unused]] const auto *surface = m_surface_shader.release();
        [[maybe_unused]] const auto *field = m_field_shader.release();
        return;
    }
    if ((m_program == 0U) && (m_field_program == 0U) && (m_vao == 0U) && (m_quad == 0U) &&
        (m_instances == 0U) && (m_field_framebuffer == 0U) && (m_field_texture == 0U)) {
        return;
    }
    const EGLDisplay DISPLAY = eglGetCurrentDisplay();
    const EGLContext CONTEXT = eglGetCurrentContext();
    const EGLSurface DRAW = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface READ = eglGetCurrentSurface(EGL_READ);
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    renderer.makeEGLCurrent();
    m_surface_shader.reset();
    m_field_shader.reset();
    renderer.bindArrayBuffer(0);
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
        renderer.onFramebufferDeleted(m_field_framebuffer);
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
    if (m_ready) {
        return true;
    }
    if (m_program == 0U) {
        if (!m_surface_shader) {
            m_surface_shader = std::make_unique<CShader>();
        }
        if (m_surface_shader->createProgram(swarm_shader::SURFACE_VERTEX.data(),
                swarm_shader::SURFACE_FRAGMENT.data(), true, true)) {
            m_program = m_surface_shader->program();
        }
    }
    if (m_field_program == 0U) {
        if (!m_field_shader) {
            m_field_shader = std::make_unique<CShader>();
        }
        if (m_field_shader->createProgram(swarm_shader::FIELD_VERTEX.data(),
                swarm_shader::FIELD_FRAGMENT.data(), true, true)) {
            m_field_program = m_field_shader->program();
        }
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
    if ((m_program == 0U) || (m_field_program == 0U) || (m_vao == 0U) || (m_quad == 0U) ||
        (m_instances == 0U) || (m_field_framebuffer == 0U)) {
        (void)std::fprintf(stderr, "swarm surface: cannot create GPU resources\n");
        return false;
    }
    constexpr std::array<float, 8> QUAD = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad);
    if (!hyprimate::gl::allocate_buffer(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD.data(), GL_STATIC_DRAW)) {
        return false;
    }
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): GLES encodes the bound buffer byte offset as a pointer.
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
    m_ready = true;
    return true;
}

bool CSwarmSurface::bind_field(GLsizei width, GLsizei height) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_field_framebuffer);
    if (width <= m_field_width && height <= m_field_height) { return true; }
    width = std::max(width, m_field_width);
    height = std::max(height, m_field_height);
    GLuint candidate = 0;
    glGenTextures(1, &candidate);
    if (candidate == 0) { return false; }
    glBindTexture(GL_TEXTURE_2D, candidate);
    if (!hyprimate::gl::allocate_texture_2d(GL_RGBA16F, width, height)) {
        glDeleteTextures(1, &candidate);
        return false;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, candidate, 0);
    if (!hyprimate::gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT0, candidate) ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_field_texture, 0);
        glDeleteTextures(1, &candidate);
        return false;
    }
    glDeleteTextures(1, &m_field_texture);
    m_field_texture = candidate;
    m_field_width = width;
    m_field_height = height;
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): GL capture, failure paths and restoration must retain their order.
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
        (void)std::fprintf(stderr, "swarm surface: invalid input or dimensions\n");
        return nullptr;
    }
    const int STANDING = static_cast<int>(std::ceil(standing_at));
    const GLsizei WIDTH = static_cast<GLsizei>(size.x) - STANDING;
    const auto HEIGHT = static_cast<GLsizei>(size.y);
    if (WIDTH == 0) {
        return input;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    const auto OUTPUT = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!OUTPUT || !OUTPUT->getTexture() || (OUTPUT->getTexture()->m_texID == 0U) ||
        OUTPUT->getTexture()->m_texID == input->getTexture()->m_texID) {
        (void)std::fprintf(stderr, "swarm surface: unable to allocate distinct work buffer\n");
        return nullptr;
    }

    const hyprimate::gl::CRawGlBracket<2> GL_STATE{{
        .viewport_width = static_cast<GLsizei>(size.x),
        .viewport_height = HEIGHT,
        .framebuffer = 0,
        .blend = false,
        .blend_src = GL_ONE,
        .blend_dst = GL_ZERO,
    }};
    constexpr std::array<GLfloat, 4> CLEAR_COLOR{0.F, 0.F, 0.F, 0.F};

    if (!initialize()) { return nullptr; }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    if (!bind_field(WIDTH, HEIGHT)) { return nullptr; }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    const std::size_t BYTES = bodies.size_bytes();
    if (BYTES > m_instance_capacity) {
        // A power-of-two capacity can exceed GLsizeiptr even when BYTES itself fits.
        constexpr auto MAX = static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
        const auto CAPACITY = BYTES > std::bit_floor(MAX) ? BYTES : std::bit_ceil(BYTES);
        if (!hyprimate::gl::allocate_buffer(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(CAPACITY), nullptr, GL_STREAM_DRAW)) {
            return nullptr;
        }
        m_instance_capacity = CAPACITY;
    }
    if (BYTES != 0U) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(BYTES), bodies.data());
    }
    const float SMOOTH_RADIUS = static_cast<float>(std::max(radius, 0.5));
    glViewport(0, 0, WIDTH, HEIGHT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, WIDTH, HEIGHT);
    glClearBufferfv(GL_COLOR, 0, CLEAR_COLOR.data());
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(m_field_program);
    glUniform2f(m_field_size_uniform, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
    glUniform1f(m_field_k, SMOOTH_RADIUS);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(bodies.size()));
    glBindFramebuffer(GL_FRAMEBUFFER,
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL owns this framebuffer.
        static_cast<Render::GL::CGLFramebuffer *>(OUTPUT.get())->getFBID());
    glViewport(0, 0, static_cast<GLsizei>(size.x), HEIGHT);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glUniform1i(m_tex, 0);
    glUniform1i(m_field, 1);
    glUniform2i(m_size, WIDTH, HEIGHT);
    glUniform1i(m_standing, STANDING);
    glUniform1f(m_k, SMOOTH_RADIUS);
    glUniform1f(m_canvas_radius, static_cast<float>(corner_radius));
    glBindSampler(0, 0);
    glBindSampler(1, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input->getTexture()->m_texID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_field_texture);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return OUTPUT;
}
