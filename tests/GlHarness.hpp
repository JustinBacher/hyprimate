#pragma once

// Minimal offscreen OpenGL ES harness for the GPU tests (ADR-0002). Run with
// EGL_PLATFORM=surfaceless: Mesa's offscreen platform needs no compositor. Production builds its
// programs with Hyprland's CShader; tests build the same embedded GLSL sources here and check the
// pixels they render.

#include <array>
#include <cstdio>
#include <string_view>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

namespace gl_harness {
    // A current GLES 3 context on a pbuffer. `valid()` is false when EGL could not provide one.
    class CContext {
      public:
        explicit CContext(EGLint width = 1920, EGLint height = 1080)
            : m_display(eglGetDisplay(EGL_DEFAULT_DISPLAY)) {
            EGLint major = 0;
            EGLint minor = 0;
            if (m_display == EGL_NO_DISPLAY ||
                eglInitialize(m_display, &major, &minor) == EGL_FALSE ||
                eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
                (void)std::fprintf(stderr, "Cannot initialize EGL; try EGL_PLATFORM=surfaceless\n");
                return;
            }
            const std::array<EGLint, 13> ATTRIBUTES{EGL_SURFACE_TYPE,
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
            if (eglChooseConfig(m_display, ATTRIBUTES.data(), &config, 1, &count) == EGL_FALSE ||
                count != 1) {
                return;
            }
            const std::array<EGLint, 5> SURFACE{EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
            const std::array<EGLint, 3> CONTEXT{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
            m_surface = eglCreatePbufferSurface(m_display, config, SURFACE.data());
            m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, CONTEXT.data());
            m_valid = m_surface != EGL_NO_SURFACE && m_context != EGL_NO_CONTEXT &&
                      eglMakeCurrent(m_display, m_surface, m_surface, m_context) != EGL_FALSE;
        }

        ~CContext() {
            if (m_display == EGL_NO_DISPLAY) {
                return;
            }
            eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_context != EGL_NO_CONTEXT) {
                eglDestroyContext(m_display, m_context);
            }
            if (m_surface != EGL_NO_SURFACE) {
                eglDestroySurface(m_display, m_surface);
            }
            eglTerminate(m_display);
        }

        CContext(const CContext &) = delete;
        CContext &operator=(const CContext &) = delete;
        CContext(CContext &&) = delete;
        CContext &operator=(CContext &&) = delete;

        [[nodiscard]] bool valid() const noexcept {
            return m_valid;
        }

      private:
        EGLDisplay m_display = EGL_NO_DISPLAY;
        EGLSurface m_surface = EGL_NO_SURFACE;
        EGLContext m_context = EGL_NO_CONTEXT;
        bool m_valid = false;
    };

    namespace detail {
        inline GLuint compile(GLenum type, std::string_view source) {
            const GLuint SHADER = glCreateShader(type);
            const char *text = source.data();
            const auto LENGTH = static_cast<GLint>(source.size());
            glShaderSource(SHADER, 1, &text, &LENGTH);
            glCompileShader(SHADER);
            GLint compiled = 0;
            glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compiled);
            if (compiled == 0) {
                std::array<char, 4096> log{};
                glGetShaderInfoLog(SHADER, log.size(), nullptr, log.data());
                (void)std::fprintf(stderr, "test shader: %s\n", log.data());
                glDeleteShader(SHADER);
                return 0;
            }
            return SHADER;
        }
    }  // namespace detail

    // Builds a program from the same sources production hands to CShader. Returns 0, having
    // logged why, when compiling or linking fails.
    inline GLuint build_program(std::string_view vertex, std::string_view fragment) {
        const GLuint VERTEX = detail::compile(GL_VERTEX_SHADER, vertex);
        const GLuint FRAGMENT = detail::compile(GL_FRAGMENT_SHADER, fragment);
        GLuint program = 0;
        if (VERTEX != 0 && FRAGMENT != 0) {
            program = glCreateProgram();
            glAttachShader(program, VERTEX);
            glAttachShader(program, FRAGMENT);
            glLinkProgram(program);
            GLint linked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked == 0) {
                std::array<char, 4096> log{};
                glGetProgramInfoLog(program, log.size(), nullptr, log.data());
                (void)std::fprintf(stderr, "test program: %s\n", log.data());
                glDeleteProgram(program);
                program = 0;
            }
        }
        if (VERTEX != 0) {
            glDeleteShader(VERTEX);
        }
        if (FRAGMENT != 0) {
            glDeleteShader(FRAGMENT);
        }
        return program;
    }
}  // namespace gl_harness
