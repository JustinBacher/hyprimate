#include <algorithm>
#include <array>
#include <cmath>

#include "workspace/rotation/RotationRenderer.hpp"
#include "workspace/rotation/RotationGeometry.hpp"
#ifndef HYPRIMATE_ROTATION_RAW_ONLY
#include "gl/RawGlBracket.hpp"
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#endif

namespace hyprimate::workspace {
    bool rotationUniforms(GLuint program, RotationUniforms &uniforms) {
        const std::array<const char *, 13> names{"sourceImage", "destinationImage", "sourceWall",
            "destinationWall", "sourceInverse", "destinationInverse", "size", "progress", "direction",
            "blurRadius", "style", "matching", "gapColor"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            uniforms[i] = glGetUniformLocation(program, names[i]);
            if (uniforms[i] < 0) return false;
        }
        return true;
    }

    bool drawRotation(GLuint program, GLuint vao, const RotationUniforms &uniforms,
        GLuint src, GLuint dst, GLuint out, GLuint sourceFramebuffer, GLuint presentationFramebuffer,
        int width, int height, double position, Direction direction, const GapFill &gap,
        bool matchingWallpaper, GLuint sourceWallpaper, GLuint destinationWallpaper) {
        if (!program || !vao || !src || !dst || !out || !sourceWallpaper || !destinationWallpaper ||
            src == dst || src == out || dst == out || out == sourceWallpaper || out == destinationWallpaper ||
            !sourceFramebuffer || !presentationFramebuffer || sourceFramebuffer == presentationFramebuffer ||
            width <= 0 || height <= 0 || !std::isfinite(position) || position < 0 || position > 1) return false;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const std::array<GLuint, 4> inputs{src, dst, sourceWallpaper, destinationWallpaper};
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindSampler(static_cast<GLuint>(i), 0);
            glBindTexture(GL_TEXTURE_2D, inputs[i]);
        }
        glActiveTexture(GL_TEXTURE0);
        const auto frame = rotation::make_frame(width, height, position, direction);
        glUseProgram(program);
        for (std::size_t i = 0; i < inputs.size(); ++i) glUniform1i(uniforms[i], static_cast<GLint>(i));
        glUniformMatrix3fv(uniforms[4], 1, GL_FALSE, frame.source.data());
        glUniformMatrix3fv(uniforms[5], 1, GL_FALSE, frame.destination.data());
        glUniform2f(uniforms[6], static_cast<float>(width), static_cast<float>(height));
        glUniform1f(uniforms[7], static_cast<float>(position));
        glUniform1f(uniforms[8], direction == Direction::Forward ? 1.0F : -1.0F);
        glUniform1f(uniforms[9], std::isfinite(gap.blur) ? std::clamp(gap.blur, 0.0F, 64.0F) : 0.0F);
        glUniform1i(uniforms[10], static_cast<int>(gap.style));
        glUniform1i(uniforms[11], matchingWallpaper ? 1 : 0);
        glUniform4fv(uniforms[12], 1, gap.color.data());
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return true;
    }

#ifndef HYPRIMATE_ROTATION_RAW_ONLY
    RotationRenderer::RotationRenderer() = default;
    RotationRenderer::~RotationRenderer() = default;
    bool RotationRenderer::initialize() {
        if (m_program) return true;
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL) return false;
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> bracket{{}};
        m_shader = std::make_unique<CShader>();
        if (!m_shader->createProgram(VERTEX_SHADER, FRAGMENT_SHADER, true, true)) {
            m_shader.reset();
            return false;
        }
        m_program = m_shader->program();
        if (!rotationUniforms(m_program, m_uniforms)) { release(); return false; }
        glGenVertexArrays(1, &m_vao);
        if (!m_vao) { release(); return false; }
        return true;
    }
    void RotationRenderer::release() {
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL) {
            (void)m_shader.release();
            m_vao = 0;
            m_program = 0;
            return;
        }
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> bracket{{}};
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        m_shader.reset();
        m_vao = 0;
        m_program = 0;
    }
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
        const GLuint src = source->getTexture()->m_texID;
        const GLuint dst = destination->getTexture()->m_texID;
        const GLuint out = presentation->getTexture()->m_texID;
        const GLuint wallA = sourceWallpaper->getTexture()->m_texID;
        const GLuint wallB = destinationWallpaper->getTexture()->m_texID;
        auto *sourceFb = static_cast<Render::GL::CGLFramebuffer *>(source.get());
        auto *outputFb = static_cast<Render::GL::CGLFramebuffer *>(presentation.get());
        const GLuint sourceId = sourceFb->getFBID(), outputId = outputFb->getFBID();
        const int width = static_cast<int>(source->m_size.x), height = static_cast<int>(source->m_size.y);
        if (width <= 0 || height <= 0) return false;
        (void)ctx;
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> bracket{{.viewport_width = width, .viewport_height = height,
            .framebuffer = outputId}};
        return drawRotation(m_program, m_vao, m_uniforms, src, dst, out, sourceId, outputId,
            width, height, position, direction, gap, matchingWallpaper, wallA, wallB);
    }
#endif
}
