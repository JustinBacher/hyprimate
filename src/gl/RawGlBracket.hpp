#pragma once

#include <hyprland/src/render/OpenGL.hpp>

#include "RawGlState.hpp"

namespace hyprimate::gl {
    namespace detail {
        struct SHyprlandRawGlShadow {
            void forget_framebuffer(GLuint framebuffer) const {
                Render::GL::g_pHyprOpenGL->onFramebufferDeleted(framebuffer);
            }

            void sync(const SRawGlShadowState &state) const {
                auto &renderer = *Render::GL::g_pHyprOpenGL;
                renderer.setActiveTexture(static_cast<GLenum>(state.active_texture));
                renderer.bindArrayBuffer(static_cast<GLuint>(state.array_buffer));
                renderer.bindFramebuffer(
                    GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.draw_framebuffer));
                renderer.bindFramebuffer(
                    GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.read_framebuffer));
                renderer.setViewport(
                    state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
                renderer.setCapStatus(GL_SCISSOR_TEST, state.scissor);
                renderer.setCapStatus(GL_STENCIL_TEST, state.stencil);
                renderer.blend(state.blend);
                renderer.blendFunc(static_cast<GLenum>(state.src), static_cast<GLenum>(state.dst));
            }
        };
    }  // namespace detail

    // Raw GL isolation for material work (ADR-0002). The raw guard installs the draw
    // baseline before synchronizing Hyprland's caches, then restores caller state, including
    // independent READ/DRAW framebuffers.
    // Route plugin FBO deletion through delete_framebuffers to invalidate saved and outer
    // bindings before name reuse. External deletions do not notify the raw guard.
    // Construct and destroy with the owning context current.
    template <std::size_t Units>
    class CRawGlBracket {
      public:
        explicit CRawGlBracket(const SShadowedState &known) : m_state(known, m_shadow) {
        }

        CRawGlBracket(const CRawGlBracket &) = delete;
        CRawGlBracket &operator=(const CRawGlBracket &) = delete;
        CRawGlBracket(CRawGlBracket &&) = delete;
        CRawGlBracket &operator=(CRawGlBracket &&) = delete;

      private:
        detail::SHyprlandRawGlShadow m_shadow;
        CRawGlState<Units, detail::SHyprlandRawGlShadow> m_state;
    };
}  // namespace hyprimate::gl
