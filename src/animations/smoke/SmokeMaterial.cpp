#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "animations/smoke/SmokeMaterial.hpp"
#include "gl/Storage.hpp"

namespace {
    // Cells along the domain's longer side: fine enough for curling filaments, small enough
    // that every pass costs a fraction of a millisecond.
    constexpr GLsizei GRID_LONG = 320;
    constexpr int PRESSURE_ITERATIONS = 22;
    constexpr double MAX_STEP = 1.0 / 20.0;

    void bind(GLenum unit, GLenum target, GLuint texture) {
        glActiveTexture(unit);
        glBindTexture(target, texture);
    }
}  // namespace

void CSmokeField::release() {
    for (auto &texture : m_textures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        texture = 0;
    }
    if (m_framebuffer != 0) {
        glDeleteFramebuffers(1, &m_framebuffer);
    }
    *this = CSmokeField{};
}

bool CSmokeMaterial::initialize(const SSmokePrograms &programs) {
    if (m_ready) {
        return m_programs == programs.ids;
    }
    if (std::ranges::any_of(programs.ids, [](GLuint program) { return program == 0; })) {
        return false;
    }
    m_programs = programs.ids;
    constexpr std::array<const char *, UniformCount> NAMES{"u_window",
        "u_noise",
        "u_dye",
        "u_velocity_field",
        "u_pressure",
        "u_divergence",
        "u_predicted",
        "u_age",
        "u_predicted_age",
        "u_grid",
        "u_domain",
        "u_shift",
        "u_previous_domain",
        "u_domain_scale",
        "u_resize_rate",
        "u_half",
        "u_carry",
        "u_velocity",
        "u_source",
        "u_size",
        "u_tex_origin",
        "u_tex_size",
        "u_origin",
        "u_logical",
        "u_dt",
        "u_time",
        "u_seed",
        "u_smoke",
        "u_previous_smoke",
        "u_presence",
        "u_cohesion",
        "u_dispersal",
        "u_turbulence",
        "u_trail",
        "u_life",
        "u_gather",
        "u_fade",
        "u_position",
        "u_offset",
        "u_lag",
        "u_body_half",
        "u_body_radius",
        "u_body_offset",
        "u_body_stretch",
        "u_mass",
        "u_contraction",
        "u_settle",
        "u_transit",
        "u_head_velocity",
        "u_origin_uv",
        "u_order",
        "u_attract",
        "u_wind",
        "u_edge_point",
        "u_edge_dir",
        "u_density",
        "u_variation"};
    for (std::size_t p = 0; p < ProgramCount; ++p) {
        for (std::size_t i = 0; i < UniformCount; ++i) {
            m_uniforms[p][i] = glGetUniformLocation(m_programs[p], NAMES[i]);
        }
    }
    // Periodic trilinear 3D value noise; shared across all frames and windows.
    std::array<std::uint8_t, 32 * 32 * 32> noise{};
    std::uint32_t state = 0x91e10da5U;
    for (auto &value : noise) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        value = static_cast<std::uint8_t>(state >> 24);
    }
    glGenVertexArrays(1, &m_vao);
    glGenTextures(1, &m_noise);
    glGenTextures(1, &m_empty);
    const hyprimate::gl::ClientPixelUnpack UNPACK;
    bind(GL_TEXTURE1, GL_TEXTURE_3D, m_noise);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    const bool NOISE_READY = m_noise != 0 &&
        hyprimate::gl::allocate_texture_3d(GL_R8, 32, 32, 32);
    if (NOISE_READY)
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 32, 32, 32, GL_RED, GL_UNSIGNED_BYTE, noise.data());
    // Stands in for the density of a field that has not simulated yet.
    constexpr std::array<std::uint8_t, 4> EMPTY{};
    bind(GL_TEXTURE2, GL_TEXTURE_2D, m_empty);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const bool EMPTY_READY = m_empty != 0 &&
        hyprimate::gl::allocate_texture_2d(GL_RGBA8, 1, 1);
    if (EMPTY_READY)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, EMPTY.data());
    if (m_vao == 0 || !NOISE_READY || !EMPTY_READY) {
        release();
        return false;
    }
    m_ready = true;
    return true;
}

void CSmokeMaterial::release() {
    m_programs.fill(0);
    if (m_noise != 0) {
        glDeleteTextures(1, &m_noise);
    }
    if (m_empty != 0) {
        glDeleteTextures(1, &m_empty);
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
    m_noise = m_empty = m_vao = 0;
    m_ready = false;
}

void CSmokeMaterial::use(Program program) const {
    glUseProgram(m_programs[program]);
    const auto &uniforms = m_uniforms[program];
    // Fixed units: no pass samples both members of a shared unit.
    glUniform1i(uniforms[VelocityField], 0);
    glUniform1i(uniforms[Noise], 1);
    glUniform1i(uniforms[DyeField], 2);
    glUniform1i(uniforms[PressureField], 2);
    glUniform1i(uniforms[Window], 3);
    glUniform1i(uniforms[DivergenceField], 3);
    glUniform1i(uniforms[PredictedField], 4);
    glUniform1i(uniforms[AgeField], 5);
    glUniform1i(uniforms[PredictedAgeField], 6);
}

void CSmokeMaterial::motion(Program program, const hyprimate::SmokeFrame &frame) const {
    const auto F = [](double value) { return static_cast<GLfloat>(value); };
    const auto &u = m_uniforms[program];
    // Only the composite draws solid pixels; the fluid still follows the growing head.
    const auto BODY = program == Composite && frame.close_source_body
                          ? *frame.close_source_body
                          : hyprimate::smoke_body(frame);
    glUniform2f(u[Carry], F(frame.carry.x), F(frame.carry.y));
    glUniform2f(u[WindowVelocity], F(frame.velocity.x), F(frame.velocity.y));
    glUniform2f(u[Lag], F(frame.lag.x), F(frame.lag.y));
    glUniform2f(u[Position], F(frame.position.x), F(frame.position.y));
    glUniform2f(u[Offset], F(frame.offset.x), F(frame.offset.y));
    glUniform2f(u[BodyHalf], F(BODY.half.x), F(BODY.half.y));
    glUniform1f(u[BodyRadius], F(BODY.radius));
    glUniform2f(u[BodyOffset], F(BODY.offset.x), F(BODY.offset.y));
    glUniform2f(u[BodyStretch], F(BODY.stretch.x), F(BODY.stretch.y));
    glUniform1f(u[Mass], F(frame.mass));
    glUniform1f(u[Contraction], F(frame.contraction));
    glUniform1f(u[Settle], F(frame.settle));
    glUniform1f(u[Transit], F(frame.transit));
    glUniform2f(u[HeadVelocity], F(frame.head_velocity.x), F(frame.head_velocity.y));
    glUniform2f(u[OriginUV], F(frame.origin_uv.x), F(frame.origin_uv.y));
    glUniform1f(u[Order], F(frame.order));
    glUniform1f(u[Attract], F(frame.attract));
    glUniform1f(u[Wind], F(frame.wind));
    glUniform2f(u[EdgePoint], F(frame.edge_point.x), F(frame.edge_point.y));
    glUniform2f(u[EdgeDir], F(frame.edge_dir.x), F(frame.edge_dir.y));
    glUniform1f(u[Density], F(frame.density));
    glUniform1f(u[Variation], F(frame.variation));
    glUniform1f(u[Gather], F(frame.gather));
    glUniform1f(u[Time], F(frame.time));
    glUniform1f(u[Seed], F(frame.seed));
    glUniform1f(u[Smoke], F(frame.smoke));
    glUniform1f(u[Life], F(std::max(frame.life, 0.05)));
}

void CSmokeMaterial::pass(GLuint target) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void CSmokeMaterial::pass(GLuint target, GLuint age) {
    constexpr std::array<GLenum, 2> BOTH{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    constexpr GLenum FIRST = GL_COLOR_ATTACHMENT0;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, age, 0);
    glDrawBuffers(2, BOTH.data());
    pass(target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    glDrawBuffers(1, &FIRST);
}

bool CSmokeMaterial::allocate(CSmokeField &field, GLsizei width, GLsizei height) const {
    // Publish only a fully initialized set; a failed attempt never changes a survivor.
    CSmokeField pending;
    const auto FAIL = [&] {
        pending.release();
        return false;
    };
    glGenFramebuffers(1, &pending.m_framebuffer);
    glGenTextures(static_cast<GLsizei>(pending.m_textures.size()), pending.m_textures.data());
    if (pending.m_framebuffer == 0 ||
        std::ranges::any_of(pending.m_textures, [](GLuint texture) { return texture == 0; }))
        return FAIL();
    glBindFramebuffer(GL_FRAMEBUFFER, pending.m_framebuffer);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    for (std::size_t i = 0; i < pending.m_textures.size(); ++i) {
        const bool VELOCITY = i == CSmokeField::VelocityA || i == CSmokeField::VelocityB;
        const bool DYE = i == CSmokeField::DyeA || i == CSmokeField::DyeB ||
            i == CSmokeField::Predicted;
        const GLenum INTERNAL = DYE ? GL_RGBA16F : (VELOCITY ? GL_RG16F : GL_R16F);
        bind(GL_TEXTURE0, GL_TEXTURE_2D, pending.m_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!hyprimate::gl::allocate_texture_2d(INTERNAL, width, height))
            return FAIL();
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pending.m_textures[i], 0);
        if (!hyprimate::gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT0, pending.m_textures[i]) ||
            glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return FAIL();
        glClear(GL_COLOR_BUFFER_BIT);
    }
    // The two density/age passes need simultaneous attachments, not just individually
    // renderable textures. Validate every pairing before any simulation can mutate it.
    constexpr std::array<GLenum, 2> BOTH{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    constexpr std::array<std::array<std::size_t, 2>, 3> PAIRS{{
        {CSmokeField::Predicted, CSmokeField::PredictedAge},
        {CSmokeField::DyeA, CSmokeField::AgeA},
        {CSmokeField::DyeB, CSmokeField::AgeB}}};
    glDrawBuffers(2, BOTH.data());
    for (const auto &pair : PAIRS) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            pending.m_textures[pair[0]], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
            pending.m_textures[pair[1]], 0);
        if (!hyprimate::gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT0, pending.m_textures[pair[0]]) ||
            !hyprimate::gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT1, pending.m_textures[pair[1]]) ||
            glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return FAIL();
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    glDrawBuffers(1, BOTH.data());
    pending.m_width = width;
    pending.m_height = height;
    field.release();
    field = pending;
    return true;
}

bool CSmokeMaterial::simulate(CSmokeField &field,
    const hyprimate::SmokeFrame &frame,
    const SSmokeTarget &target,
    const SSmokeImages &images) {
    if (!m_ready) {
        return false;
    }
    const double DT = field.m_primed ? std::min(frame.time - field.m_time, MAX_STEP) : 0.0;
    if (field.m_primed && !(DT > 0.0)) {
        return true;
    }
    const hyprimate::Vec2 DOMAIN = hyprimate::smoke_domain(frame.size);

    std::array<GLfloat, 4> clear{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
    const auto RESTORE = [&] { glClearColor(clear[0], clear[1], clear[2], clear[3]); };
    // Keep the allocation for this field's lifetime. Resizing changes its world-space
    // domain, not its storage; the advection passes remap the previous domain below.
    if (field.m_framebuffer == 0) {
        const bool WIDE = DOMAIN.x >= DOMAIN.y;
        const double RATIO = WIDE ? DOMAIN.y / DOMAIN.x : DOMAIN.x / DOMAIN.y;
        const auto SHORT = static_cast<GLsizei>(std::max(16.0, std::round(GRID_LONG * RATIO)));
        const GLsizei WIDTH = WIDE ? GRID_LONG : SHORT;
        const GLsizei HEIGHT = WIDE ? SHORT : GRID_LONG;
        if (!allocate(field, WIDTH, HEIGHT)) {
            RESTORE();
            return false;
        }
        field.m_primed = false;
    }
    if (!field.m_primed) {
        field.m_primed = true;
        field.m_time = frame.time;
        field.m_smoke = frame.smoke;
        field.m_centre = {.x = frame.position.x + frame.offset.x,
            .y = frame.position.y + frame.offset.y};
        field.m_domain = DOMAIN;
        field.m_size = frame.size;
        RESTORE();
        return true;
    }

    const auto F = [](double value) { return static_cast<GLfloat>(value); };
    const hyprimate::Vec2 CENTRE{
        .x = frame.position.x + frame.offset.x, .y = frame.position.y + frame.offset.y};
    const hyprimate::Vec2 SHIFT{.x = (CENTRE.x - field.m_centre.x) / field.m_domain.x,
        .y = (CENTRE.y - field.m_centre.y) / field.m_domain.y};
    const double ELAPSED = frame.time - field.m_time;
    const double RESIZE_BLEND = 1.0 - std::exp(-DT / 0.09);
    const auto RATE = [&](double size, double previous, double rate) {
        const double TARGET = std::clamp((size - previous) / (previous * ELAPSED), -6.0, 6.0);
        return rate + (TARGET - rate) * RESIZE_BLEND;
    };
    field.m_resize_rate = {
        .x = RATE(frame.size.x, field.m_size.x, field.m_resize_rate.x),
        .y = RATE(frame.size.y, field.m_size.y, field.m_resize_rate.y)};
    glBindFramebuffer(GL_FRAMEBUFFER, field.m_framebuffer);
    glViewport(0, 0, field.m_width, field.m_height);
    glBindVertexArray(m_vao);
    bind(GL_TEXTURE1, GL_TEXTURE_3D, m_noise);
    const auto COMMON = [&](Program program) {
        use(program);
        const auto &u = m_uniforms[program];
        glUniform2f(u[Grid], F(field.m_width), F(field.m_height));
        glUniform2f(u[Domain], F(DOMAIN.x), F(DOMAIN.y));
        glUniform2f(u[Shift], F(SHIFT.x), F(SHIFT.y));
        glUniform2f(u[PreviousDomain], F(field.m_domain.x), F(field.m_domain.y));
        glUniform2f(u[DomainScale], F(DOMAIN.x / field.m_domain.x), F(DOMAIN.y / field.m_domain.y));
        glUniform2f(u[ResizeRate], F(field.m_resize_rate.x), F(field.m_resize_rate.y));
        glUniform2f(u[Half], F(frame.size.x * 0.5), F(frame.size.y * 0.5));
        motion(program, frame);
        glUniform1f(u[Dt], F(DT));
        glUniform1f(u[PreviousSmoke], F(field.m_smoke));
        glUniform1f(u[Presence], F(frame.presence));
        glUniform1f(u[Cohesion], F(frame.cohesion));
        glUniform1f(u[Dispersal], F(frame.dispersal));
        glUniform1f(u[Turbulence], F(frame.turbulence));
        glUniform1f(u[Trail], F(frame.trail));
        glUniform1f(u[Gather], F(frame.gather));
    };
    auto &textures = field.m_textures;
    const auto OTHER_VELOCITY = [&] {
        return field.m_velocity == CSmokeField::VelocityA ? CSmokeField::VelocityB
                                                           : CSmokeField::VelocityA;
    };

    COMMON(Velocity);
    bind(GL_TEXTURE0, GL_TEXTURE_2D, textures[field.m_velocity]);
    bind(GL_TEXTURE2, GL_TEXTURE_2D, textures[field.m_dye]);
    bind(GL_TEXTURE5, GL_TEXTURE_2D, textures[field.m_age]);
    pass(textures[OTHER_VELOCITY()]);
    field.m_velocity = OTHER_VELOCITY();

    COMMON(Divergence);
    bind(GL_TEXTURE0, GL_TEXTURE_2D, textures[field.m_velocity]);
    pass(textures[CSmokeField::Divergence]);

    COMMON(Pressure);
    bind(GL_TEXTURE3, GL_TEXTURE_2D, textures[CSmokeField::Divergence]);
    for (int i = 0; i < PRESSURE_ITERATIONS; ++i) {
        const auto NEXT = field.m_pressure == CSmokeField::PressureA ? CSmokeField::PressureB
                                                                      : CSmokeField::PressureA;
        bind(GL_TEXTURE2, GL_TEXTURE_2D, textures[field.m_pressure]);
        pass(textures[NEXT]);
        field.m_pressure = NEXT;
    }

    COMMON(Project);
    bind(GL_TEXTURE0, GL_TEXTURE_2D, textures[field.m_velocity]);
    bind(GL_TEXTURE2, GL_TEXTURE_2D, textures[field.m_pressure]);
    pass(textures[OTHER_VELOCITY()]);
    field.m_velocity = OTHER_VELOCITY();

    COMMON(Advect);
    bind(GL_TEXTURE0, GL_TEXTURE_2D, textures[field.m_velocity]);
    bind(GL_TEXTURE2, GL_TEXTURE_2D, textures[field.m_dye]);
    bind(GL_TEXTURE5, GL_TEXTURE_2D, textures[field.m_age]);
    pass(textures[CSmokeField::Predicted], textures[CSmokeField::PredictedAge]);

    COMMON(Dye);
    const auto &u = m_uniforms[Dye];
    glUniform2f(u[Source], F(target.source_pos.x), F(target.source_pos.y));
    glUniform2f(u[Size], F(target.source_size.x), F(target.source_size.y));
    glUniform2f(u[TextureOrigin], F(images.texture_origin.x), F(images.texture_origin.y));
    glUniform2f(u[TextureSize], F(images.texture_size.x), F(images.texture_size.y));
    const auto NEXT_DYE = field.m_dye == CSmokeField::DyeA ? CSmokeField::DyeB : CSmokeField::DyeA;
    const auto NEXT_AGE = field.m_age == CSmokeField::AgeA ? CSmokeField::AgeB : CSmokeField::AgeA;
    bind(GL_TEXTURE3, GL_TEXTURE_2D, images.window);
    bind(GL_TEXTURE4, GL_TEXTURE_2D, textures[CSmokeField::Predicted]);
    bind(GL_TEXTURE6, GL_TEXTURE_2D, textures[CSmokeField::PredictedAge]);
    pass(textures[NEXT_DYE], textures[NEXT_AGE]);
    field.m_dye = NEXT_DYE;
    field.m_age = NEXT_AGE;

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    field.m_time = frame.time;
    field.m_smoke = frame.smoke;
    field.m_centre = CENTRE;
    field.m_domain = DOMAIN;
    field.m_size = frame.size;
    RESTORE();
    return true;
}

void CSmokeMaterial::draw(const CSmokeField &field,
    const hyprimate::SmokeFrame &frame,
    const SSmokeTarget &target,
    const SSmokeImages &images) {
    if (!m_ready) {
        return;
    }
    const auto F = [](double value) { return static_cast<GLfloat>(value); };
    use(Composite);
    glBindVertexArray(m_vao);
    bind(GL_TEXTURE1, GL_TEXTURE_3D, m_noise);
    bind(GL_TEXTURE0, GL_TEXTURE_2D,
        field.m_primed ? field.m_textures[field.m_velocity] : m_empty);
    bind(GL_TEXTURE2, GL_TEXTURE_2D, field.m_primed ? field.m_textures[field.m_dye] : m_empty);
    bind(GL_TEXTURE3, GL_TEXTURE_2D, images.window);
    bind(GL_TEXTURE5, GL_TEXTURE_2D, field.m_primed ? field.m_textures[field.m_age] : m_empty);
    const auto &u = m_uniforms[Composite];
    // The grid lies where it was last stepped. Map it from the window's current position, so
    // a frame that does not step still shows the smoke fixed in the world.
    const hyprimate::Vec2 DOMAIN = field.m_primed ? field.m_domain : hyprimate::smoke_domain(frame.size);
    glUniform2f(u[Origin], F(target.origin.x), F(target.origin.y));
    glUniform2f(u[Source], F(target.source_pos.x), F(target.source_pos.y));
    glUniform2f(u[Size], F(target.source_size.x), F(target.source_size.y));
    glUniform2f(u[TextureOrigin], F(images.texture_origin.x), F(images.texture_origin.y));
    glUniform2f(u[TextureSize], F(images.texture_size.x), F(images.texture_size.y));
    glUniform2f(u[Logical], F(frame.size.x), F(frame.size.y));
    glUniform2f(u[Domain], F(DOMAIN.x), F(DOMAIN.y));
    glUniform2f(u[Grid], F(std::max(field.m_width, 1)), F(std::max(field.m_height, 1)));
    motion(Composite, frame);
    if (field.m_primed) {
        glUniform2f(u[Offset], F(field.m_centre.x - frame.position.x), F(field.m_centre.y - frame.position.y));
    }
    glUniform1f(u[Fade], F(field.m_primed ? frame.fade : 0.0));
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
