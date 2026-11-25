#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "gl/CompositorContext.hpp"

namespace hyprimate::gl {
    namespace {
        EglContextId compositor_id() noexcept {
            const auto &backend = Render::GL::g_pHyprOpenGL;
            // Callers also use the host renderer's framebuffer guard. During shutdown the two
            // globals do not necessarily disappear together.
            if (!backend || !g_pHyprRenderer) {
                return {};
            }
            return {.display = backend->m_eglDisplay, .context = backend->m_eglContext};
        }
    }  // namespace

    CurrentCompositorContext::CurrentCompositorContext() noexcept {
        const auto CURRENT = compositor_id();
        if (!CURRENT.current()) return;
        m_backend = WP<Render::GL::CHyprOpenGLImpl>{Render::GL::g_pHyprOpenGL};
        m_id = CURRENT;
    }

    bool CompositorContextOwner::ensure_current() noexcept {
        const auto &backend = Render::GL::g_pHyprOpenGL;
        if (m_id.valid()) {
            return !m_backend.expired() && m_backend == backend && m_id == compositor_id() &&
                   m_id.current();
        }
        const auto CURRENT = compositor_id();
        if (!CURRENT.current()) {
            return false;
        }
        m_backend = WP<Render::GL::CHyprOpenGLImpl>{backend};
        m_id = CURRENT;
        return true;
    }

    bool CompositorContextOwner::ensure_current(const CurrentCompositorContext &current) noexcept {
        if (!current || current.m_backend.expired()) return false;
        if (m_id.valid()) {
            return !m_backend.expired() && m_backend == current.m_backend && m_id == current.m_id;
        }
        m_backend = current.m_backend;
        m_id = current.m_id;
        return true;
    }

    ContextOwnerState CompositorContextOwner::classify_current() const noexcept {
        const CurrentCompositorContext current;
        return classify_current(current);
    }

    ContextOwnerState CompositorContextOwner::classify_current(
        const CurrentCompositorContext &current) const noexcept {
        const bool available = current && !current.m_backend.expired();
        return detail::classify_context_owner(m_id,
            available ? current.m_id : EglContextId{},
            available && !m_backend.expired() && m_backend == current.m_backend);
    }

    void CompositorContextOwner::reset() noexcept {
        m_backend.reset();
        m_id = {};
    }

    ScopedCompositorContext::ScopedCompositorContext() noexcept : m_scope(compositor_id()) {
    }

    ScopedCompositorContext::ScopedCompositorContext(const CompositorContextOwner &owner) noexcept
        : m_scope(!owner.m_backend.expired() && owner.m_backend == Render::GL::g_pHyprOpenGL &&
                          owner.m_id == compositor_id()
                      ? owner.m_id
                      : EglContextId{}) {
    }
}  // namespace hyprimate::gl
