#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl32.h>

#include "window/burn/BurnDynamics.hpp"

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
