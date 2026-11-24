#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl32.h>

#include "window/smoke/Smoke.hpp"

struct SSmokeTarget {
    hyprimate::Vec2 canvas, origin, source_pos, source_size;
};
struct SSmokeImages {
    GLuint window = 0;
    hyprimate::Vec2 texture_origin, texture_size;
};

struct SSmokePrograms {
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
    std::array<GLuint, ProgramCount> ids{};
};

// One window's smoke fluid: velocity, pressure, window-coloured density and the density's age
// on a grid that follows the window while the smoke itself stays anchored to the world.
// release() needs the owning context current.
class CSmokeField {
  public:
    void release();
    [[nodiscard]] GLuint framebuffer() const {
        return m_framebuffer;
    }

  private:
    friend class CSmokeMaterial;
    enum Texture : std::size_t {
        VelocityA,
        VelocityB,
        DyeA,
        DyeB,
        Predicted,
        // Density-weighted age in seconds since the smoke left the body, transported exactly
        // as the density is so every fragment keeps the age of the smoke it came from.
        AgeA,
        AgeB,
        PredictedAge,
        PressureA,
        PressureB,
        Divergence,
        TextureCount
    };
    std::array<GLuint, TextureCount> m_textures{};
    GLuint m_framebuffer = 0;
    GLsizei m_width = 0, m_height = 0;
    std::size_t m_velocity = VelocityA, m_dye = DyeA, m_age = AgeA, m_pressure = PressureA;
    double m_time = 0, m_smoke = 0;
    hyprimate::Vec2 m_centre;  // world position of the grid centre at the last step
    hyprimate::Vec2 m_position;  // window position at the last simulated frame
    hyprimate::Vec2 m_domain, m_size, m_resize_rate;
    bool m_primed = false;
};

// Independent of Hyprland. The caller owns framebuffer, viewport, blend and tests, and
// restores texture units 0-6 (2D and 3D), program and VAO afterwards.
class CSmokeMaterial {
  public:
    // Readiness is published only after all shared textures exist. Failure cleans partial
    // resources and the same instance can initialize again with a complete program set.
    bool initialize(const SSmokePrograms &programs);
    void release();
    // Advances the field to frame.time; later calls for the same time do nothing.
    // Initial storage is transactional; later window resizes reuse it and remap the fluid.
    // Uses its own framebuffer and viewport; the caller must restore them afterwards.
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
    using Program = SSmokePrograms::Program;
    using enum SSmokePrograms::Program;
    enum Uniform : std::size_t {
        Window,
        Noise,
        DyeField,
        VelocityField,
        PressureField,
        DivergenceField,
        PredictedField,
        AgeField,
        PredictedAgeField,
        Grid,
        Domain,
        Shift,
        PreviousDomain,
        DomainScale,
        ResizeRate,
        Half,
        Carry,
        WindowVelocity,
        DragMotion,
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
        TrailLength,
        TrailGirth,
        Life,
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
        Tint,
        Density,
        Variation,
        UniformCount
    };
    void use(Program program) const;
    // Uniforms every pass reads from the frame: motion, scrolling and the body shape.
    void motion(Program program, const hyprimate::SmokeFrame &frame, hyprimate::Vec2 drag_delta) const;
    static void pass(GLuint target);
    // Writes the density and its age together.
    static void pass(GLuint target, GLuint age);
    bool allocate(CSmokeField &field, GLsizei width, GLsizei height) const;
    std::array<GLuint, ProgramCount> m_programs{};
    std::array<std::array<GLint, UniformCount>, ProgramCount> m_uniforms{};
    GLuint m_vao = 0, m_noise = 0, m_empty = 0;
    bool m_ready = false;
};
