#pragma once

#include <cstddef>
#include <span>

#include <GLES3/gl32.h>

#include "window/paper/PaperRenderMesh.hpp"

struct SPaperTarget {
    // Device pixels, in the same coordinate system as Draw::source/image_origin.
    hyprimate::Vec2 canvas{}, origin{};
    hyprimate::Box source{};
    hyprimate::Vec2 texture_origin{}, texture_size{};
    float alpha = 1;
    // Hyprland's export/composite mesh treats texture row zero as screen top.
    bool output_top_first = false;
};

// Compositor-independent production draw path. Programs are borrowed: CShader in production,
// GlHarness in tests. All GL methods require the creating context to be current.
class CPaperMaterial {
  public:
    CPaperMaterial() = default;
    CPaperMaterial(const CPaperMaterial &) = delete;
    CPaperMaterial &operator=(const CPaperMaterial &) = delete;
    CPaperMaterial(CPaperMaterial &&) = delete;
    CPaperMaterial &operator=(CPaperMaterial &&) = delete;
    bool initialize(GLuint program);
    void release();
    bool draw(const hyprimate::PaperSurface &surface,
        const SPaperTarget &target,
        GLuint window,
        GLuint output_framebuffer);
    [[nodiscard]] std::span<const SPaperVertex> vertices() const {
        return m_mesh.vertices();
    }

    // Changes framebuffer bindings, viewport, program, VAO, ARRAY_BUFFER, texture/sampler units
    // 0-1, and blend/cull/depth/scissor/stencil enables. The compositor wrapper brackets these.
    // Depth/blend parameters, clear values, renderbuffer binding, and colour mask are restored
    // here.
  private:
    bool upload(std::span<const SPaperVertex> vertices);
    bool resize(GLsizei width, GLsizei height, bool shaded);

    GLuint m_program = 0, m_vao = 0, m_buffer = 0;
    GLuint m_framebuffer = 0, m_shadow_framebuffer = 0;
    GLuint m_color = 0, m_depth = 0, m_depth_buffer = 0;
    GLsizei m_width = 0, m_height = 0;
    GLsizei m_shadow_width = 0, m_shadow_height = 0;
    std::size_t m_capacity = 0;
    CPaperRenderMesh m_mesh;
    GLint m_canvas = -1, m_center = -1, m_unit = -1, m_aspect = -1;
    GLint m_uv = -1, m_uv_bounds = -1;
    GLint m_image = -1, m_scene_depth = -1, m_opacity = -1, m_shading = -1, m_depth_pass = -1;
    GLint m_light_basis = -1, m_light_bounds = -1, m_light_range = -1;
    GLint m_shadow_size = -1, m_thickness = -1;
    GLint m_shadow_strength = -1;
};
