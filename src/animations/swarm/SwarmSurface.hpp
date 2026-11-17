#pragma once

#include <GLES3/gl32.h>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CSwarmSurface {
  public:
    CSwarmSurface() = default;
    ~CSwarmSurface();
    CSwarmSurface(const CSwarmSurface &) = delete;
    CSwarmSurface &operator=(const CSwarmSurface &) = delete;

    // input is a full-size alpha mask, with the standing region beginning at standingAt.
    // radius is the Gaussian standard deviation in physical pixels; zero returns input itself.
    // The caller draws the standing creatures over this mask after filtering.
    SP<Render::IFramebuffer> filter(Render::CRenderContext &ctx,
        const SP<Render::IFramebuffer> &input,
        const Vector2D &size,
        double standingAt,
        double radius);

  private:
    bool initialize();
    GLuint m_program = 0, m_vao = 0;
    GLint m_image = -1, m_original = -1, m_size = -1, m_standing = -1, m_radius = -1,
          m_vertical = -1;
};
