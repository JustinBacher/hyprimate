#pragma once

#include <array>
#include <cstddef>
#include <span>

#include <GLES3/gl3.h>

#include "window/burn/BurnGround.hpp"

class CBurnGroundDebris {
  public:
    using Instance = std::array<GLfloat, 11>;
    // A monitor can show its pinned, ordinary and special-workspace areas together.
    static constexpr std::size_t MAX_INSTANCES = 3 * CBurnGround::MAX_PARTICLES;

    CBurnGroundDebris() = default;
    ~CBurnGroundDebris() = default;
    CBurnGroundDebris(const CBurnGroundDebris &) = delete;
    CBurnGroundDebris &operator=(const CBurnGroundDebris &) = delete;
    CBurnGroundDebris(CBurnGroundDebris &&) = delete;
    CBurnGroundDebris &operator=(CBurnGroundDebris &&) = delete;

    // All GL calls belong to the ground pass's owning context and raw state bracket.
    // Initialization is idempotent and leaves no partial objects on failure. The caller
    // must release on that context; if it is gone, context teardown reclaims the GL names.
    [[nodiscard]] bool initialize();
    void release();

    // Pack a live particle into screen-relative logical coordinates and birth UV.
    // screen is the usable whole-output logical box, not the work buffer's viewport.
    [[nodiscard]] static Instance pack(
        const CBurnGround::Particle &particle, hyprimate::Box screen);

    // FBO, viewport and wallpaper on unit 0 are already bound by the ground pass.
    // Both boxes are in world logical coordinates (y down); the basis maps whole-output
    // normalized coordinates to the safe wallpaper texture, even for a partial capture.
    void draw(std::span<const Instance> instances,
        hyprimate::Box viewport,
        hyprimate::Box screen,
        hyprimate::Vec2 origin,
        hyprimate::Vec2 axis_x,
        hyprimate::Vec2 axis_y) const;

  private:
    GLuint m_program = 0, m_vao = 0, m_instances = 0;
    GLint m_clip_transform = -1, m_screen_scale = -1, m_origin = -1, m_basis = -1;
};
