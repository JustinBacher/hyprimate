#pragma once

#include <hyprland/src/helpers/memory/Memory.hpp>

#include "gl/ContextOwnerState.hpp"

namespace Render::GL {
    class CHyprOpenGLImpl;
}

namespace hyprimate::gl {
    // Valid only for one draw while its compositor EGL binding remains uninterrupted.
    class CurrentCompositorContext {
      public:
        CurrentCompositorContext() noexcept;
        CurrentCompositorContext(const CurrentCompositorContext &) = delete;
        CurrentCompositorContext &operator=(const CurrentCompositorContext &) = delete;
        CurrentCompositorContext(CurrentCompositorContext &&) = delete;
        CurrentCompositorContext &operator=(CurrentCompositorContext &&) = delete;
        [[nodiscard]] explicit operator bool() const noexcept {
            return m_id.valid();
        }

      private:
        friend class CompositorContextOwner;
        WP<Render::GL::CHyprOpenGLImpl> m_backend;
        EglContextId m_id;
    };

    // Kept with the GL names, not the animation/window that happens to draw them. The weak
    // backend identity distinguishes a replacement backend even if EGL reuses handle values.
    class CompositorContextOwner {
      public:
        [[nodiscard]] bool ensure_current() noexcept;
        [[nodiscard]] bool ensure_current(const CurrentCompositorContext &) noexcept;

        // Classification does not adopt a new owner or retire names. An unowned handle
        // classifies a validated current context as current.
        [[nodiscard]] ContextOwnerState classify_current() const noexcept;
        [[nodiscard]] ContextOwnerState classify_current(const CurrentCompositorContext &) const noexcept;
        void reset() noexcept;

      private:
        friend class ScopedCompositorContext;
        WP<Render::GL::CHyprOpenGLImpl> m_backend;
        EglContextId m_id;
    };

    class ScopedCompositorContext {
      public:
        ScopedCompositorContext() noexcept;
        explicit ScopedCompositorContext(const CompositorContextOwner &owner) noexcept;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(m_scope);
        }

      private:
        ScopedEglContext m_scope;
    };
}  // namespace hyprimate::gl
