#pragma once

#include <array>
#include <cstdio>

#include <GLES3/gl3.h>

namespace hyprimate::gl {
using ProgramDiagnostic = void (*)(const char *label, const char *stage, const char *message) noexcept;

namespace detail {
    inline void report_program_error(ProgramDiagnostic report, const char *label,
        const char *stage, const char *message) noexcept {
        if (report) report(label, stage, message);
        else (void)std::fprintf(stderr, "hyprimate: %s %s: %s\n", label, stage, message);
    }

    [[nodiscard]] inline GLuint compile_shader(
        GLenum type, const GLchar *source, const char *label, ProgramDiagnostic report) noexcept {
        const GLuint SHADER = glCreateShader(type);
        if (SHADER == 0) {
            return 0;
        }
        glShaderSource(SHADER, 1, &source, nullptr);
        glCompileShader(SHADER);
        GLint compiled = GL_FALSE;
        glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) {
            return SHADER;
        }

        std::array<GLchar, 1024> log{};
        glGetShaderInfoLog(SHADER, static_cast<GLsizei>(log.size()), nullptr, log.data());
        report_program_error(report, label, type == GL_VERTEX_SHADER ? "vertex shader" : "fragment shader",
            log.data());
        glDeleteShader(SHADER);
        return 0;
    }
}  // namespace detail

// Requires the owning GLES context current. Publish only a linked program; the caller
// owns it, and no shader stages survive a failed build or stay attached after success.
[[nodiscard]] inline GLuint build_program(const GLchar *vertex, const GLchar *fragment,
    const char *label, ProgramDiagnostic report = nullptr) noexcept {
    const GLuint VERTEX_SHADER = detail::compile_shader(GL_VERTEX_SHADER, vertex, label, report);
    if (VERTEX_SHADER == 0) {
        return 0;
    }
    const GLuint FRAGMENT_SHADER = detail::compile_shader(GL_FRAGMENT_SHADER, fragment, label, report);
    const GLuint PROGRAM = FRAGMENT_SHADER != 0 ? glCreateProgram() : 0;
    GLint linked = GL_FALSE;
    if (PROGRAM != 0) {
        glAttachShader(PROGRAM, VERTEX_SHADER);
        glAttachShader(PROGRAM, FRAGMENT_SHADER);
        glLinkProgram(PROGRAM);
        glGetProgramiv(PROGRAM, GL_LINK_STATUS, &linked);
        if (linked == GL_TRUE) {
            glDetachShader(PROGRAM, VERTEX_SHADER);
            glDetachShader(PROGRAM, FRAGMENT_SHADER);
        } else {
            std::array<GLchar, 1024> log{};
            glGetProgramInfoLog(PROGRAM, static_cast<GLsizei>(log.size()), nullptr, log.data());
            detail::report_program_error(report, label, "program", log.data());
            glDeleteProgram(PROGRAM);
        }
    }
    if (FRAGMENT_SHADER != 0) {
        glDeleteShader(FRAGMENT_SHADER);
    }
    glDeleteShader(VERTEX_SHADER);
    return linked == GL_TRUE ? PROGRAM : 0;
}
}  // namespace hyprimate::gl
