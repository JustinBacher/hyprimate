#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl32.h>

namespace hyprimate::gl {
    // Requested starting state for raw material work, not the caller's saved state.
    struct SShadowedState {
        GLint viewport_x = 0;
        GLint viewport_y = 0;
        GLsizei viewport_width = 0;
        GLsizei viewport_height = 0;
        GLuint framebuffer = 0;
        bool blend = false;
        GLenum blend_src = GL_ONE;
        GLenum blend_dst = GL_ZERO;
    };

    struct SRawGlShadowState {
        std::array<GLint, 4> viewport{};
        GLint active_texture = GL_TEXTURE0;
        GLint array_buffer = 0;
        GLint draw_framebuffer = 0;
        GLint read_framebuffer = 0;
        GLint src = GL_ONE;
        GLint dst = GL_ZERO;
        bool scissor = false;
        bool stencil = false;
        bool blend = false;
    };

    // Compositor-independent implementation, exercised by the real GLES tests.
    // Shadow::sync updates host caches after matching raw state is installed;
    // Shadow::forget_framebuffer invalidates any enclosing host binding guards.
    // All work, including destruction, must run with the owning context current.
    template <std::size_t Units, typename Shadow>
    class CRawGlState {
      public:
        explicit CRawGlState(const SShadowedState &known, Shadow &shadow) : m_shadow(shadow) {
            glGetIntegerv(GL_VIEWPORT, m_prior.viewport.data());
            glGetIntegerv(GL_ACTIVE_TEXTURE, &m_prior.active_texture);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_prior.array_buffer);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_prior.draw_framebuffer);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_prior.read_framebuffer);
            glGetIntegerv(GL_BLEND_SRC_RGB, &m_prior.src);
            glGetIntegerv(GL_BLEND_DST_RGB, &m_prior.dst);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_src_alpha);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &m_dst_alpha);
            m_prior.scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
            m_prior.stencil = glIsEnabled(GL_STENCIL_TEST) != GL_FALSE;
            m_prior.blend = glIsEnabled(GL_BLEND) != GL_FALSE;
            glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vertex_array);
            glGetIntegerv(GL_SCISSOR_BOX, m_scissor_box.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, m_mask.data());
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &m_equation_rgb);
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &m_equation_alpha);
            glGetFloatv(GL_DEPTH_RANGE, m_depth_range.data());
            for (std::size_t i = 0; i < CAPS.size(); ++i) {
                m_caps[i] = glIsEnabled(CAPS[i]) != GL_FALSE;
                glDisable(CAPS[i]);
            }
            // The compositor also supports GLES 3.0; sample masks were added in 3.1.
            GLint minor_version = 0;
            glGetIntegerv(GL_MINOR_VERSION, &minor_version);
            m_has_sample_mask = minor_version >= 1;
            if (m_has_sample_mask) {
                m_sample_mask = glIsEnabled(GL_SAMPLE_MASK) != GL_FALSE;
                glDisable(GL_SAMPLE_MASK);
            }
            for (std::size_t unit = 0; unit < Units; ++unit) {
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_units[unit].texture_2d);
                glGetIntegerv(GL_TEXTURE_BINDING_3D, &m_units[unit].texture_3d);
                glGetIntegerv(GL_SAMPLER_BINDING, &m_units[unit].sampler);
                // Texture parameters belong to the material, not an inherited sampler object.
                glBindSampler(static_cast<GLuint>(unit), 0);
            }
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glBlendEquation(GL_FUNC_ADD);
            glDepthRangef(0, 1);
            const SRawGlShadowState BASELINE{
                .viewport = {known.viewport_x, known.viewport_y, known.viewport_width,
                    known.viewport_height},
                .draw_framebuffer = static_cast<GLint>(known.framebuffer),
                .read_framebuffer = static_cast<GLint>(known.framebuffer),
                .src = static_cast<GLint>(known.blend_src),
                .dst = static_cast<GLint>(known.blend_dst),
                .blend = known.blend,
            };
            apply_shadow(BASELINE);
        }

        ~CRawGlState() {
            // A bracket also surrounds release/resize. Never recreate a deleted texture,
            // buffer or framebuffer, or try to bind a deleted VAO, sampler or program.
            for (std::size_t unit = 0; unit < Units; ++unit) {
                const auto &saved = m_units[unit];
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                glBindTexture(GL_TEXTURE_2D, live(saved.texture_2d, glIsTexture));
                glBindTexture(GL_TEXTURE_3D, live(saved.texture_3d, glIsTexture));
                glBindSampler(static_cast<GLuint>(unit), live(saved.sampler, glIsSampler));
            }
            m_prior.array_buffer = static_cast<GLint>(live(m_prior.array_buffer, glIsBuffer));
            const GLint DRAW_FRAMEBUFFER = m_prior.draw_framebuffer;
            const GLint READ_FRAMEBUFFER = m_prior.read_framebuffer;
            m_prior.draw_framebuffer =
                static_cast<GLint>(live(DRAW_FRAMEBUFFER, glIsFramebuffer));
            m_prior.read_framebuffer = DRAW_FRAMEBUFFER == READ_FRAMEBUFFER
                ? m_prior.draw_framebuffer
                : static_cast<GLint>(live(READ_FRAMEBUFFER, glIsFramebuffer));
            // Host binding guards can outlive this bracket. Invalidate their saved
            // bindings too, before their own destructor gets a chance to rebind them.
            if (DRAW_FRAMEBUFFER != 0 && m_prior.draw_framebuffer == 0) {
                m_shadow.forget_framebuffer(static_cast<GLuint>(DRAW_FRAMEBUFFER));
            }
            if (READ_FRAMEBUFFER != 0 && READ_FRAMEBUFFER != DRAW_FRAMEBUFFER &&
                m_prior.read_framebuffer == 0) {
                m_shadow.forget_framebuffer(static_cast<GLuint>(READ_FRAMEBUFFER));
            }
            apply_shadow(m_prior);
            // Hyprland caches only the RGB blend-factor pair.
            glBlendFuncSeparate(static_cast<GLenum>(m_prior.src), static_cast<GLenum>(m_prior.dst),
                static_cast<GLenum>(m_src_alpha), static_cast<GLenum>(m_dst_alpha));
            glBlendEquationSeparate(
                static_cast<GLenum>(m_equation_rgb), static_cast<GLenum>(m_equation_alpha));
            for (std::size_t i = 0; i < CAPS.size(); ++i) {
                set_enabled(CAPS[i], m_caps[i]);
            }
            if (m_has_sample_mask) {
                set_enabled(GL_SAMPLE_MASK, m_sample_mask);
            }
            glDepthRangef(m_depth_range[0], m_depth_range[1]);
            glColorMask(m_mask[0], m_mask[1], m_mask[2], m_mask[3]);
            glScissor(m_scissor_box[0], m_scissor_box[1], m_scissor_box[2], m_scissor_box[3]);
            glBindVertexArray(live(m_vertex_array, glIsVertexArray));
            GLuint program = live(m_program, glIsProgram);
            if (program != 0) {
                GLint deleted = GL_FALSE;
                glGetProgramiv(program, GL_DELETE_STATUS, &deleted);
                if (deleted != GL_FALSE) {
                    program = 0;
                }
            }
            glUseProgram(program);
        }

        CRawGlState(const CRawGlState &) = delete;
        CRawGlState &operator=(const CRawGlState &) = delete;
        CRawGlState(CRawGlState &&) = delete;
        CRawGlState &operator=(CRawGlState &&) = delete;

      private:
        // Material-owned VAOs, textures and FBOs cover vertex input and draw-buffer state.
        // Depth functions/masks and renderbuffers are restored by the materials using them.
        // These inherited enables can otherwise suppress fullscreen and mesh draws alike.
        static constexpr std::array<GLenum, 7> CAPS{GL_DEPTH_TEST, GL_CULL_FACE,
            GL_RASTERIZER_DISCARD, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE,
            GL_POLYGON_OFFSET_FILL, GL_PRIMITIVE_RESTART_FIXED_INDEX};

        static void set_enabled(GLenum capability, bool enabled) {
            if (enabled) {
                glEnable(capability);
            } else {
                glDisable(capability);
            }
        }

        template <typename IsObject>
        static GLuint live(GLint name, IsObject is_object) {
            return name != 0 && is_object(static_cast<GLuint>(name)) != GL_FALSE
                ? static_cast<GLuint>(name) : 0;
        }

        void apply_shadow(const SRawGlShadowState &state) {
            glActiveTexture(static_cast<GLenum>(state.active_texture));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.array_buffer));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.draw_framebuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.read_framebuffer));
            glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
            set_enabled(GL_SCISSOR_TEST, state.scissor);
            set_enabled(GL_STENCIL_TEST, state.stencil);
            set_enabled(GL_BLEND, state.blend);
            glBlendFunc(static_cast<GLenum>(state.src), static_cast<GLenum>(state.dst));
            // Raw work may also have used host setters. Synchronize to the actual target,
            // never replay the entry framebuffer (it may have been destroyed meanwhile).
            m_shadow.sync(state);
        }

        struct SUnit {
            GLint texture_2d = 0;
            GLint texture_3d = 0;
            GLint sampler = 0;
        };

        Shadow &m_shadow;
        SRawGlShadowState m_prior;
        GLint m_program = 0;
        GLint m_vertex_array = 0;
        GLint m_src_alpha = GL_ONE;
        GLint m_dst_alpha = GL_ZERO;
        std::array<GLint, 4> m_scissor_box{};
        GLint m_equation_rgb = GL_FUNC_ADD;
        GLint m_equation_alpha = GL_FUNC_ADD;
        std::array<GLboolean, 4> m_mask{};
        std::array<GLfloat, 2> m_depth_range{};
        std::array<bool, CAPS.size()> m_caps{};
        bool m_has_sample_mask = false;
        bool m_sample_mask = false;
        std::array<SUnit, Units> m_units{};
    };
}  // namespace hyprimate::gl
