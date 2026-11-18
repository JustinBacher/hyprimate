#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "Check.hpp"
#include "workspace/rotation/RotationRenderer.hpp"
#include "workspace/rotation/RotationGeometry.hpp"

namespace {
    constexpr int WIDTH = 96, HEIGHT = 64;
    using hyprimate::workspace::Direction;
    using hyprimate::workspace::RotationRenderer;

    GLuint texture(const std::vector<unsigned char> &pixels) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return id;
    }

    GLuint framebuffer(GLuint tex) {
        GLuint id = 0;
        glGenFramebuffers(1, &id);
        glBindFramebuffer(GL_FRAMEBUFFER, id);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        return id;
    }

    GLuint program() {
        const char *vs = "#version 300 es\nvoid main(){gl_Position=vec4(0.0);}";
        const char *fs =
            "#version 300 es\nprecision mediump float; out vec4 c; void main(){c=vec4(1.0);}";
        GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &vs, nullptr);
        glShaderSource(f, 1, &fs, nullptr);
        glCompileShader(v);
        glCompileShader(f);
        GLuint p = glCreateProgram();
        glAttachShader(p, v);
        glAttachShader(p, f);
        glLinkProgram(p);
        GLint linked = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &linked);
        CHECK(linked != 0);
        glDeleteShader(v);
        glDeleteShader(f);
        return p;
    }

    struct State {
        std::array<GLint, 13> values{};
        std::array<GLint, 4> viewport{};
        std::array<GLboolean, 4> mask{};
        std::array<GLboolean, 5> enabled{};
        bool operator==(const State &) const = default;
    };

    State state() {
        State s;
        const std::array<GLenum, 13> names{GL_READ_FRAMEBUFFER_BINDING,
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
        for (std::size_t i = 0; i < names.size(); ++i) glGetIntegerv(names[i], &s.values[i]);
        glGetIntegerv(GL_VIEWPORT, s.viewport.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, s.mask.data());
        const std::array<GLenum, 5> caps{
            GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND};
        for (std::size_t i = 0; i < caps.size(); ++i) s.enabled[i] = glIsEnabled(caps[i]);
        return s;
    }

    void run() {
        using hyprimate::workspace::GapFill;
        using hyprimate::workspace::GapFillStyle;
        std::vector<unsigned char> source(WIDTH * HEIGHT * 4), dest(source.size());
        std::vector<unsigned char> wallA(source.size()), wallB(source.size()), blank(source.size());
        for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
            const auto i = static_cast<std::size_t>((y * WIDTH + x) * 4);
            const bool window = x > 25 && x < 70 && y > 15 && y < 50;
            wallA[i] = static_cast<unsigned char>((x / 4 % 2) ? 180 : 20);
            wallA[i + 1] = 40;
            wallA[i + 2] = 20;
            wallA[i + 3] = 255;
            wallB[i] = 20;
            wallB[i + 1] = static_cast<unsigned char>((x / 4 % 2) ? 180 : 20);
            wallB[i + 2] = 40;
            wallB[i + 3] = 255;
            for (int c = 0; c < 3; ++c) {
                source[i + c] = window ? (c == 2 ? 240 : 0) : wallA[i + c];
                dest[i + c] = window ? (c == 2 ? 240 : 0) : wallB[i + c];
            }
            source[i + 3] = dest[i + 3] = window ? 255 : 0;
        }
        std::array<GLuint, 5> tex{texture(source), texture(dest), texture(blank), texture(wallA), texture(wallB)};
        std::array<GLuint, 3> fb{framebuffer(tex[0]), framebuffer(tex[1]), framebuffer(tex[2])};
        RotationRenderer renderer;
        CHECK(renderer.initialize());
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        GLuint p = program();
        glUseProgram(p);
        glBindVertexArray(vao);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[0]);
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
        const State before = state();
        auto render = [&](double t, Direction direction, GapFill gap, bool matching) {
            CHECK(renderer.drawRaw(tex[0], tex[1], tex[2], fb[0], fb[2], WIDTH, HEIGHT,
                t, direction, gap, matching, tex[3], tex[4]));
            CHECK(state() == before);
            for (int unit = 0; unit < 4; ++unit) {
                glActiveTexture(GL_TEXTURE0 + unit);
                GLint binding = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                CHECK(static_cast<GLuint>(binding) == tex[unit]);
            }
            glActiveTexture(GL_TEXTURE3);
            std::vector<unsigned char> pixels(source.size());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[2]);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
            return pixels;
        };
        for (Direction direction : {Direction::Forward, Direction::Backward}) {
            GapFill gap;
            auto start = render(0, direction, gap, false);
            auto end = render(1, direction, gap, false);
            for (std::size_t i = 0; i < start.size(); i += 4) {
                for (int c = 0; c < 3; ++c) {
                    CHECK(start[i + c] == source[i + c]);
                    CHECK(end[i + c] == dest[i + c]);
                }
                CHECK(start[i + 3] == 255 && end[i + 3] == 255);
            }
            for (double t : {0.1, 0.25, 0.5, 0.75, 0.9}) {
                gap.style = GapFillStyle::Background;
                gap.color = {0.25F, 0.0F, 0.0F, 0.5F};
                const auto frame = hyprimate::workspace::rotation::make_frame(WIDTH, HEIGHT, t, direction);
                const auto image = render(t, direction, gap, false);
                const auto project = [](const auto &m, double x, double y) {
                    return std::array{m[0] * x + m[3] * y + m[6],
                        m[1] * x + m[4] * y + m[7]};
                };
                for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
                    const auto a = project(frame.source, x + 0.5, y + 0.5);
                    const auto b = project(frame.destination, x + 0.5, y + 0.5);
                    const auto inside = [](const auto &p) {
                        return p[0] >= 0 && p[0] < WIDTH && p[1] >= 0 && p[1] < HEIGHT;
                    };
                    if (!inside(a) && !inside(b)) continue;
                    const bool first = inside(a);
                    const auto &p = first ? a : b;
                    // Nearest-filter fixture avoids conflating geometric placement with filtering.
                    const auto at = static_cast<std::size_t>((static_cast<int>(p[1]) * WIDTH +
                        static_cast<int>(p[0])) * 4);
                    const auto out = static_cast<std::size_t>((y * WIDTH + x) * 4);
                    const auto &expected = first ? source : dest;
                    for (int c = 0; c < 3; ++c) CHECK(image[out + c] == expected[at + c]);
                }
                for (auto style : {GapFillStyle::Morph, GapFillStyle::Wallpaper, GapFillStyle::Still}) {
                    gap.style = style;
                    const auto styled = render(t, direction, gap, false);
                    for (std::size_t i = 0; i < image.size(); i += 4)
                        CHECK((styled[i + 2] > 220) == (image[i + 2] > 220));
                }
            }
            std::array<std::vector<unsigned char>, 4> results;
            for (int style = 0; style < 4; ++style) {
                gap.style = static_cast<GapFillStyle>(style);
                gap.color = {0.25F, 0.0F, 0.0F, 0.5F};
                results[style] = render(0.5, direction, gap, false);
            }
            int windows = 0, gaps = 0;
            for (std::size_t i = 0; i < source.size(); i += 4) {
                if (results[0][i + 2] > 220) {
                    ++windows;
                    for (int style = 1; style < 4; ++style) CHECK(results[style][i + 2] > 220);
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
            for (std::size_t i = 0; i < source.size(); i += 4)
                if (results[2][i + 2] > 220) CHECK(blurred[i + 2] == results[2][i + 2]);
            gap.style = GapFillStyle::Still;
            auto same = render(0.5, direction, gap, true);
            CHECK(same != results[3]);
            for (std::size_t i = 0; i < source.size(); i += 4)
                if (results[3][i + 2] > 220) CHECK(same[i + 2] == results[3][i + 2]);
        }
        renderer.release();
        glDeleteProgram(p);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(static_cast<GLsizei>(fb.size()), fb.data());
        glDeleteTextures(static_cast<GLsizei>(tex.size()), tex.data());
    }
}  // namespace

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        std::fprintf(stderr, "Cannot initialize offscreen EGL (try EGL_PLATFORM=surfaceless)\n");
        return 1;
    }
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const std::array<EGLint, 13> attrs{EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display, attrs.data(), &config, 1, &count) || count != 1) return 1;
    const std::array<EGLint, 5> surfaceAttrs{EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
    const std::array<EGLint, 3> contextAttrs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs.data());
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs.data());
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
        return 1;
    run();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return g_failures == 0 ? 0 : 1;
}
