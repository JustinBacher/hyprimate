#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>

namespace hyprimate::gl {
    // GL state Hyprland's renderer shadows, with the values the bracketed work starts from.
    struct SShadowedState {
        GLint viewport_x = 0;
        GLint viewport_y = 0;
        GLsizei viewport_width = 0;
        GLsizei viewport_height = 0;
        GLuint framebuffer = 0;  // bound for both drawing and reading
        bool blend = false;
        GLenum blend_src = GL_ONE;
        GLenum blend_dst = GL_ZERO;
    };

    // Runs compositor-independent raw GL (materials, ADR-0002) inside Hyprland's renderer.
    //
    // Hyprland shadows part of the GL state (viewport, scissor/stencil/blend enables, active
    // texture, blend factors, array buffer, framebuffers) and skips setter calls that match its
    // shadow, so state changed behind its back must end up exactly where the shadow believes it
    // is. Entry sets every shadowed state to `known` through Hyprland's setters; exit first writes
    // the same values back with raw GL, so real state and shadow agree again.
    //
    // Exit then returns the clip and blend state the caller had (viewport, scissor and stencil
    // enables, blend enable and factors) through the setters, so a caller's scissor or clip stays
    // active for whatever it draws next. Active texture, array buffer and framebuffer are left at
    // `known`: Hyprland binds those before each use, and framebuffers are restored by the
    // caller's binding guard.
    //
    // State Hyprland does not shadow, or caches privately, is captured on entry and restored on
    // exit: the current program (Hyprland caches it), vertex array, scissor box, colour mask,
    // blend equations, depth test, face culling, and the 2D/3D texture and sampler bindings of
    // the first `Units` texture units. Bracketed work may therefore call glScissor freely.
    //
    // Must be constructed and destroyed with Hyprland's EGL context current, on the render
    // thread. Not copyable or movable: it restores exactly once, in its destructor.
    template <std::size_t Units>
    class CRawGlBracket {
      public:
        explicit CRawGlBracket(const SShadowedState &known)
            : m_known(known), m_depth_test(glIsEnabled(GL_DEPTH_TEST) != GL_FALSE),
              m_cull_face(glIsEnabled(GL_CULL_FACE) != GL_FALSE) {
            // On entry the real state equals Hyprland's shadow, so these reads are its values.
            glGetIntegerv(GL_VIEWPORT, m_prior.viewport.data());
            glGetIntegerv(GL_BLEND_SRC_RGB, &m_prior.src);
            glGetIntegerv(GL_BLEND_DST_RGB, &m_prior.dst);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_prior.src_alpha);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &m_prior.dst_alpha);
            m_prior.scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
            m_prior.stencil = glIsEnabled(GL_STENCIL_TEST) != GL_FALSE;
            m_prior.blend = glIsEnabled(GL_BLEND) != GL_FALSE;
            glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vertex_array);
            glGetIntegerv(GL_SCISSOR_BOX, m_scissor_box.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, m_mask.data());
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &m_equation_rgb);
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &m_equation_alpha);

            auto &renderer = *Render::GL::g_pHyprOpenGL;
            for (std::size_t unit = 0; unit < Units; ++unit) {
                renderer.setActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_units[unit].texture_2d);
                glGetIntegerv(GL_TEXTURE_BINDING_3D, &m_units[unit].texture_3d);
                glGetIntegerv(GL_SAMPLER_BINDING, &m_units[unit].sampler);
            }
            renderer.setActiveTexture(GL_TEXTURE0);
            renderer.bindArrayBuffer(0);
            renderer.bindFramebuffer(GL_FRAMEBUFFER, m_known.framebuffer);
            renderer.setViewport(m_known.viewport_x,
                m_known.viewport_y,
                m_known.viewport_width,
                m_known.viewport_height);
            renderer.setCapStatus(GL_SCISSOR_TEST, false);
            renderer.setCapStatus(GL_STENCIL_TEST, false);
            renderer.blend(m_known.blend);
            renderer.blendFunc(m_known.blend_src, m_known.blend_dst);
            // Hyprland's blend cache covers only the RGB pair; make the alpha pair agree with it.
            glBlendFunc(m_known.blend_src, m_known.blend_dst);
        }

        ~CRawGlBracket() {
            for (std::size_t unit = 0; unit < Units; ++unit) {
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_units[unit].texture_2d));
                glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(m_units[unit].texture_3d));
                glBindSampler(
                    static_cast<GLuint>(unit), static_cast<GLuint>(m_units[unit].sampler));
            }
            glActiveTexture(GL_TEXTURE0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, m_known.framebuffer);
            glViewport(m_known.viewport_x,
                m_known.viewport_y,
                m_known.viewport_width,
                m_known.viewport_height);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            if (m_known.blend) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            glBlendFunc(m_known.blend_src, m_known.blend_dst);
            glBlendEquationSeparate(
                static_cast<GLenum>(m_equation_rgb), static_cast<GLenum>(m_equation_alpha));
            set_enabled(GL_DEPTH_TEST, m_depth_test);
            set_enabled(GL_CULL_FACE, m_cull_face);
            glColorMask(m_mask[0], m_mask[1], m_mask[2], m_mask[3]);
            glScissor(m_scissor_box[0], m_scissor_box[1], m_scissor_box[2], m_scissor_box[3]);
            glBindVertexArray(static_cast<GLuint>(m_vertex_array));
            glUseProgram(static_cast<GLuint>(m_program));

            // Shadow and real state agree at `known`; return the caller's clip and blend state.
            auto &renderer = *Render::GL::g_pHyprOpenGL;
            renderer.setViewport(
                m_prior.viewport[0], m_prior.viewport[1], m_prior.viewport[2], m_prior.viewport[3]);
            renderer.setCapStatus(GL_SCISSOR_TEST, m_prior.scissor);
            renderer.setCapStatus(GL_STENCIL_TEST, m_prior.stencil);
            renderer.blend(m_prior.blend);
            renderer.blendFunc(static_cast<GLenum>(m_prior.src), static_cast<GLenum>(m_prior.dst));
            if (m_prior.src_alpha != m_prior.src || m_prior.dst_alpha != m_prior.dst) {
                // Hyprland's blend cache covers only the RGB pair.
                glBlendFuncSeparate(static_cast<GLenum>(m_prior.src),
                    static_cast<GLenum>(m_prior.dst),
                    static_cast<GLenum>(m_prior.src_alpha),
                    static_cast<GLenum>(m_prior.dst_alpha));
            }
        }

        CRawGlBracket(const CRawGlBracket &) = delete;
        CRawGlBracket &operator=(const CRawGlBracket &) = delete;
        CRawGlBracket(CRawGlBracket &&) = delete;
        CRawGlBracket &operator=(CRawGlBracket &&) = delete;

      private:
        struct SUnit {
            GLint texture_2d = 0;
            GLint texture_3d = 0;
            GLint sampler = 0;
        };

        static void set_enabled(GLenum capability, bool enabled) {
            if (enabled) {
                glEnable(capability);
            } else {
                glDisable(capability);
            }
        }

        struct SPrior {
            std::array<GLint, 4> viewport{};
            GLint src = GL_ONE;
            GLint dst = GL_ZERO;
            GLint src_alpha = GL_ONE;
            GLint dst_alpha = GL_ZERO;
            bool scissor = false;
            bool stencil = false;
            bool blend = false;
        };

        SShadowedState m_known;
        SPrior m_prior;
        GLint m_program = 0;
        GLint m_vertex_array = 0;
        std::array<GLint, 4> m_scissor_box{};
        GLint m_equation_rgb = GL_FUNC_ADD;
        GLint m_equation_alpha = GL_FUNC_ADD;
        std::array<GLboolean, 4> m_mask{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        bool m_depth_test = false;
        bool m_cull_face = false;
        std::array<SUnit, Units> m_units{};
    };
}  // namespace hyprimate::gl
