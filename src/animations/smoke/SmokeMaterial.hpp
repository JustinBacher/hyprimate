#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl32.h>

#include "animations/smoke/Smoke.hpp"

struct SSmokeTarget {
    hyprimate::Vec2 canvas, origin, source_pos, source_size;
};
struct SSmokeImages {
    GLuint window = 0;
    hyprimate::Vec2 texture_origin, texture_size;
};

// One window's smoke fluid: velocity, pressure and window-coloured density on a grid that
// follows the window while the smoke itself stays anchored to the world.
// release() needs the owning context current.
class CSmokeField {
  public:
    void release();

  private:
    friend class CSmokeMaterial;
    enum Texture : std::size_t {
        VelocityA,
        VelocityB,
        DyeA,
        DyeB,
        Predicted,
        PressureA,
        PressureB,
        Divergence,
        TextureCount
    };
    std::array<GLuint, TextureCount> m_textures{};
    GLuint m_framebuffer = 0;
    GLsizei m_width = 0, m_height = 0;
    std::size_t m_velocity = VelocityA, m_dye = DyeA, m_pressure = PressureA;
    double m_time = 0, m_smoke = 0;
    hyprimate::Vec2 m_centre;  // world position of the grid centre at the last step
    hyprimate::Vec2 m_domain, m_size, m_resize_rate;
    bool m_primed = false;
};

// Independent of Hyprland. The caller owns framebuffer, viewport, blend and tests, and
// restores texture units 0-4 (2D and 3D), program and VAO afterwards.
class CSmokeMaterial {
  public:
    bool initialize();
    void release();
    // Advances the field to frame.time; later calls for the same time do nothing.
    // Uses its own framebuffer and restores the caller's framebuffers and viewport.
    bool simulate(CSmokeField &field,
        const hyprimate::SmokeFrame &frame,
        const SSmokeTarget &target,
        const SSmokeImages &images);
    // Draws into the caller's framebuffer.
    void draw(const CSmokeField &field,
        const hyprimate::SmokeFrame &frame,
        const SSmokeTarget &target,
        const SSmokeImages &images);

  private:
    enum Program : std::size_t {
        Velocity,
        Divergence,
        Pressure,
        Project,
        Advect,
        Dye,
        Composite,
        ProgramCount
    };
    enum Uniform : std::size_t {
        Window,
        Noise,
        DyeField,
        VelocityField,
        PressureField,
        DivergenceField,
        PredictedField,
        Grid,
        Domain,
        Shift,
        PreviousDomain,
        DomainScale,
        ResizeRate,
        Half,
        Carry,
        WindowVelocity,
        Source,
        Size,
        TextureOrigin,
        TextureSize,
        Origin,
        Logical,
        Dt,
        Time,
        Seed,
        Smoke,
        PreviousSmoke,
        Presence,
        Cohesion,
        Dispersal,
        Turbulence,
        Trail,
        Gather,
        Fade,
        Position,
        Offset,
        Lag,
        BodyHalf,
        BodyRadius,
        BodyOffset,
        BodyStretch,
        Mass,
        Contraction,
        Settle,
        Transit,
        HeadVelocity,
        OriginUV,
        Order,
        Attract,
        Wind,
        EdgePoint,
        EdgeDir,
        Density,
        Variation,
        UniformCount
    };
    void use(Program program) const;
    // Uniforms every pass reads from the frame: motion, scrolling and the body shape.
    void motion(Program program, const hyprimate::SmokeFrame &frame) const;
    static void pass(GLuint target);
    bool allocate(CSmokeField &field, GLsizei width, GLsizei height) const;
    std::array<GLuint, ProgramCount> m_programs{};
    std::array<std::array<GLint, UniformCount>, ProgramCount> m_uniforms{};
    GLuint m_vao = 0, m_noise = 0, m_empty = 0;
    bool m_tried = false;
};
