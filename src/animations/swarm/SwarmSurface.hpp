#pragma once

#include <memory>
#include <span>

#include <GLES3/gl32.h>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Shader.hpp>

#include "animations/swarm/SwarmSurfaceBody.hpp"

class CSwarmSurface {
  public:
    CSwarmSurface() = default;
    ~CSwarmSurface();
    CSwarmSurface(const CSwarmSurface &) = delete;
    CSwarmSurface &operator=(const CSwarmSurface &) = delete;
    CSwarmSurface(CSwarmSurface &&) = delete;
    CSwarmSurface &operator=(CSwarmSurface &&) = delete;

    // The standing half is rebuilt from analytic bodies; zero radius preserves the input exactly.
    // radius controls smooth joins; corner_radius independently restores the native outline.
    SP<Render::IFramebuffer> filter(Render::CRenderContext &ctx,
        const SP<Render::IFramebuffer> &input,
        const Vector2D &size,
        double standing_at,
        double radius,
        double corner_radius,
        std::span<const hyprimate::SwarmSurfaceBody> bodies);

  private:
    bool initialize();
    std::unique_ptr<CShader> m_surface_shader, m_field_shader;
    GLuint m_program = 0, m_field_program = 0, m_vao = 0, m_quad = 0, m_instances = 0;
    GLuint m_field_texture = 0, m_field_framebuffer = 0;
    GLsizei m_field_width = 0, m_field_height = 0;
    std::size_t m_instance_capacity = 0;
    GLint m_field_size_uniform = -1, m_field_k = -1;
    GLint m_tex = -1, m_field = -1, m_size = -1, m_standing = -1, m_k = -1, m_canvas_radius = -1;
};
