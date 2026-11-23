#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "workspace/rotation/RotationGeometry.hpp"
#include "workspace/rotation/RotationRenderer.hpp"

namespace {
    constexpr int WIDTH = 96;
    constexpr int HEIGHT = 64;
    using hyprimate::workspace::Direction;

    GLuint texture(const std::vector<unsigned char> &pixels) {
        GLuint texture_id = 0;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return texture_id;
    }

    GLuint framebuffer(GLuint tex) {
        GLuint framebuffer_id = 0;
        glGenFramebuffers(1, &framebuffer_id);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_id);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        return framebuffer_id;
    }
    class TestRenderer {
      public:
        bool initialize();
        void release();
        bool draw_raw(GLuint src,
            GLuint dst,
            GLuint out,
            GLuint source_framebuffer,
            GLuint presentation_framebuffer,
            int width,
            int height,
            double position,
            Direction direction,
            const hyprimate::workspace::GapFill &gap,
            bool matching_wallpaper,
            GLuint source_wallpaper,
            GLuint destination_wallpaper);

      private:
        GLuint m_program = 0;
        GLuint m_vao = 0;
        std::array<GLint, 13> m_uniforms{};
    };
    using hyprimate::workspace::GapFill;
    bool TestRenderer::initialize() {
        m_program = gl_harness::build_program(
            hyprimate::workspace::VERTEX_SHADER, hyprimate::workspace::FRAGMENT_SHADER);
        if (m_program == 0) {
            return false;
        }
        if (!hyprimate::workspace::rotation_uniforms(m_program, m_uniforms)) {
            return false;
        }
        glGenVertexArrays(1, &m_vao);
        return m_vao != 0;
    }
    void TestRenderer::release() {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteProgram(m_program);
    }
    bool TestRenderer::draw_raw(GLuint src,
        GLuint dst,
        GLuint out,
        GLuint source_framebuffer,
        GLuint presentation_framebuffer,
        int width,
        int height,
        double position,
        Direction direction,
        const GapFill &gap,
        bool matching_wallpaper,
        GLuint source_wallpaper,
        GLuint destination_wallpaper) {
        GLint read_framebuffer = 0;
        GLint draw_framebuffer = 0;
        GLint program = 0;
        GLint vao = 0;
        GLint active = 0;
        std::array<GLint, 4> textures{};
        std::array<GLint, 4> samplers{};
        std::array<GLint, 4> viewport{};
        std::array<GLboolean, 4> mask{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
        const std::array<GLenum, 5> CAPS{
            GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND};
        std::array<GLboolean, 5> enabled{};
        for (std::size_t i = 0; i < CAPS.size(); ++i) {
            enabled[i] = glIsEnabled(CAPS[i]);
            glDisable(CAPS[i]);
        }
        constexpr std::size_t UNITS = 4;
        for (std::size_t i = 0; i < UNITS; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
            glGetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, presentation_framebuffer);
        glViewport(0, 0, width, height);
        const bool RESULT = hyprimate::workspace::draw_rotation(m_program,
            m_vao,
            m_uniforms,
            src,
            dst,
            out,
            source_framebuffer,
            presentation_framebuffer,
            width,
            height,
            position,
            direction,
            gap,
            matching_wallpaper,
            source_wallpaper,
            destination_wallpaper);
        for (std::size_t i = 0; i < UNITS; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures[i]));
            glBindSampler(static_cast<GLuint>(i), static_cast<GLuint>(samplers[i]));
        }
        glActiveTexture(static_cast<GLenum>(active));
        glBindVertexArray(static_cast<GLuint>(vao));
        glUseProgram(static_cast<GLuint>(program));
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        for (std::size_t i = 0; i < CAPS.size(); ++i) {
            if (enabled[i] != GL_FALSE) {
                glEnable(CAPS[i]);
            }
        }
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_framebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_framebuffer));
        return RESULT;
    }

    GLuint program() {
        const GLuint PROGRAM_ID =
            gl_harness::build_program("#version 300 es\nvoid main(){gl_Position=vec4(0.0);}",
                "#version 300 es\nprecision mediump float; out vec4 c; void main(){c=vec4(1.0);}");
        CHECK(PROGRAM_ID != 0);
        return PROGRAM_ID;
    }

    struct State {
        std::array<GLint, 13> values{};
        std::array<GLint, 4> viewport{};
        std::array<GLboolean, 4> mask{};
        std::array<GLboolean, 5> enabled{};
        bool operator==(const State &) const = default;
    };

    State state() {
        State current;
        const std::array<GLenum, 13> NAMES{GL_READ_FRAMEBUFFER_BINDING,
            GL_DRAW_FRAMEBUFFER_BINDING,
            GL_CURRENT_PROGRAM,
            GL_VERTEX_ARRAY_BINDING,
            GL_ARRAY_BUFFER_BINDING,
            GL_ACTIVE_TEXTURE,
            GL_TEXTURE_BINDING_2D,
            GL_BLEND_SRC_RGB,
            GL_BLEND_DST_RGB,
            GL_BLEND_SRC_ALPHA,
            GL_BLEND_DST_ALPHA,
            GL_BLEND_EQUATION_RGB,
            GL_BLEND_EQUATION_ALPHA};
        for (std::size_t i = 0; i < NAMES.size(); ++i) {
            glGetIntegerv(NAMES[i], &current.values[i]);
        }
        glGetIntegerv(GL_VIEWPORT, current.viewport.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, current.mask.data());
        const std::array<GLenum, 5> CAPS{
            GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND};
        for (std::size_t i = 0; i < CAPS.size(); ++i) {
            current.enabled[i] = glIsEnabled(CAPS[i]);
        }
        return current;
    }

    using Pixels = std::vector<unsigned char>;
    using hyprimate::workspace::GapFillStyle;

    void populate(Pixels &source, Pixels &dest, Pixels &wall_a, Pixels &wall_b) {
        for (int row = 0; row < HEIGHT; ++row) {
            for (int col = 0; col < WIDTH; ++col) {
                const auto INDEX =
                    ((static_cast<std::size_t>(row) * WIDTH) + static_cast<std::size_t>(col)) * 4;
                const bool WINDOW = col > 25 && col < 70 && row > 15 && row < 50;
                wall_a[INDEX] = static_cast<unsigned char>(((col / 4) % 2 != 0) ? 180 : 20);
                wall_a[INDEX + 1] = 40;
                wall_a[INDEX + 2] = 20;
                wall_a[INDEX + 3] = 255;
                wall_b[INDEX] = 20;
                wall_b[INDEX + 1] = static_cast<unsigned char>(((col / 4) % 2 != 0) ? 180 : 20);
                wall_b[INDEX + 2] = 40;
                wall_b[INDEX + 3] = 255;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const unsigned char WINDOW_COLOR = channel == 2 ? 240 : 0;
                    source[INDEX + channel] = WINDOW ? WINDOW_COLOR : wall_a[INDEX + channel];
                    dest[INDEX + channel] = WINDOW ? WINDOW_COLOR : wall_b[INDEX + channel];
                }
                source[INDEX + 3] = dest[INDEX + 3] = WINDOW ? 255 : 0;
            }
        }
    }

    template <typename Render>
    void check_geometry(const Render &render,
        Direction direction,
        GapFill &gap,
        const Pixels &source,
        const Pixels &dest,
        double position) {
        gap.style = GapFillStyle::Background;
        gap.color = {0.25F, 0.0F, 0.0F, 0.5F};
        const auto FRAME = hyprimate::workspace::rotation::make_frame(
            {.width = WIDTH, .height = HEIGHT}, position, direction);
        const auto IMAGE = render(position, direction, gap, false);
        const auto PROJECT = [](const auto &matrix, double xpos, double ypos) {
            return std::array{(matrix[0] * xpos) + (matrix[3] * ypos) + matrix[6],
                (matrix[1] * xpos) + (matrix[4] * ypos) + matrix[7]};
        };
        const auto INSIDE = [](const auto &point) {
            return point[0] >= 0 && point[0] < WIDTH && point[1] >= 0 && point[1] < HEIGHT;
        };
        for (int row = 0; row < HEIGHT; ++row) {
            for (int col = 0; col < WIDTH; ++col) {
                const auto SOURCE_POINT = PROJECT(FRAME.source, col + 0.5, row + 0.5);
                const auto DEST_POINT = PROJECT(FRAME.destination, col + 0.5, row + 0.5);
                if (!INSIDE(SOURCE_POINT) && !INSIDE(DEST_POINT)) {
                    continue;
                }
                const bool FIRST = INSIDE(SOURCE_POINT);
                const auto &point = FIRST ? SOURCE_POINT : DEST_POINT;
                // Nearest-filter fixture avoids conflating geometric placement with filtering.
                const auto OFFSET = ((static_cast<std::size_t>(point[1]) * WIDTH) +
                                        static_cast<std::size_t>(point[0])) *
                                    4;
                const auto OUTPUT =
                    ((static_cast<std::size_t>(row) * WIDTH) + static_cast<std::size_t>(col)) * 4;
                const auto &expected = FIRST ? source : dest;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    CHECK(IMAGE[OUTPUT + channel] == expected[OFFSET + channel]);
                }
            }
        }
        for (const auto STYLE :
            {GapFillStyle::Morph, GapFillStyle::Wallpaper, GapFillStyle::Still}) {
            gap.style = STYLE;
            const auto STYLED = render(position, direction, gap, false);
            for (std::size_t i = 0; i < IMAGE.size(); i += 4) {
                CHECK((STYLED[i + 2] > 220) == (IMAGE[i + 2] > 220));
            }
        }
    }

    template <typename Render>
    void check_direction(
        const Render &render, Direction direction, const Pixels &source, const Pixels &dest) {
        GapFill gap;
        auto start = render(0, direction, gap, false);
        auto end = render(1, direction, gap, false);
        for (std::size_t i = 0; i < start.size(); i += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                CHECK(start[i + channel] == source[i + channel]);
                CHECK(end[i + channel] == dest[i + channel]);
            }
            CHECK(start[i + 3] == 255 && end[i + 3] == 255);
        }
        for (const double POSITION : {0.1, 0.25, 0.5, 0.75, 0.9}) {
            check_geometry(render, direction, gap, source, dest, POSITION);
        }
        for (const auto STYLE : {GapFillStyle::Morph,
                 GapFillStyle::Background,
                 GapFillStyle::Wallpaper,
                 GapFillStyle::Still}) {
            gap.style = STYLE;
            hyprimate::workspace::Transition motion;
            CHECK(motion.begin(direction, .8, [](double time) { return time; }));
            motion.advance(.2);
            const auto INTERRUPTED = render(motion.progress(), direction, gap, false);
            motion.retarget(false, .8, motion.motion());
            CHECK(render(motion.progress(), motion.direction(), gap, false) == INTERRUPTED);
            motion.advance(.012);
            const auto BRAKING = render(motion.progress(), motion.direction(), gap, false);
            CHECK(BRAKING != INTERRUPTED && BRAKING != end);
            motion.advance(.189);
            CHECK(!motion.active());
            CHECK(render(motion.progress(), motion.direction(), gap, false) == start);
            CHECK(motion.begin(direction, .8, [](double time) { return time; }));
            motion.advance(.0048);
            motion.retarget(false, .8, motion.motion());
            motion.advance(1.0 / 240.0);
            CHECK(motion.active());  // At 240 Hz a 4.8 ms segment straddles two frames.
            motion.advance(1.0 / 240.0);
            CHECK(!motion.active() &&
                  render(motion.progress(), motion.direction(), gap, false) == start);
        }
        std::array<Pixels, 4> results;
        for (int style = 0; style < 4; ++style) {
            gap.style = static_cast<GapFillStyle>(style);
            gap.color = {0.25F, 0.0F, 0.0F, 0.5F};
            results[style] = render(0.5, direction, gap, false);
        }
        int windows = 0;
        int gaps = 0;
        for (std::size_t i = 0; i < source.size(); i += 4) {
            if (results[0][i + 2] > 220) {
                ++windows;
                for (int style = 1; style < 4; ++style) {
                    CHECK(results[style][i + 2] > 220);
                }
            }
            if (results[1][i + 3] < 255) {
                ++gaps;
                CHECK(std::abs(static_cast<int>(results[1][i]) - 64) <= 1);
                CHECK(std::abs(static_cast<int>(results[1][i + 3]) - 128) <= 1);
            }
        }
        CHECK(windows > 0);
        CHECK(gaps > 0);
        gap.style = GapFillStyle::Wallpaper;
        gap.blur = 16;
        auto blurred = render(0.5, direction, gap, false);
        CHECK(blurred != results[2]);
        for (std::size_t i = 0; i < source.size(); i += 4) {
            if (results[2][i + 2] > 220) {
                CHECK(blurred[i + 2] == results[2][i + 2]);
            }
        }
        gap.style = GapFillStyle::Still;
        auto same = render(0.5, direction, gap, true);
        CHECK(same != results[3]);
        for (std::size_t i = 0; i < source.size(); i += 4) {
            if (results[3][i + 2] > 220) {
                CHECK(same[i + 2] == results[3][i + 2]);
            }
        }
    }

    void run() {
        Pixels source(static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
        Pixels dest(source.size());
        Pixels wall_a(source.size());
        Pixels wall_b(source.size());
        const Pixels BLANK(source.size());
        populate(source, dest, wall_a, wall_b);
        std::array<GLuint, 5> tex{
            texture(source), texture(dest), texture(BLANK), texture(wall_a), texture(wall_b)};
        std::array<GLuint, 3> framebuffers{
            framebuffer(tex[0]), framebuffer(tex[1]), framebuffer(tex[2])};
        TestRenderer renderer;
        CHECK(renderer.initialize());
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        const GLuint PROGRAM_ID = program();
        glUseProgram(PROGRAM_ID);
        glBindVertexArray(vao);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[0]);
        for (int unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex[unit]);
        }
        glViewport(3, 4, 17, 19);
        glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        const State BEFORE = state();
        const auto RENDER = [&](double position, Direction direction, GapFill gap, bool matching) {
            CHECK(renderer.draw_raw(tex[0],
                tex[1],
                tex[2],
                framebuffers[0],
                framebuffers[2],
                WIDTH,
                HEIGHT,
                position,
                direction,
                gap,
                matching,
                tex[3],
                tex[4]));
            CHECK(state() == BEFORE);
            for (int unit = 0; unit < 4; ++unit) {
                glActiveTexture(GL_TEXTURE0 + unit);
                GLint binding = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                CHECK(std::cmp_equal(static_cast<GLuint>(binding), tex[unit]));
            }
            glActiveTexture(GL_TEXTURE3);
            std::vector<unsigned char> pixels(source.size());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[2]);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
            return pixels;
        };
        for (Direction direction : {Direction::Forward, Direction::Backward}) {
            check_direction(RENDER, direction, source, dest);
        }
        renderer.release();
        glDeleteProgram(PROGRAM_ID);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()), framebuffers.data());
        glDeleteTextures(static_cast<GLsizei>(tex.size()), tex.data());
    }
}  // namespace

int main() {
    const gl_harness::CContext CONTEXT{WIDTH, HEIGHT};
    if (!CONTEXT.valid()) {
        return 1;
    }
    run();
    return g_failures == 0 ? 0 : 1;
}
