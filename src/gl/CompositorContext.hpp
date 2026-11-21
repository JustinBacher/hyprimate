#pragma once

#include <hyprland/src/helpers/memory/Memory.hpp>

#include "gl/EglContext.hpp"

namespace Render::GL { class CHyprOpenGLImpl; }

namespace hyprimate::gl {
    // Kept with the GL names, not the animation/window that happens to draw them. The weak
    // backend identity distinguishes a replacement backend even if EGL reuses handle values.
    class CompositorContextOwner {
      public:
        [[nodiscard]] bool ensure_current() noexcept;
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
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_scope); }

      private:
        ScopedEglContext m_scope;
    };
} // namespace hyprimate::gl
