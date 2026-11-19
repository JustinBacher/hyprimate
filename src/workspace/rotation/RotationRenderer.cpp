#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "workspace/rotation/RotationRenderer.hpp"
#include "workspace/rotation/RotationGeometry.hpp"
#ifndef HYPRIMATE_ROTATION_STANDALONE
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#endif

namespace hyprimate::workspace {
    namespace {
        constexpr char VERTEX_SHADER[] = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
        constexpr char FRAGMENT_SHADER[] = R"(#version 300 es
precision highp float;
uniform sampler2D sourceImage, destinationImage, sourceWall, destinationWall;
uniform mat3 sourceInverse, destinationInverse;
uniform vec2 size;
uniform float progress, direction, blurRadius;
uniform int style, matching;
uniform vec4 gapColor;
out vec4 color;
vec2 uv(vec2 p) { return p / size; }
bool inside(vec2 p) { return all(greaterThanEqual(p, vec2(0.0))) && all(lessThanEqual(p, size)); }
vec4 wall(sampler2D image, vec2 p, float radius) {
    vec2 q = clamp(uv(p), vec2(0.0), vec2(1.0));
    if (radius <= 0.0) return texture(image, q);
    vec4 sum = texture(image, q) * 4.0;
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
        if (x == 0 && y == 0) continue;
        vec2 offset = vec2(float(x), float(y)) * radius / (2.0 * size);
        sum += texture(image, clamp(q + offset, vec2(0.0), vec2(1.0)));
    }
    return sum / 28.0;
}
void main() {
    // Hyprland's offscreen textures and final TexPass use framebuffer Y as screen Y.
    vec2 p = gl_FragCoord.xy;
    vec2 a = (sourceInverse * vec3(p, 1.0)).xy;
    vec2 b = (destinationInverse * vec3(p, 1.0)).xy;
    bool inA = inside(a), inB = inside(b);
    if (inA || inB) {
        bool useB = inB && !inA;
        vec2 local = useB ? b : a;
        vec4 scene = useB ? texture(destinationImage, uv(local)) : texture(sourceImage, uv(local));
        if (style == 3) {
            vec3 originalWall = useB ? texture(destinationWall, uv(local)).rgb : texture(sourceWall, uv(local)).rgb;
            vec3 foreground = max(scene.rgb - originalWall * (1.0 - scene.a), vec3(0.0));
            vec3 background = texture(sourceWall, uv(p)).rgb;
            if (useB && matching == 0) background = originalWall;
            color = vec4(foreground + background * (1.0 - scene.a), 1.0);
        } else color = vec4(scene.rgb, 1.0);
        return;
    }
    if (style == 1) { color = gapColor; return; }
    if (style == 3) { color = vec4(texture(sourceWall, uv(p)).rgb, 1.0); return; }
    // Distances to the actual rectangular boundaries include the top/bottom exposed
    // regions, not only the wedge between the two facing vertical edges.
    vec2 edgeA = clamp(a, vec2(0.0), size);
    vec2 edgeB = clamp(b, vec2(0.0), size);
    float distanceA = length(a - edgeA), distanceB = length(b - edgeB);
    float across = distanceA / max(distanceA + distanceB, 0.0001);
    float mixAmount = smoothstep(0.0, 1.0, across);
    if (style == 0) {
        // Pull the incoming upper facing corner toward the outgoing boundary.
        // At either canonical boundary its wallpaper sample is unchanged.
        float incomingEdge = direction > 0.0 ? 0.0 : size.x;
        float taper = 4.0 * progress * (1.0 - progress);
        vec2 bridge = mix(edgeB, vec2(incomingEdge, 0.0), (1.0 - across) * taper);
        color = vec4(mix(wall(sourceWall, edgeA, 0.0).rgb,
            wall(destinationWall, bridge, 0.0).rgb, mixAmount), 1.0);
    } else {
        // Copies grow away from their original boundaries. Both enlargement and
        // blur vanish at the join, avoiding a hard edge on patterned wallpapers.
        float fadeA = smoothstep(0.0, size.x * 0.15, distanceA);
        float fadeB = smoothstep(0.0, size.x * 0.15, distanceB);
        vec2 ea = mix(edgeA, (a - size * 0.5) / 1.5 + size * 0.5, fadeA);
        vec2 eb = mix(edgeB, (b - size * 0.5) / 1.5 + size * 0.5, fadeB);
        color = vec4(mix(wall(sourceWall, ea, blurRadius * fadeA).rgb,
            wall(destinationWall, eb, blurRadius * fadeB).rgb, mixAmount), 1.0);
    }
})";
        GLuint compile(GLenum type, const char *source) {
            const GLuint shader = glCreateShader(type);
            if (!shader) return 0;
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                std::array<char, 2048> log{};
                glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
                (void)std::fprintf(stderr, "workspace rotation shader: %s\n", log.data());
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }
    }
    bool RotationRenderer::initialize() {
        if (m_program) return true;
        const GLuint vertex = compile(GL_VERTEX_SHADER, VERTEX_SHADER);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
        if (!vertex || !fragment) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return false;
        }
        m_program = glCreateProgram();
        if (m_program) {
            glAttachShader(m_program, vertex);
            glAttachShader(m_program, fragment);
            glLinkProgram(m_program);
        }
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        if (m_program) glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (!linked) { release(); return false; }
        const std::array<const char *, 13> names{"sourceImage", "destinationImage", "sourceWall",
            "destinationWall", "sourceInverse", "destinationInverse", "size", "progress", "direction",
            "blurRadius", "style", "matching", "gapColor"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            m_uniforms[i] = glGetUniformLocation(m_program, names[i]);
            if (m_uniforms[i] < 0) { release(); return false; }
        }
        glGenVertexArrays(1, &m_vao);
        if (!m_vao) { release(); return false; }
        return true;
    }
    void RotationRenderer::release() {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_program) glDeleteProgram(m_program);
        m_vao = 0;
        m_program = 0;
    }
#ifndef HYPRIMATE_ROTATION_STANDALONE
    bool RotationRenderer::draw(Render::CRenderContext &ctx, const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &destination, const SP<Render::IFramebuffer> &presentation,
        const SP<Render::IFramebuffer> &sourceWallpaper, const SP<Render::IFramebuffer> &destinationWallpaper,
        double position, Direction direction, const GapFill &gap, bool matchingWallpaper) {
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !source || !destination ||
            !presentation || !sourceWallpaper || !destinationWallpaper || !source->getTexture() ||
            !destination->getTexture() || !presentation->getTexture() || !sourceWallpaper->getTexture() ||
            !destinationWallpaper->getTexture() || source->m_size != destination->m_size ||
            source->m_size != presentation->m_size || source->m_size != sourceWallpaper->m_size ||
            source->m_size != destinationWallpaper->m_size) return false;
        (void)ctx;
        return drawRaw(source->getTexture()->m_texID, destination->getTexture()->m_texID,
            presentation->getTexture()->m_texID,
            static_cast<Render::GL::CGLFramebuffer *>(source.get())->getFBID(),
            static_cast<Render::GL::CGLFramebuffer *>(presentation.get())->getFBID(),
            static_cast<int>(source->m_size.x), static_cast<int>(source->m_size.y), position, direction,
            gap, matchingWallpaper, sourceWallpaper->getTexture()->m_texID, destinationWallpaper->getTexture()->m_texID);
    }
#endif
    bool RotationRenderer::drawRaw(GLuint src, GLuint dst, GLuint out, GLuint sourceFramebuffer,
        GLuint presentationFramebuffer, int width, int height, double position, Direction direction,
        const GapFill &gap, bool matchingWallpaper, GLuint sourceWallpaper, GLuint destinationWallpaper) {
        if (!m_program || !src || !dst || !out || !sourceWallpaper || !destinationWallpaper ||
            src == dst || src == out || dst == out || out == sourceWallpaper || out == destinationWallpaper ||
            !sourceFramebuffer || !presentationFramebuffer || sourceFramebuffer == presentationFramebuffer ||
            width <= 0 || height <= 0 || !std::isfinite(position) || position < 0 || position > 1) return false;
        const auto frame = rotation::make_frame(width, height, position, direction);
        GLint readFramebuffer = 0, drawFramebuffer = 0, program = 0, vao = 0, active = 0;
        std::array<GLint, 4> textures{}, samplers{}, viewport{};
        std::array<GLboolean, 4> mask{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
        const std::array<GLenum, 5> caps{GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND};
        std::array<GLboolean, 5> enabled{};
        for (std::size_t i = 0; i < caps.size(); ++i) { enabled[i] = glIsEnabled(caps[i]); glDisable(caps[i]); }
        const std::array<GLuint, 4> inputs{src, dst, sourceWallpaper, destinationWallpaper};
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
            glGetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
            glBindSampler(static_cast<GLuint>(i), 0);
            glBindTexture(GL_TEXTURE_2D, inputs[i]);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, presentationFramebuffer);
        glViewport(0, 0, width, height);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(m_program);
        for (std::size_t i = 0; i < 4; ++i) glUniform1i(m_uniforms[i], static_cast<GLint>(i));
        glUniformMatrix3fv(m_uniforms[4], 1, GL_FALSE, frame.source.data());
        glUniformMatrix3fv(m_uniforms[5], 1, GL_FALSE, frame.destination.data());
        glUniform2f(m_uniforms[6], static_cast<float>(width), static_cast<float>(height));
        glUniform1f(m_uniforms[7], static_cast<float>(position));
        glUniform1f(m_uniforms[8], direction == Direction::Forward ? 1.0F : -1.0F);
        glUniform1f(m_uniforms[9], std::isfinite(gap.blur) ? std::clamp(gap.blur, 0.0F, 64.0F) : 0.0F);
        glUniform1i(m_uniforms[10], static_cast<int>(gap.style));
        glUniform1i(m_uniforms[11], matchingWallpaper ? 1 : 0);
        glUniform4fv(m_uniforms[12], 1, gap.color.data());
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures[i]));
            glBindSampler(static_cast<GLuint>(i), static_cast<GLuint>(samplers[i]));
        }
        glActiveTexture(static_cast<GLenum>(active));
        glBindVertexArray(static_cast<GLuint>(vao));
        glUseProgram(static_cast<GLuint>(program));
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        for (std::size_t i = 0; i < caps.size(); ++i) if (enabled[i]) glEnable(caps[i]);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
        return true;
    }
}
