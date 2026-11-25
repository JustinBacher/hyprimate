#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>

#include <GLES3/gl32.h>

#include "gl/ContextOwnerState.hpp"
#include "gl/GlProgram.hpp"
#include "window/burn/BurnDynamics.hpp"
#include "window/burn/BurnShaders.hpp"

struct SBurnTarget {
    // Device pixels, using the same capture/texture contract as animation::Draw.
    hyprimate::Vec2 canvas{}, origin{};
    hyprimate::Box source{};
    hyprimate::Vec2 texture_origin{}, texture_size{};
    float alpha = 1;
    bool output_top_first = false;
};

// Compositor-independent material. initialize/release/draw require the owning
// GLES context. The program is borrowed; the caller brackets raw GL state.
// Initialization failures leave a retryable empty material; failed target growth
// retains the previous drawable target and capacity.
class CBurnMaterial {
  public:
    static constexpr GLint MAX_TARGET_EDGE = 8192;
    static constexpr double MAX_TARGET_PIXELS = 16777216;
    CBurnMaterial() = default;
    ~CBurnMaterial() = default;
    CBurnMaterial(CBurnMaterial &&) = delete;
    CBurnMaterial &operator=(CBurnMaterial &&) = delete;
    CBurnMaterial(const CBurnMaterial &) = delete;
    CBurnMaterial &operator=(const CBurnMaterial &) = delete;
    bool initialize(GLuint program);
    void release();
    // Forget names from a context that can no longer be safely entered.
    void abandon() noexcept {
        m_program = 0;
        m_vao = {};
        m_buffer = {};
        m_framebuffer = m_color = m_depth = 0;
        m_target_width = m_target_height = 0;
        m_target_limit = 0;
    }
    bool draw(const hyprimate::BurnSimulation &simulation,
        const SBurnTarget &target,
        GLuint image,
        GLuint output_framebuffer);

  private:
    struct Vertex {
        float x, y, z, u, v, local_u, local_v, nx, ny, nz, kind, heat, opacity, age;
    };
    static constexpr std::size_t GRID = 96;
    static constexpr std::size_t DEBRIS_VERTICES = hyprimate::BurnSimulation::MAX_FRAGMENTS * 24 +
                                                   hyprimate::BurnSimulation::MAX_PARTICLES * 6;
    void bind_vertices(GLuint vao, GLuint buffer);
    std::size_t debris(const hyprimate::BurnSimulation &simulation, const SBurnTarget &target);
    bool resize(GLsizei width, GLsizei height);
    GLuint m_program = 0;
    std::array<GLuint, 2> m_vao{}, m_buffer{};
    GLuint m_framebuffer = 0, m_color = 0, m_depth = 0;
    GLsizei m_target_width = 0, m_target_height = 0;
    GLint m_target_limit = 0;
    std::array<Vertex, DEBRIS_VERTICES> m_debris{};
    GLint m_canvas = -1, m_source = -1, m_threshold = -1, m_seed = -1;
    GLint m_variation = -1;
    GLint m_width = -1, m_curl = -1, m_alpha = -1, m_closing = -1;
    GLint m_top_first = -1, m_image = -1, m_uv = -1, m_uv_bounds = -1;
};

// Context-independent cache operations. The caller classifies and claims the owner;
// all methods except accept/abandon require that owner to be current.
class CBurnResources {
  public:
    CBurnResources() = default;
    CBurnResources(const CBurnResources &) = delete;
    CBurnResources &operator=(const CBurnResources &) = delete;
    CBurnResources(CBurnResources &&) = delete;
    CBurnResources &operator=(CBurnResources &&) = delete;
    bool accept(hyprimate::gl::ContextOwnerState state) noexcept {
        if (state == hyprimate::gl::ContextOwnerState::unavailable) return false;
        if (state == hyprimate::gl::ContextOwnerState::replaced) abandon();
        return true;
    }
    void abandon() noexcept {
        m_material.abandon();
        m_program = 0;
        m_maximum_edge = 0;
    }
    void release() {
        m_material.release();
        if (m_program) glDeleteProgram(m_program);
        m_program = 0;
        m_maximum_edge = 0;
    }
    bool initialize(hyprimate::gl::ProgramDiagnostic report = nullptr) {
        if (m_program) return true;
        GLuint candidate = 0;
        try {
            const auto VERTEX = burn_shader::vertex();
            const auto FRAGMENT = burn_shader::fragment();
            candidate = hyprimate::gl::build_program(VERTEX.c_str(), FRAGMENT.c_str(), "Burn", report);
            if (candidate && m_material.initialize(candidate)) {
                m_program = candidate;
                return true;
            }
        } catch (const std::exception &) {
        }
        m_material.release();
        if (candidate) glDeleteProgram(candidate);
        return false;
    }
    bool supports(hyprimate::Vec2 size) {
        if (m_maximum_edge == 0) {
            GLint texture_limit = 0, renderbuffer_limit = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer_limit);
            m_maximum_edge =
                std::min({texture_limit, renderbuffer_limit, CBurnMaterial::MAX_TARGET_EDGE});
        }
        return size.x >= 1 && size.y >= 1 && size.x <= m_maximum_edge &&
               size.y <= m_maximum_edge && size.x * size.y <= CBurnMaterial::MAX_TARGET_PIXELS;
    }
    [[nodiscard]] GLuint program() const noexcept { return m_program; }
    CBurnMaterial &material() noexcept { return m_material; }

  private:
    CBurnMaterial m_material;
    GLuint m_program = 0;
    GLint m_maximum_edge = 0;
};
