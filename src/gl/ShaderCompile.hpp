#pragma once

#include <GLES3/gl3.h>

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

namespace hyprimate::gl {
    inline std::string shader_log(GLuint name, bool program) {
        GLint size = 0;
        if (program) {
            glGetProgramiv(name, GL_INFO_LOG_LENGTH, &size);
        } else {
            glGetShaderiv(name, GL_INFO_LOG_LENGTH, &size);
        }
        std::string log(static_cast<std::size_t>(std::clamp(size, 1, 64 * 1024)), '\0');
        GLsizei length = 0;
        if (program) {
            glGetProgramInfoLog(name, static_cast<GLsizei>(log.size()), &length, log.data());
        } else {
            glGetShaderInfoLog(name, static_cast<GLsizei>(log.size()), &length, log.data());
        }
        log.resize(static_cast<std::size_t>(length));
        return log;
    }

    inline GLuint compile_shader(GLenum stage, std::string_view source, std::string& error) {
        constexpr GLenum compute_shader = 0x91B9;
        const std::string_view label = stage == GL_VERTEX_SHADER ? "vertex" :
            stage == GL_FRAGMENT_SHADER ? "fragment" : stage == compute_shader ? "compute" : "shader";
        if (source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) {
            error = std::string(label) + ": source too large";
            return 0;
        }
        const auto shader = glCreateShader(stage);
        if (!shader) {
            error = std::string(label) + ": cannot create shader";
            return 0;
        }
        const auto* body = source.data();
        const auto bytes = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &body, &bytes);
        glCompileShader(shader);
        GLint status = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == GL_TRUE) return shader;
        error = std::string(label) + ": " + shader_log(shader, false);
        glDeleteShader(shader);
        return 0;
    }
}
