#pragma once

#include <span>

#include <GLES3/gl32.h>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "animations/swarm/SwarmSurfaceBody.hpp"

class CSwarmSurface {
  public:
    CSwarmSurface() = default;
    ~CSwarmSurface();
    CSwarmSurface(const CSwarmSurface &) = delete;
    CSwarmSurface &operator=(const CSwarmSurface &) = delete;

    // The standing half is rebuilt from analytic bodies; zero radius preserves the input exactly.
    // radius controls smooth joins; cornerRadius independently restores the native outline.
    SP<Render::IFramebuffer> filter(Render::CRenderContext &ctx,
        const SP<Render::IFramebuffer> &input,
        const Vector2D &size,
        double standingAt,
        double radius,
        double cornerRadius,
        std::span<const hyprimate::SwarmSurfaceBody> bodies);

  private:
    bool initialize();
    GLuint m_program = 0, m_fieldProgram = 0, m_vao = 0, m_quad = 0, m_instances = 0;
    GLuint m_fieldTexture = 0, m_fieldFramebuffer = 0;
    GLsizei m_fieldWidth = 0, m_fieldHeight = 0;
    std::size_t m_instanceCapacity = 0;
    GLint m_fieldSizeUniform = -1, m_fieldK = -1;
    GLint m_tex = -1, m_field = -1, m_size = -1, m_standing = -1, m_k = -1, m_canvasRadius = -1;
};
