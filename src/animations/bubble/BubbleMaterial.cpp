#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleOptics.hpp"

namespace {
#include "bubble_shaders_generated.hpp"

    GLuint program(const char *vertex, const char *fragment) {
        const auto compile = [](GLenum type, const char *source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048]{};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::fprintf(stderr, "bubble shader: %s\n", log);
                glDeleteShader(shader);
                return GLuint{0};
            }
            return shader;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
        GLuint result = 0;
        if (vs && fs) {
            result = glCreateProgram();
            glAttachShader(result, vs);
            glAttachShader(result, fs);
            glLinkProgram(result);
            GLint ok = 0;
            glGetProgramiv(result, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048]{};
                glGetProgramInfoLog(result, sizeof(log), nullptr, log);
                std::fprintf(stderr, "bubble program: %s\n", log);
                glDeleteProgram(result);
                result = 0;
            }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return result;
    }
    void vector(GLint location, hyprimate::Vec2 v) {
        glUniform2f(location, float(v.x), float(v.y));
    }
    void surface(const std::array<GLint, 37> &u, const hyprimate::BubbleSurface &s) {
        vector(u[0], s.halfSize);
        vector(u[1], s.core);
        glUniform1f(u[2], float(s.corner));
        glUniform1f(u[3], float(s.depth));
        glUniform1f(u[4], float(s.content));
        glUniform1f(u[5], float(s.film));
        glUniform1f(u[6], float(s.visibility));
        glUniform1f(u[7], float(s.ripple));
        glUniform1f(u[8], float(s.thickness));
        glUniform1f(u[9], float(s.flow));
        glUniform1f(u[10], float(s.aureole));
        glUniform1i(u[11], s.spherical);
        glUniform1i(u[12], int(std::min(s.holeCount, hyprimate::kBubbleHoles)));
        for (std::size_t i = 0; i < std::min(s.holeCount, hyprimate::kBubbleHoles); ++i) {
            const auto &h = s.holes[i];
            const auto *loc = u.data() + 13 + 6 * i;
            glUniform3f(loc[0], float(h.centre.x), float(h.centre.y), float(h.centre.z));
            glUniform1f(loc[1], float(h.radius));
            glUniform1f(loc[2], float(h.rim));
            glUniform1f(loc[3], float(h.precursor));
            glUniform4f(loc[4],
                float(h.amplitude[0]),
                float(h.amplitude[1]),
                float(h.amplitude[2]),
                float(h.amplitude[3]));
            glUniform4f(
                loc[5], float(h.phase[0]), float(h.phase[1]), float(h.phase[2]), float(h.phase[3]));
        }
    }
    struct Instance {
        float values[12];
    };
}  // namespace

bool CBubbleMaterial::initialize() {
    if (m_tried)
        return m_membrane && m_curl && m_particles && m_vao && m_particleVao && m_instance && m_lut;
    m_tried = true;
    m_membrane = program(bubble_shader::VERTEX, bubble_shader::FRAGMENT);
    m_curl = program(bubble_shader::CURL_VERTEX, bubble_shader::CURL_FRAGMENT);
    m_particles = program(bubble_shader::PARTICLE_VERTEX, bubble_shader::PARTICLE_FRAGMENT);
    if (!m_membrane || !m_curl || !m_particles) {
        release();
        m_tried = true;
        return false;
    }
    constexpr const char *membraneNames[] = {"size",
        "sourcePos",
        "sourceSize",
        "outputOrigin",
        "textureOrigin",
        "imageSize",
        "wallpaperOrigin",
        "wallpaperX",
        "wallpaperY",
        "wallpaperPixel",
        "wallpaperReady",
        "image",
        "wallpaperImage",
        "filmLut"};
    constexpr const char *particleNames[] = {
        "canvas", "origin", "sourcePos", "sourceSize", "size", "onGlass", "filmLut"};
    constexpr const char *curlNames[] = {"size",
        "canvas",
        "origin",
        "sourcePos",
        "sourceSize",
        "textureOrigin",
        "imageSize",
        "wallpaperOrigin",
        "wallpaperX",
        "wallpaperY",
        "wallpaperPixel",
        "wallpaperReady",
        "image",
        "wallpaperImage",
        "filmLut"};
    constexpr const char *surfaceNames[] = {"windowSurface", "shellSurface"};
    constexpr const char *fields[] = {"halfSize",
        "core",
        "corner",
        "depth",
        "content",
        "film",
        "visibility",
        "ripple",
        "thickness",
        "flow",
        "aureole",
        "spherical",
        "holeCount"};
    constexpr const char *holeFields[] = {
        "centre", "radius", "rim", "precursor", "amplitude", "phase"};
    for (std::size_t i = 0; i < m_membraneUniforms.size(); ++i)
        m_membraneUniforms[i] = glGetUniformLocation(m_membrane, membraneNames[i]);
    for (std::size_t i = 0; i < m_particleUniforms.size(); ++i)
        m_particleUniforms[i] = glGetUniformLocation(m_particles, particleNames[i]);
    for (std::size_t i = 0; i < m_curlUniforms.size(); ++i)
        m_curlUniforms[i] = glGetUniformLocation(m_curl, curlNames[i]);
    for (std::size_t s = 0; s < m_surfaceUniforms.size(); ++s) {
        char name[80];
        for (std::size_t i = 0; i < std::size(fields); ++i) {
            std::snprintf(name, sizeof(name), "%s.%s", surfaceNames[s], fields[i]);
            m_surfaceUniforms[s][i] = glGetUniformLocation(m_membrane, name);
        }
        for (std::size_t h = 0; h < hyprimate::kBubbleHoles; ++h)
            for (std::size_t i = 0; i < std::size(holeFields); ++i) {
                std::snprintf(
                    name, sizeof(name), "%s.holes[%zu].%s", surfaceNames[s], h, holeFields[i]);
                m_surfaceUniforms[s][13 + h * 6 + i] = glGetUniformLocation(m_membrane, name);
            }
    }
    {
        char name[80];
        for (std::size_t i = 0; i < std::size(fields); ++i) {
            std::snprintf(name, sizeof(name), "sheet.%s", fields[i]);
            m_curlSurfaceUniforms[i] = glGetUniformLocation(m_curl, name);
        }
        for (std::size_t h = 0; h < hyprimate::kBubbleHoles; ++h)
            for (std::size_t i = 0; i < std::size(holeFields); ++i) {
                std::snprintf(name, sizeof(name), "sheet.holes[%zu].%s", h, holeFields[i]);
                m_curlSurfaceUniforms[13 + h * 6 + i] = glGetUniformLocation(m_curl, name);
            }
    }
    glGenVertexArrays(1, &m_vao);
    glGenVertexArrays(1, &m_particleVao);
    glGenBuffers(1, &m_instance);
    glBindVertexArray(m_particleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instance);
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Instance),
            reinterpret_cast<void *>(i * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
    const auto table = hyprimate::bubbleThinFilmTable();
    std::vector<float> rgba(std::size_t(table.thicknessSamples) * table.cosineSamples * 4, 1.0f);
    for (std::size_t i = 0; i < table.rgb.size() / 3; ++i)
        std::copy_n(table.rgb.data() + i * 3, 3, rgba.data() + i * 4);
    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &m_lut);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        table.thicknessSamples,
        table.cosineSamples,
        0,
        GL_RGBA,
        GL_FLOAT,
        rgba.data());
    return m_vao && m_particleVao && m_instance && m_lut;
}
void CBubbleMaterial::release() {
    if (m_membrane) glDeleteProgram(m_membrane);
    if (m_curl) glDeleteProgram(m_curl);
    if (m_particles) glDeleteProgram(m_particles);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_particleVao) glDeleteVertexArrays(1, &m_particleVao);
    if (m_instance) glDeleteBuffers(1, &m_instance);
    if (m_lut) glDeleteTextures(1, &m_lut);
    m_membrane = m_curl = m_particles = m_vao = m_particleVao = m_instance = m_lut = 0;
    m_tried = false;
}
void CBubbleMaterial::drawMembranes(
    const hyprimate::BubbleFrame &frame, const SBubbleTarget &t, const SBubbleImages &images) {
    if (!m_membrane) return;
    glUseProgram(m_membrane);
    glBindVertexArray(m_vao);
    const auto &u = m_membraneUniforms;
    vector(u[0], frame.size);
    vector(u[1], t.sourcePos);
    vector(u[2], t.sourceSize);
    vector(u[3], t.origin);
    vector(u[4], images.textureOrigin);
    vector(u[5], images.textureSize);
    vector(u[6], images.wallpaperOrigin);
    vector(u[7], images.wallpaperX);
    vector(u[8], images.wallpaperY);
    vector(u[9], images.wallpaperPixel);
    glUniform1i(u[10], images.wallpaper != 0);
    glUniform1i(u[11], 0);
    glUniform1i(u[12], 1);
    glUniform1i(u[13], 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, images.window);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, images.wallpaper);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    surface(m_surfaceUniforms[0], frame.window);
    surface(m_surfaceUniforms[1], frame.shell);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    const auto active = [](const hyprimate::BubbleSurface &s) {
        if (s.visibility <= 0 || s.aureole <= 0) return false;
        for (std::size_t i = 0; i < std::min(s.holeCount, hyprimate::kBubbleHoles); ++i)
            if (s.holes[i].radius > 0) return true;
        return false;
    };
    if (!active(frame.window) && !active(frame.shell)) return;
    GLint srcRgb, dstRgb, srcAlpha, dstAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    const GLboolean blending = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_curl);
    const auto &c = m_curlUniforms;
    vector(c[0], frame.size);
    vector(c[1], t.canvas);
    vector(c[2], t.origin);
    vector(c[3], t.sourcePos);
    vector(c[4], t.sourceSize);
    vector(c[5], images.textureOrigin);
    vector(c[6], images.textureSize);
    vector(c[7], images.wallpaperOrigin);
    vector(c[8], images.wallpaperX);
    vector(c[9], images.wallpaperY);
    vector(c[10], images.wallpaperPixel);
    glUniform1i(c[11], images.wallpaper != 0);
    glUniform1i(c[12], 0);
    glUniform1i(c[13], 1);
    glUniform1i(c[14], 2);
    for (const auto *s : {&frame.window, &frame.shell}) {
        if (!active(*s)) continue;
        surface(m_curlSurfaceUniforms, *s);
        glDrawArraysInstanced(
            GL_TRIANGLES, 0, 96 * 12 * 6, GLsizei(std::min(s->holeCount, hyprimate::kBubbleHoles)));
    }
    glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    if (!blending) glDisable(GL_BLEND);
}
void CBubbleMaterial::drawParticles(std::span<const hyprimate::BubbleParticle> particles,
    hyprimate::Vec2 size,
    const SBubbleTarget &t,
    bool onGlass) {
    if (!m_particles || particles.empty()) return;
    std::array<Instance, 2 * hyprimate::kBubbleMaxParticles> data;
    const auto count = std::min(particles.size(), data.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto &p = particles[i];
        data[i] = {{float(p.centre.x),
            float(p.centre.y),
            float(p.tail.x),
            float(p.tail.y),
            float(p.radius),
            float(p.tailRadius),
            float(p.depth),
            0,
            float(p.thickness),
            float(p.spin),
            float(p.blur),
            0}};
    }
    glUseProgram(m_particles);
    glBindVertexArray(m_particleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instance);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Instance), data.data());
    const auto &u = m_particleUniforms;
    vector(u[0], t.canvas);
    vector(u[1], t.origin);
    vector(u[2], t.sourcePos);
    vector(u[3], t.sourceSize);
    vector(u[4], size);
    glUniform1i(u[5], onGlass);
    glUniform1i(u[6], 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, GLsizei(count));
}
