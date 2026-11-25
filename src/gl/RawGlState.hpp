#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include <EGL/egl.h>

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
    // FBO names cannot identify an object after deletion and numeric-name reuse.
    // `delete_framebuffers` notifies active brackets and Hyprland's outer binding guard first.
    // Only deletion routed through this wrapper can invalidate saved bindings before reuse.
    // Non-FBO bindings still rely on name checks.
    struct SFramebufferGuardRegistration {
        using Invalidate = void (*)(void *, GLuint);

        SFramebufferGuardRegistration(void *owner, Invalidate invalidate)
            : m_owner(owner), m_invalidate(invalidate), m_previous(active) {
            active = this;
        }
        ~SFramebufferGuardRegistration() {
            active = m_previous;
        }
        SFramebufferGuardRegistration(SFramebufferGuardRegistration &&) = delete;
        SFramebufferGuardRegistration &operator=(SFramebufferGuardRegistration &&) = delete;
        SFramebufferGuardRegistration(const SFramebufferGuardRegistration &) = delete;
        SFramebufferGuardRegistration &operator=(const SFramebufferGuardRegistration &) = delete;

        [[nodiscard]] static bool has_active() noexcept {
            return active != nullptr;
        }
        static void invalidate(GLuint framebuffer) {
            for (auto *guard = active; guard != nullptr; guard = guard->m_previous) {
                guard->m_invalidate(guard->m_owner, framebuffer);
            }
        }

      private:
        inline static thread_local SFramebufferGuardRegistration *active = nullptr;
        void *m_owner;
        Invalidate m_invalidate;
        SFramebufferGuardRegistration *m_previous = nullptr;
    };

    inline void delete_framebuffers(GLsizei count, const GLuint *framebuffers) {
        if (count > 0 && SFramebufferGuardRegistration::has_active()) {
            for (GLsizei index = 0; index < count; ++index) {
                if (framebuffers[index] != 0) {
                    SFramebufferGuardRegistration::invalidate(framebuffers[index]);
                }
            }
        }
        glDeleteFramebuffers(count, framebuffers);
    }


    inline void delete_framebuffer(GLuint framebuffer) {
        delete_framebuffers(1, &framebuffer);
    }


    // Compositor-independent implementation, exercised by the real GLES tests.
    // Shadow::sync updates host caches after matching raw state is installed;
    // Shadow::forget_framebuffer invalidates any enclosing host binding guards.
    // All work, including destruction, must run with the owning context current.
    template <std::size_t Units, typename Shadow>
    class CRawGlState {
      public:
        explicit CRawGlState(const SShadowedState &known, Shadow &shadow)
            : m_shadow(shadow),
              m_framebuffer_registration(this, &CRawGlState::invalidate_saved_framebuffer) {
            GLint major_version = 0;
            GLint minor_version = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &major_version);
            glGetIntegerv(GL_MINOR_VERSION, &minor_version);
            m_has_sample_mask = major_version > 3 || minor_version >= 1;
            m_indexed_core = major_version > 3 || (major_version == 3 && minor_version >= 2);
            m_has_indexed_state = m_indexed_core;
            if (!m_has_indexed_state) {
                const auto *raw_extensions = glGetString(GL_EXTENSIONS);
                const std::string_view extensions = raw_extensions == nullptr
                    ? std::string_view{}
                    : std::string_view{reinterpret_cast<const char *>(raw_extensions)};
                if (has_extension(extensions, "GL_EXT_draw_buffers_indexed")) {
                    static const SIndexedApi EXT = [] {
                        SIndexedApi api;
                        api.load(true);
                        return api;
                    }();
                    if (EXT.valid()) {
                        m_indexed_api = EXT;
                        m_has_indexed_state = true;
                    }
                }
                if (!m_has_indexed_state &&
                    has_extension(extensions, "GL_OES_draw_buffers_indexed")) {
                    static const SIndexedApi OES = [] {
                        SIndexedApi api;
                        api.load(false);
                        return api;
                    }();
                    if (OES.valid()) {
                        m_indexed_api = OES;
                        m_has_indexed_state = true;
                    }
                }
            }
            if (m_has_indexed_state) {
                glGetIntegerv(GL_MAX_DRAW_BUFFERS, &m_indexed_count);
                if (m_indexed_count > static_cast<GLint>(m_indexed_states.size())) {
                    m_indexed_overflow.resize(
                        static_cast<std::size_t>(m_indexed_count) - m_indexed_states.size());
                }
            }
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
            if (m_program != 0) {
                glGetProgramiv(static_cast<GLuint>(m_program), GL_DELETE_STATUS, &m_program_deleted);
            }
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
            if (m_has_indexed_state) {
                for (GLint index = 0; index < m_indexed_count; ++index) {
                    auto &saved = indexed_state(static_cast<std::size_t>(index));
                    const auto DRAW_BUFFER = static_cast<GLuint>(index);
                    std::array<GLint, 4> mask{};
                    glGetIntegeri_v(GL_COLOR_WRITEMASK, DRAW_BUFFER, mask.data());
                    for (std::size_t channel = 0; channel < mask.size(); ++channel) {
                        saved.color_mask[channel] = mask[channel] != 0;
                    }
                    saved.blend = m_indexed_api.is_enabled(GL_BLEND, DRAW_BUFFER) != GL_FALSE;
                    glGetIntegeri_v(GL_BLEND_SRC_RGB, DRAW_BUFFER, &saved.src_rgb);
                    glGetIntegeri_v(GL_BLEND_DST_RGB, DRAW_BUFFER, &saved.dst_rgb);
                    glGetIntegeri_v(GL_BLEND_SRC_ALPHA, DRAW_BUFFER, &saved.src_alpha);
                    glGetIntegeri_v(GL_BLEND_DST_ALPHA, DRAW_BUFFER, &saved.dst_alpha);
                    glGetIntegeri_v(GL_BLEND_EQUATION_RGB, DRAW_BUFFER, &saved.equation_rgb);
                    glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, DRAW_BUFFER, &saved.equation_alpha);
                }
            }
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glBlendEquation(GL_FUNC_ADD);
            glDepthRangef(0, 1);
            const SRawGlShadowState BASELINE{
                .viewport = {known.viewport_x,
                    known.viewport_y,
                    known.viewport_width,
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
            // FBO saved bindings are invalidated by delete_framebuffers before deletion.
            // Other object types still use name-only liveness checks; deletion/reuse is not safe.
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
            m_prior.draw_framebuffer = static_cast<GLint>(live(DRAW_FRAMEBUFFER, glIsFramebuffer));
            m_prior.read_framebuffer =
                DRAW_FRAMEBUFFER == READ_FRAMEBUFFER
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
            glBlendFuncSeparate(static_cast<GLenum>(m_prior.src),
                static_cast<GLenum>(m_prior.dst),
                static_cast<GLenum>(m_src_alpha),
                static_cast<GLenum>(m_dst_alpha));
            GLint global_equation = m_equation_rgb;
            if (!m_indexed_core) {
                // EXT/OES cannot set an advanced equation per slot. All advanced
                // slots therefore share the last globally assigned advanced mode.
                for (GLint index = 0; index < m_indexed_count; ++index) {
                    const GLint equation =
                        indexed_state(static_cast<std::size_t>(index)).equation_rgb;
                    if (!ordinary_equation(equation)) {
                        global_equation = equation;
                        break;
                    }
                }
            }
            if (ordinary_equation(global_equation)) {
                glBlendEquationSeparate(
                    static_cast<GLenum>(m_equation_rgb), static_cast<GLenum>(m_equation_alpha));
            } else {
                glBlendEquation(static_cast<GLenum>(global_equation));
            }
            for (std::size_t i = 0; i < CAPS.size(); ++i) {
                set_enabled(CAPS[i], m_caps[i]);
            }
            if (m_has_sample_mask) {
                set_enabled(GL_SAMPLE_MASK, m_sample_mask);
            }
            glDepthRangef(m_depth_range[0], m_depth_range[1]);
            glColorMask(m_mask[0], m_mask[1], m_mask[2], m_mask[3]);
            for (GLint index = 0; index < m_indexed_count; ++index) {
                const auto &saved = indexed_state(static_cast<std::size_t>(index));
                const auto DRAW_BUFFER = static_cast<GLuint>(index);
                m_indexed_api.color_mask(DRAW_BUFFER, saved.color_mask[0], saved.color_mask[1],
                    saved.color_mask[2], saved.color_mask[3]);
                m_indexed_api.factors(DRAW_BUFFER, static_cast<GLenum>(saved.src_rgb),
                    static_cast<GLenum>(saved.dst_rgb), static_cast<GLenum>(saved.src_alpha),
                    static_cast<GLenum>(saved.dst_alpha));
                if (ordinary_equation(saved.equation_rgb)) {
                    m_indexed_api.equation(DRAW_BUFFER, static_cast<GLenum>(saved.equation_rgb),
                        static_cast<GLenum>(saved.equation_alpha));
                } else if (m_indexed_core && saved.equation_rgb != m_equation_rgb) {
                    // The global setter already restored matching advanced slots.
                    m_indexed_api.equation_single(
                        DRAW_BUFFER, static_cast<GLenum>(saved.equation_rgb));
                }
                if (saved.blend) {
                    m_indexed_api.enable(GL_BLEND, DRAW_BUFFER);
                } else {
                    m_indexed_api.disable(GL_BLEND, DRAW_BUFFER);
                }
            }
            glScissor(m_scissor_box[0], m_scissor_box[1], m_scissor_box[2], m_scissor_box[3]);
            glBindVertexArray(live(m_vertex_array, glIsVertexArray));
            GLint current_program = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
            // A program already pending deletion at entry must remain current if untouched.
            // A deletion requested during the bracket instead releases it on exit.
            if (current_program != m_program || m_program_deleted == GL_FALSE) {
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
        }

        CRawGlState(const CRawGlState &) = delete;
        CRawGlState &operator=(const CRawGlState &) = delete;
        CRawGlState(CRawGlState &&) = delete;
        CRawGlState &operator=(CRawGlState &&) = delete;

      private:
        // Material-owned VAOs, textures and FBOs cover vertex input and draw-buffer state.
        // Depth functions/masks and renderbuffers are restored by the materials using them.
        // These inherited enables can otherwise suppress fullscreen and mesh draws alike.
        static constexpr std::array<GLenum, 7> CAPS{GL_DEPTH_TEST,
            GL_CULL_FACE,
            GL_RASTERIZER_DISCARD,
            GL_SAMPLE_ALPHA_TO_COVERAGE,
            GL_SAMPLE_COVERAGE,
            GL_POLYGON_OFFSET_FILL,
            GL_PRIMITIVE_RESTART_FIXED_INDEX};

        static void set_enabled(GLenum capability, bool enabled) {
            if (enabled) {
                glEnable(capability);
            } else {
                glDisable(capability);
            }
        }

        static void invalidate_saved_framebuffer(void *owner, GLuint framebuffer) {
            auto &state = *static_cast<CRawGlState *>(owner);
            if (state.m_prior.draw_framebuffer == static_cast<GLint>(framebuffer)) {
                state.m_prior.draw_framebuffer = 0;
            }
            if (state.m_prior.read_framebuffer == static_cast<GLint>(framebuffer)) {
                state.m_prior.read_framebuffer = 0;
            }
            state.m_shadow.forget_framebuffer(framebuffer);
        }
        void set_blend_enabled(bool enabled) {
            if (!m_has_indexed_state) {
                set_enabled(GL_BLEND, enabled);
                return;
            }
            for (GLint index = 0; index < m_indexed_count; ++index) {
                if (enabled) {
                    m_indexed_api.enable(GL_BLEND, static_cast<GLuint>(index));
                } else {
                    m_indexed_api.disable(GL_BLEND, static_cast<GLuint>(index));
                }
            }
        }

        template <typename IsObject>
        static GLuint live(GLint name, IsObject is_object) {
            return name != 0 && is_object(static_cast<GLuint>(name)) != GL_FALSE
                       ? static_cast<GLuint>(name)
                       : 0;
        }

        void apply_shadow(const SRawGlShadowState &state) {
            glActiveTexture(static_cast<GLenum>(state.active_texture));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.array_buffer));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.draw_framebuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.read_framebuffer));
            glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
            set_enabled(GL_SCISSOR_TEST, state.scissor);
            set_enabled(GL_STENCIL_TEST, state.stencil);
            set_blend_enabled(state.blend);
            glBlendFunc(static_cast<GLenum>(state.src), static_cast<GLenum>(state.dst));
            // Raw work may also have used host setters. Synchronize to the actual target,
            // never replay the entry framebuffer (it may have been destroyed meanwhile).
            m_shadow.sync(state);
        }

        static bool has_extension(std::string_view extensions, std::string_view wanted) {
            std::size_t pos = 0;
            while (pos < extensions.size()) {
                const auto end = extensions.find(' ', pos);
                const auto length =
                    end == std::string_view::npos ? extensions.size() - pos : end - pos;
                if (extensions.substr(pos, length) == wanted) {
                    return true;
                }
                if (end == std::string_view::npos) {
                    break;
                }
                pos = end + 1;
            }
            return false;
        }

        // Advanced blend equations are valid only for the non-separate setters. Saved
        // advanced RGB/alpha equations are equal; ordinary equations may differ.
        static bool ordinary_equation(GLint equation) {
            return equation == GL_FUNC_ADD || equation == GL_FUNC_SUBTRACT ||
                   equation == GL_FUNC_REVERSE_SUBTRACT || equation == GL_MIN || equation == GL_MAX;
        }

        // Dispatch is selected from the current context's version or advertised extensions.
        struct SIndexedApi {
            decltype(&glEnablei) enable = &glEnablei;
            decltype(&glDisablei) disable = &glDisablei;
            decltype(&glIsEnabledi) is_enabled = &glIsEnabledi;
            decltype(&glColorMaski) color_mask = &glColorMaski;
            decltype(&glBlendFuncSeparatei) factors = &glBlendFuncSeparatei;
            decltype(&glBlendEquationSeparatei) equation = &glBlendEquationSeparatei;
            decltype(&glBlendEquationi) equation_single = &glBlendEquationi;

            void load(bool ext) {
                enable = reinterpret_cast<decltype(enable)>(
                    eglGetProcAddress(ext ? "glEnableiEXT" : "glEnableiOES"));
                disable = reinterpret_cast<decltype(disable)>(
                    eglGetProcAddress(ext ? "glDisableiEXT" : "glDisableiOES"));
                is_enabled = reinterpret_cast<decltype(is_enabled)>(
                    eglGetProcAddress(ext ? "glIsEnablediEXT" : "glIsEnablediOES"));
                color_mask = reinterpret_cast<decltype(color_mask)>(
                    eglGetProcAddress(ext ? "glColorMaskiEXT" : "glColorMaskiOES"));
                factors = reinterpret_cast<decltype(factors)>(
                    eglGetProcAddress(ext ? "glBlendFuncSeparateiEXT" : "glBlendFuncSeparateiOES"));
                equation = reinterpret_cast<decltype(equation)>(eglGetProcAddress(
                    ext ? "glBlendEquationSeparateiEXT" : "glBlendEquationSeparateiOES"));
                equation_single = reinterpret_cast<decltype(equation_single)>(eglGetProcAddress(
                    ext ? "glBlendEquationiEXT" : "glBlendEquationiOES"));
            }

            [[nodiscard]] bool valid() const {
                return enable != nullptr && disable != nullptr && is_enabled != nullptr &&
                       color_mask != nullptr && factors != nullptr && equation != nullptr &&
                       equation_single != nullptr;
            }
        };

        struct SUnit {
            GLint texture_2d = 0;
            GLint texture_3d = 0;
            GLint sampler = 0;
        };
        struct SIndexedState {
            std::array<GLboolean, 4> color_mask{};
            GLint src_rgb = GL_ONE;
            GLint dst_rgb = GL_ZERO;
            GLint src_alpha = GL_ONE;
            GLint dst_alpha = GL_ZERO;
            GLint equation_rgb = GL_FUNC_ADD;
            GLint equation_alpha = GL_FUNC_ADD;
            bool blend = false;
        };
        SIndexedState &indexed_state(std::size_t index) {
            return index < m_indexed_states.size()
                       ? m_indexed_states[index]
                       : m_indexed_overflow[index - m_indexed_states.size()];
        }


        static constexpr std::size_t MAX_INDEXED_DRAW_BUFFERS = 16;

        Shadow &m_shadow;
        SRawGlShadowState m_prior;
        SIndexedApi m_indexed_api;
        std::array<SIndexedState, MAX_INDEXED_DRAW_BUFFERS> m_indexed_states{};
        std::vector<SIndexedState> m_indexed_overflow;
        GLint m_indexed_count = 0;
        bool m_has_indexed_state = false;
        bool m_indexed_core = false;
        GLint m_program = 0;
        GLint m_program_deleted = GL_FALSE;
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
        SFramebufferGuardRegistration m_framebuffer_registration;
    };
}  // namespace hyprimate::gl
