#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <GLES3/gl32.h>

#include "animations/paper/PaperSurface.hpp"

struct SPaperTarget {
    // Device pixels, in the same coordinate system as Draw::source/image_origin.
    hyprimate::Vec2 canvas{}, origin{};
    hyprimate::Box source{};
    hyprimate::Vec2 texture_origin{}, texture_size{};
    float alpha = 1;
    // Hyprland's export/composite mesh treats texture row zero as screen top.
    bool output_top_first = false;
};
struct SPaperVertex {
    float x, y, z, u, v, nx, ny, nz;
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
        return m_vertices;
    }

    // Changes framebuffer bindings, viewport, program, VAO, ARRAY_BUFFER, texture/sampler units
    // 0-1, and blend/cull/depth/scissor/stencil enables. The compositor wrapper brackets these.
    // Depth parameters, clear values, renderbuffer binding, and colour mask are restored here.
  private:
    struct Edge {
        std::uint32_t a, b;
        std::size_t first, second;
        std::size_t edge;
    };
    struct Neighbor {
        std::size_t corner = SIZE_MAX, edge = 0, other_edge = 0;
    };
    struct Bounds {
        hyprimate::Vec3 low{}, high{};
        std::size_t first = 0, count = 0, left = 0, right = 0;
    };
    void reconstruct(const hyprimate::PaperSurface &surface);
    std::size_t build_bounds(
        const hyprimate::PaperSurface &surface, std::size_t first, std::size_t count);
    double clearance(const hyprimate::PaperSurface &surface,
        hyprimate::Vec3 point,
        std::uint32_t endpoint_a,
        std::uint32_t endpoint_b,
        double limit,
        std::size_t branch = 0) const;
    bool mesh(const hyprimate::PaperSurface &surface);
    bool resize(GLsizei width, GLsizei height, bool shaded);
    void topology(const hyprimate::PaperSurface &surface);

    GLuint m_program = 0, m_vao = 0, m_buffer = 0;
    GLuint m_framebuffer = 0, m_shadow_framebuffer = 0;
    GLuint m_color = 0, m_depth = 0, m_depth_buffer = 0;
    GLsizei m_width = 0, m_height = 0;
    GLsizei m_shadow_width = 0, m_shadow_height = 0;
    std::size_t m_capacity = 0;
    std::vector<SPaperVertex> m_vertices;
    std::vector<SPaperVertex> m_corners, m_midpoints;
    std::vector<Bounds> m_bounds;
    std::vector<std::size_t> m_order;
    std::vector<hyprimate::PaperTriangle> m_faces;
    std::vector<Edge> m_edges;
    std::vector<std::array<Neighbor, 2>> m_neighbors;
    std::vector<std::size_t> m_heads, m_next, m_fan;
    std::vector<double> m_weights;
    std::vector<unsigned char> m_visited;
    std::vector<hyprimate::Vec3> m_normals;
    GLint m_canvas = -1, m_center = -1, m_unit = -1, m_aspect = -1;
    GLint m_uv = -1, m_uv_bounds = -1;
    GLint m_image = -1, m_scene_depth = -1, m_opacity = -1, m_shading = -1, m_depth_pass = -1;
    GLint m_light_basis = -1, m_light_bounds = -1, m_light_range = -1;
    GLint m_shadow_size = -1, m_thickness = -1;
};
