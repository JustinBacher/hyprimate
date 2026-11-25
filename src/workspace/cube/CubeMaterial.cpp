#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>

#include "CubeMaterial.hpp"
#include "CubeMesh.hpp"
#include "gl/Storage.hpp"

namespace hyprimate::workspace::cube {
    namespace {
        constexpr const char *VERTEX_SHADER = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 shade;
layout(location = 3) in int chart;
out vec2 uv;
out vec3 tint;
flat out int face;
void main() {
    // Keep signed eye depth in w. Hardware clips the room walls against the view
    // pyramid before division; negative w cannot survive even with z set to zero.
    // No depth test or arbitrary near/far slices are needed for these convex meshes.
    gl_Position = vec4(position.xy, 0.0, position.z);
    uv = texcoord;
    tint = shade;
    face = chart;
}
)";

        constexpr const char *FRAGMENT_SHADER = R"(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D sourceImage;
uniform highp sampler2D destinationImage;
in vec2 uv;
in vec3 tint;
flat in int face;
out vec4 color;
void main() {
    vec3 rgb = tint;
    if (face == 0) {
        rgb *= textureLod(sourceImage, uv, 0.0).rgb;
    } else if (face == 1) {
        rgb *= textureLod(destinationImage, uv, 0.0).rgb;
    } else if (face == 2) {
        vec2 p = uv * vec2(0.88, 0.83) + vec2(0.14, 0.19);
        float light = exp(-dot(p, p));
        rgb = mix(vec3(0.048, 0.060, 0.075), vec3(0.119, 0.147, 0.173), light);
    }
    color = vec4(rgb, 1.0);
}
)";


        GLuint compile_shader(GLenum stage, const char *source) {
            const GLuint SHADER = glCreateShader(stage);
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
            (void)std::fprintf(stderr, "hyprimate: Cube shader: %s\n", log.data());
            glDeleteShader(SHADER);
            return 0;
        }

        GLuint make_program() {
            const GLuint VERTEX = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
            if (VERTEX == 0) {
                return 0;
            }
            const GLuint FRAGMENT = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
            const GLuint PROGRAM = FRAGMENT != 0 ? glCreateProgram() : 0;
            GLint linked = GL_FALSE;
            if (PROGRAM != 0) {
                glAttachShader(PROGRAM, VERTEX);
                glAttachShader(PROGRAM, FRAGMENT);
                glLinkProgram(PROGRAM);
                glGetProgramiv(PROGRAM, GL_LINK_STATUS, &linked);
                glDetachShader(PROGRAM, VERTEX);
                glDetachShader(PROGRAM, FRAGMENT);
            }
            glDeleteShader(VERTEX);
            if (FRAGMENT != 0) {
                glDeleteShader(FRAGMENT);
            }
            if (linked == GL_TRUE) {
                return PROGRAM;
            }
            if (PROGRAM != 0) {
                std::array<GLchar, 1024> log{};
                glGetProgramInfoLog(PROGRAM, static_cast<GLsizei>(log.size()), nullptr, log.data());
                (void)std::fprintf(stderr, "hyprimate: Cube program: %s\n", log.data());
                glDeleteProgram(PROGRAM);
            }
            return 0;
        }

        const void *attribute_offset(std::size_t offset) {
            // GLES interprets this pointer as a byte offset into the bound VBO.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
            return reinterpret_cast<const void *>(offset);
        }
    }  // namespace

    bool Material::initialize() {
        if (m_program != 0) {
            return true;
        }
        m_program = make_program();
        if (m_program == 0) {
            return false;
        }
        const GLint SOURCE = glGetUniformLocation(m_program, "sourceImage");
        const GLint DESTINATION = glGetUniformLocation(m_program, "destinationImage");
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_buffer);
        glGenSamplers(1, &m_sampler);
        if (SOURCE < 0 || DESTINATION < 0 || m_vao == 0 || m_buffer == 0 || m_sampler == 0) {
            release();
            return false;
        }
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        if (!gl::allocate_buffer(
                GL_ARRAY_BUFFER, sizeof(detail::Vertex) * detail::MAX_VERTICES, nullptr, GL_STREAM_DRAW)) {
            release();
            return false;
        }
        constexpr std::array<GLint, 3> SIZES{3, 2, 3};
        constexpr std::array<std::size_t, 3> OFFSETS{
            offsetof(detail::Vertex, clip), offsetof(detail::Vertex, uv), offsetof(detail::Vertex, tint)};
        for (GLuint i = 0; i < SIZES.size(); ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(
                i, SIZES[i], GL_FLOAT, GL_FALSE, sizeof(detail::Vertex), attribute_offset(OFFSETS[i]));
        }
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(
            3, 1, GL_INT, sizeof(detail::Vertex), attribute_offset(offsetof(detail::Vertex, chart)));
        glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glUseProgram(m_program);
        glUniform1i(SOURCE, 0);
        glUniform1i(DESTINATION, 1);
        return true;
    }

    void Material::release() {
        if (m_program == 0) {
            return;
        }
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_buffer);
        glDeleteSamplers(1, &m_sampler);
        glDeleteProgram(m_program);
        abandon();
    }

    void Material::abandon() {
        m_program = m_vao = m_buffer = m_sampler = 0;
    }

    bool Material::draw(GLuint source,
        GLuint destination,
        GLuint output_texture,
        int width,
        int height,
        double progress,
        Direction direction,
        const Settings &settings) {
        if (m_program == 0 || source == 0 || destination == 0 || output_texture == 0 ||
            source == output_texture || destination == output_texture || width <= 0 ||
            height <= 0 || !std::isfinite(progress) || progress < 0.0 || progress > 1.0) {
            return false;
        }
        const double TILT = std::isfinite(settings.tilt_degrees)
                                ? std::clamp(settings.tilt_degrees, -20.0F, 20.0F)
                                : -20.0F;
        const double ZOOM =
            std::isfinite(settings.zoom) ? std::clamp(settings.zoom, 0.0F, 1.0F) : 0.0F;
        const detail::Pose POSE{.progress = progress,
            .half_height = static_cast<double>(height) / width,
            .tilt = TILT * std::numbers::pi / 180.0,
            .zoom = ZOOM,
            .direction = direction};
        detail::Mesh mesh(direction == Direction::Backward);
        detail::build_mesh(mesh, POSE, settings.variant);
        if (mesh.count() == 0) {
            return false;
        }

        const std::array<GLuint, 2> INPUTS{source, destination};
        for (GLuint i = 0; i < INPUTS.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindSampler(i, m_sampler);
            glBindTexture(GL_TEXTURE_2D, INPUTS[i]);
        }
        glUseProgram(m_program);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, mesh.bytes(), mesh.data());
        glDrawArrays(GL_TRIANGLES, 0, mesh.count());
        return true;
    }
}  // namespace hyprimate::workspace::cube
