#include <algorithm>
#include <array>
#include <cmath>

#include "workspace/rotation/RotationGeometry.hpp"
#include "workspace/rotation/RotationRenderer.hpp"
#ifndef HYPRIMATE_ROTATION_RAW_ONLY
#include "workspace/rotation/RotationShaders.hpp"
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/NativeResource.hpp"
#include "gl/RawGlBracket.hpp"
#endif

namespace hyprimate::workspace {
    bool rotation_uniforms(GLuint program, RotationUniforms &uniforms) {
        const std::array<const char *, 13> NAMES{"sourceImage",
            "destinationImage",
            "sourceWall",
            "destinationWall",
            "sourceInverse",
            "destinationInverse",
            "size",
            "progress",
            "direction",
            "blurRadius",
            "style",
            "matching",
            "gapColor"};
        for (std::size_t i = 0; i < NAMES.size(); ++i) {
            uniforms[i] = glGetUniformLocation(program, NAMES[i]);
            if (uniforms[i] < 0) {
                return false;
            }
        }
        return true;
    }

    bool draw_rotation(GLuint program,
        GLuint vao,
        const RotationUniforms &uniforms,
        GLuint src,
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
        if (program == 0 || vao == 0 || src == 0 || dst == 0 || out == 0 || source_wallpaper == 0 ||
            destination_wallpaper == 0 || src == dst || src == out || dst == out ||
            out == source_wallpaper || out == destination_wallpaper || source_framebuffer == 0 ||
            presentation_framebuffer == 0 || source_framebuffer == presentation_framebuffer ||
            width <= 0 || height <= 0 || !std::isfinite(position) || position < 0 || position > 1) {
            return false;
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const std::array<GLuint, 4> INPUTS{src, dst, source_wallpaper, destination_wallpaper};
        for (std::size_t i = 0; i < INPUTS.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindSampler(static_cast<GLuint>(i), 0);
            glBindTexture(GL_TEXTURE_2D, INPUTS[i]);
        }
        glActiveTexture(GL_TEXTURE0);
        const auto FRAME =
            rotation::make_frame({.width = width, .height = height}, position, direction);
        glUseProgram(program);
        for (std::size_t i = 0; i < INPUTS.size(); ++i) {
            glUniform1i(uniforms[i], static_cast<GLint>(i));
        }
        glUniformMatrix3fv(uniforms[4], 1, GL_FALSE, FRAME.source.data());
        glUniformMatrix3fv(uniforms[5], 1, GL_FALSE, FRAME.destination.data());
        glUniform2f(uniforms[6], static_cast<float>(width), static_cast<float>(height));
        glUniform1f(uniforms[7], static_cast<float>(position));
        glUniform1f(uniforms[8], direction == Direction::Forward ? 1.0F : -1.0F);
        glUniform1f(
            uniforms[9], std::isfinite(gap.blur) ? std::clamp(gap.blur, 0.0F, 64.0F) : 0.0F);
        glUniform1i(uniforms[10], static_cast<int>(gap.style));
        glUniform1i(uniforms[11], matching_wallpaper ? 1 : 0);
        glUniform4fv(uniforms[12], 1, gap.color.data());
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return true;
    }

#ifndef HYPRIMATE_ROTATION_RAW_ONLY
    RotationRenderer::RotationRenderer() = default;
    RotationRenderer::~RotationRenderer() {
        release();
    }
    bool RotationRenderer::initialize() {
        const gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT || !m_context.ensure_current()) {
            return false;
        }
        if (m_program != 0) {
            return true;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> BRACKET{{}};
        m_shader = std::make_unique<CShader>();
        if (!m_shader->createProgram(VERTEX_SHADER, FRAGMENT_SHADER, true, true)) {
            m_shader.reset();
            return false;
        }
        m_program = m_shader->program();
        if (!rotation_uniforms(m_program, m_uniforms)) {
            release();
            return false;
        }
        glGenVertexArrays(1, &m_vao);
        if (m_vao == 0) {
            release();
            return false;
        }
        return true;
    }
    void RotationRenderer::release() {
        if (!m_shader && m_vao == 0) {
            m_context.reset();
            return;
        }
        const gl::ScopedCompositorContext CONTEXT{m_context};
        if (!CONTEXT) {
            // CShader has no detach API; its destructor must not delete in another context.
            [[maybe_unused]] const auto *const LEAKED_SHADER = m_shader.release();
            m_vao = 0;
            m_program = 0;
            m_context.reset();
            return;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> BRACKET{{}};
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
        }
        m_shader.reset();
        m_vao = 0;
        m_program = 0;
        m_context.reset();
    }
    bool RotationRenderer::draw(Render::CRenderContext &ctx,
        const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &destination,
        const SP<Render::IFramebuffer> &presentation,
        const SP<Render::IFramebuffer> &source_wallpaper,
        const SP<Render::IFramebuffer> &destination_wallpaper,
        double position,
        Direction direction,
        const GapFill &gap,
        bool matching_wallpaper) {
        if (!m_context.ensure_current()) {
            return false;
        }
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !source || !destination ||
            !presentation || !source_wallpaper || !destination_wallpaper || !source->getTexture() ||
            !destination->getTexture() || !presentation->getTexture() ||
            !source_wallpaper->getTexture() || !destination_wallpaper->getTexture() ||
            source->m_size != destination->m_size || source->m_size != presentation->m_size ||
            source->m_size != source_wallpaper->m_size ||
            source->m_size != destination_wallpaper->m_size) {
            return false;
        }
        if (!gl::native_current(source) || !gl::native_current(destination) ||
            !gl::native_current(presentation) || !gl::native_current(source_wallpaper) ||
            !gl::native_current(destination_wallpaper)) {
            return false;
        }
        const GLuint SRC = source->getTexture()->m_texID;
        const GLuint DST = destination->getTexture()->m_texID;
        const GLuint OUT = presentation->getTexture()->m_texID;
        const GLuint WALL_A = source_wallpaper->getTexture()->m_texID;
        const GLuint WALL_B = destination_wallpaper->getTexture()->m_texID;
        // RT_GL framebuffers are CGLFramebuffer instances; Hyprland does not expose getFBID on the
        // base.
        auto *source_fb = static_cast<Render::GL::CGLFramebuffer *>(
            source.get());  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
        auto *output_fb = static_cast<Render::GL::CGLFramebuffer *>(
            presentation.get());  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
        const GLuint SOURCE_ID = source_fb->getFBID();
        const GLuint OUTPUT_ID = output_fb->getFBID();
        const int WIDTH = static_cast<int>(source->m_size.x);
        const int HEIGHT = static_cast<int>(source->m_size.y);
        if (WIDTH <= 0 || HEIGHT <= 0) {
            return false;
        }
        (void)ctx;
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<4> BRACKET{
            {.viewport_width = WIDTH, .viewport_height = HEIGHT, .framebuffer = OUTPUT_ID}};
        return draw_rotation(m_program,
            m_vao,
            m_uniforms,
            SRC,
            DST,
            OUT,
            SOURCE_ID,
            OUTPUT_ID,
            WIDTH,
            HEIGHT,
            position,
            direction,
            gap,
            matching_wallpaper,
            WALL_A,
            WALL_B);
    }
#endif
}  // namespace hyprimate::workspace
