#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "animations/burn/BurnMaterial.hpp"

namespace {
    using hyprimate::Vec3;
    Vec3 rotate(Vec3 point, Vec3 sine, Vec3 cosine) {
        const Vec3 X{
            point.x, point.y * cosine.x - point.z * sine.x, point.y * sine.x + point.z * cosine.x};
        const Vec3 Y{X.x * cosine.y + X.z * sine.y, X.y, -X.x * sine.y + X.z * cosine.y};
        return {Y.x * cosine.z - Y.y * sine.z, Y.x * sine.z + Y.y * cosine.z, Y.z};
    }
    bool finite(hyprimate::Vec2 p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
    }
    constexpr std::array<std::array<float, 2>, 6> CORNERS{
        {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}}};
}  // namespace
namespace {
    // Depth and renderbuffer state are not covered by RawGlBracket.
    struct LocalState {
        GLint depth_function = 0, renderbuffer = 0;
        GLboolean depth_mask = GL_TRUE;
        LocalState() {
            glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        }
        ~LocalState() {
            glDepthFunc(static_cast<GLenum>(depth_function));
            glDepthMask(depth_mask);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        }
        LocalState(const LocalState &) = delete;
        LocalState &operator=(const LocalState &) = delete;
    };
}  // namespace

void CBurnMaterial::bind_vertices(GLuint vao, GLuint buffer) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    constexpr std::array<GLint, 5> SIZES{3, 2, 2, 3, 4};
    constexpr std::array<std::size_t, 5> OFFSETS{offsetof(Vertex, x),
        offsetof(Vertex, u),
        offsetof(Vertex, local_u),
        offsetof(Vertex, nx),
        offsetof(Vertex, kind)};
    for (GLuint i = 0; i < SIZES.size(); ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            SIZES[i],
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<const void *>(OFFSETS[i]));
    }
}

bool CBurnMaterial::initialize(GLuint program) {
    if (m_program != 0) return m_program == program;
    if (program == 0) return false;
    m_program = program;
    GLint texture_limit = 0, renderbuffer_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer_limit);
    m_target_limit = std::min({texture_limit, renderbuffer_limit, MAX_TARGET_EDGE});
    m_canvas = glGetUniformLocation(program, "canvasSize");
    m_source = glGetUniformLocation(program, "source");
    m_threshold = glGetUniformLocation(program, "threshold");
    m_seed = glGetUniformLocation(program, "seedPhase");
    m_width = glGetUniformLocation(program, "frontWidth");
    m_curl = glGetUniformLocation(program, "curl");
    m_variation = glGetUniformLocation(program, "variation");
    m_alpha = glGetUniformLocation(program, "alpha");
    m_closing = glGetUniformLocation(program, "closing");
    m_top_first = glGetUniformLocation(program, "topFirst");
    m_image = glGetUniformLocation(program, "image");
    m_uv = glGetUniformLocation(program, "uvTransform");
    m_uv_bounds = glGetUniformLocation(program, "uvBounds");
    glGenVertexArrays(2, m_vao.data());
    glGenBuffers(2, m_buffer.data());
    glGenFramebuffers(1, &m_framebuffer);
    glGenTextures(1, &m_color);
    glGenRenderbuffers(1, &m_depth);
    if (!m_vao[0] || !m_vao[1] || !m_buffer[0] || !m_buffer[1] || !m_framebuffer || !m_color ||
        !m_depth) {
        release();
        return false;
    }
    // Immutable tessellation; no grid allocation or upload on animation frames.
    std::vector<Vertex> grid;
    grid.reserve(6 + GRID * GRID * 6);
    for (const auto &corner : CORNERS)
        grid.push_back({0, 0, 0, corner[0], corner[1], corner[0], corner[1], 0, 0, 1, 0, 1, 1, 0});
    for (std::size_t y = 0; y < GRID; ++y) {
        for (std::size_t x = 0; x < GRID; ++x) {
            for (const auto &corner : CORNERS) {
                const float U = (static_cast<float>(x) + corner[0]) / static_cast<float>(GRID);
                const float V = (static_cast<float>(y) + corner[1]) / static_cast<float>(GRID);
                grid.push_back({0, 0, 0, U, V, U, V, 0, 0, 1, 0, 1, 1, 0});
            }
        }
    }
    bind_vertices(m_vao[0], m_buffer[0]);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(grid.size() * sizeof(Vertex)),
        grid.data(),
        GL_STATIC_DRAW);
    bind_vertices(m_vao[1], m_buffer[1]);
    glBufferData(
        GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(m_debris)), nullptr, GL_DYNAMIC_DRAW);
    return true;
}

bool CBurnMaterial::resize(GLsizei width, GLsizei height) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    if (width <= m_target_width && height <= m_target_height) return true;
    auto capacity_x =
        std::max(m_target_width, std::min(m_target_limit, ((width + 255) / 256) * 256));
    auto capacity_y =
        std::max(m_target_height, std::min(m_target_limit, ((height + 255) / 256) * 256));
    if (static_cast<double>(capacity_x) * capacity_y > MAX_TARGET_PIXELS) {
        // A tall window after an ultrawide one must not inherit an unusable
        // cross-product of their dimensions. draw() checked this exact size.
        capacity_x = width;
        capacity_y = height;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, capacity_x, capacity_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, capacity_x, capacity_y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    m_target_width = capacity_x;
    m_target_height = capacity_y;
    return true;
}

void CBurnMaterial::release() {
    glDeleteBuffers(2, m_buffer.data());
    glDeleteVertexArrays(2, m_vao.data());
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_color);
    glDeleteRenderbuffers(1, &m_depth);
    m_framebuffer = m_color = m_depth = 0;
    m_target_width = m_target_height = 0;
    m_vao = {};
    m_buffer = {};
    m_program = 0;
}

std::size_t CBurnMaterial::debris(
    const hyprimate::BurnSimulation &simulation, const SBurnTarget &target) {
    const auto &capture = simulation.front().capture;
    const double SX = target.source.w / capture.w, SY = target.source.h / capture.h;
    const double SZ = std::sqrt(SX * SY);
    std::size_t count = 0;
    for (const auto &flake : simulation.fragments()) {
        const double BEND =
            std::min(flake.size.x, flake.size.y) *
            (.18 + simulation.params().curl * (.6 + .8 * flake.age / flake.lifetime));
        const Vec3 SINE{
            std::sin(flake.rotation.x), std::sin(flake.rotation.y), std::sin(flake.rotation.z)};
        const Vec3 COSINE{
            std::cos(flake.rotation.x), std::cos(flake.rotation.y), std::cos(flake.rotation.z)};
        for (unsigned row = 0; row < 2; ++row) {
            for (unsigned column = 0; column < 2; ++column) {
                for (const auto &corner : CORNERS) {
                    const double LOCAL_U = (column + corner[0]) * .5;
                    const double LOCAL_V = (row + corner[1]) * .5;
                    const double LOCAL_X = LOCAL_U - .5;
                    const double LOCAL_Y = LOCAL_V - .5;
                    const Vec3 POINT = rotate({LOCAL_X * flake.size.x,
                                                  LOCAL_Y * flake.size.y,
                                                  (LOCAL_X * LOCAL_X + LOCAL_Y * LOCAL_Y) * BEND},
                        SINE,
                        COSINE);
                    const Vec3 NORMAL = rotate(
                        {-2 * LOCAL_X * BEND / flake.size.x, -2 * LOCAL_Y * BEND / flake.size.y, 1},
                        SINE,
                        COSINE);
                    m_debris[count++] = {
                        static_cast<float>(target.source.x - target.origin.x +
                                           (flake.position.x - capture.x + POINT.x) * SX),
                        static_cast<float>(target.source.y - target.origin.y +
                                           (flake.position.y - capture.y + POINT.y) * SY),
                        static_cast<float>((POINT.z + flake.depth) * SZ),
                        static_cast<float>(flake.uv.x + LOCAL_U * flake.uv.w),
                        static_cast<float>(flake.uv.y + LOCAL_V * flake.uv.h),
                        static_cast<float>(LOCAL_U),
                        static_cast<float>(LOCAL_V),
                        static_cast<float>(NORMAL.x),
                        static_cast<float>(NORMAL.y),
                        static_cast<float>(NORMAL.z),
                        1,
                        static_cast<float>(flake.heat),
                        static_cast<float>(flake.opacity),
                        static_cast<float>(std::clamp(flake.age / flake.lifetime, 0.0, 1.0))};
                }
            }
        }
    }
    for (const auto &particle : simulation.particles()) {
        for (const auto &corner : CORNERS) {
            m_debris[count++] = {
                static_cast<float>(
                    target.source.x - target.origin.x +
                    (particle.position.x - capture.x + (corner[0] - .5) * particle.size) * SX),
                static_cast<float>(
                    target.source.y - target.origin.y +
                    (particle.position.y - capture.y + (corner[1] - .5) * particle.size) * SY),
                0,
                0,
                0,
                corner[0],
                corner[1],
                0,
                0,
                1,
                particle.ember ? 3.F : 2.F,
                static_cast<float>(particle.heat),
                static_cast<float>(particle.opacity),
                static_cast<float>(std::clamp((particle.size - 2.0) * .25, 0.0, 1.0))};
        }
    }
    return count;
}

bool CBurnMaterial::draw(const hyprimate::BurnSimulation &simulation,
    const SBurnTarget &target,
    GLuint image,
    GLuint output_framebuffer) {
    const auto &front = simulation.front();
    if (m_program == 0 || !image || !simulation.initialized() ||
        !hyprimate::is_usable(front.capture) || !hyprimate::is_usable(target.source) ||
        !finite(target.canvas) || !finite(target.origin) || !finite(target.texture_origin) ||
        !finite(target.texture_size) || target.canvas.x < 1 || target.canvas.y < 1 ||
        target.texture_size.x < 1 || target.texture_size.y < 1 || !std::isfinite(target.alpha))
        return false;
    // RawGlBracket restores all state touched below. glClearBufferfv does not
    // mutate the compositor's clear colour.
    if (target.canvas.x > m_target_limit || target.canvas.y > m_target_limit ||
        target.canvas.x * target.canvas.y > MAX_TARGET_PIXELS)
        return false;
    const LocalState STATE;
    const auto WIDTH = static_cast<GLsizei>(target.canvas.x);
    const auto HEIGHT = static_cast<GLsizei>(target.canvas.y);
    if (!resize(WIDTH, HEIGHT)) return false;
    glViewport(0, 0, static_cast<GLsizei>(target.canvas.x), static_cast<GLsizei>(target.canvas.y));
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, WIDTH, HEIGHT);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    constexpr std::array<GLfloat, 4> CLEAR{};
    glClearBufferfv(GL_COLOR, 0, CLEAR.data());
    constexpr GLfloat DEPTH = 1;
    glClearBufferfv(GL_DEPTH, 0, &DEPTH);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    glUniform2f(m_canvas, static_cast<float>(target.canvas.x), static_cast<float>(target.canvas.y));
    glUniform4f(m_source,
        static_cast<float>(target.source.x - target.origin.x),
        static_cast<float>(target.source.y - target.origin.y),
        static_cast<float>(target.source.w),
        static_cast<float>(target.source.h));
    glUniform1f(m_threshold, static_cast<float>(front.progress));
    glUniform1f(m_seed, hyprimate::burn_phase(front.seed));
    glUniform1f(
        m_variation, static_cast<float>(std::clamp(simulation.params().variation, 0.0, 1.0)));
    glUniform1f(m_width, static_cast<float>(std::max(.0001, simulation.params().front_width)));
    glUniform1f(m_curl,
        simulation.params().consume
            ? 0.F
            : static_cast<float>(std::clamp(simulation.params().curl, 0.0, 1.5)));
    glUniform1f(m_alpha, std::clamp(target.alpha, 0.F, 1.F));
    glUniform1i(m_closing, front.closing ? 1 : 0);
    glUniform1i(m_top_first, target.output_top_first ? 1 : 0);
    glUniform1i(m_image, 0);
    const auto &S = target.source;
    const auto &O = target.texture_origin;
    const auto &T = target.texture_size;
    glUniform4f(m_uv,
        static_cast<float>((S.x - O.x) / T.x),
        static_cast<float>((S.y - O.y) / T.y),
        static_cast<float>(S.w / T.x),
        static_cast<float>(S.h / T.y));
    glUniform4f(m_uv_bounds,
        static_cast<float>(std::max(.5, S.x - O.x + .5) / T.x),
        static_cast<float>(std::max(.5, S.y - O.y + .5) / T.y),
        static_cast<float>(std::min(T.x - .5, S.x + S.w - O.x - .5) / T.x),
        static_cast<float>(std::min(T.y - .5, S.y + S.h - O.y - .5) / T.y));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image);
    glBindSampler(0, 0);
    if (front.progress > 0) {
        glBindVertexArray(m_vao[0]);
        const bool FLAT = !front.closing || simulation.params().consume ||
                          simulation.params().curl <= 0 || front.progress >= 1;
        glDrawArrays(GL_TRIANGLES, FLAT ? 0 : 6, FLAT ? 6 : static_cast<GLsizei>(GRID * GRID * 6));
    }
    const auto COUNT = debris(simulation, target);
    if (COUNT > 0) {
        glBindVertexArray(m_vao[1]);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer[1]);
        glBufferSubData(
            GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(COUNT * sizeof(Vertex)), m_debris.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(COUNT));
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output_framebuffer);
    glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    return true;
}
