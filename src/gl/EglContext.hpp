#pragma once

#include <cstdio>

#include <EGL/egl.h>

namespace hyprimate::gl {
    struct EglContextId {
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLContext context = EGL_NO_CONTEXT;

        [[nodiscard]] bool valid() const noexcept {
            return display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT;
        }
        [[nodiscard]] bool current() const noexcept {
            return valid() && eglGetCurrentContext() == context &&
                   eglGetCurrentDisplay() == display;
        }
        bool operator==(const EglContextId &) const = default;
    };

    // Hyprland uses a surfaceless GLES context. Never substitute a share group: VAOs and FBOs
    // are context-local. Do not steal a thread bound to another client API.
    class ScopedEglContext {
      public:
        explicit ScopedEglContext(EglContextId target) noexcept {
            if (!target.valid() || eglQueryAPI() != EGL_OPENGL_ES_API) return;
            m_previous = {eglGetCurrentDisplay(), eglGetCurrentContext()};
            m_draw = eglGetCurrentSurface(EGL_DRAW);
            m_read = eglGetCurrentSurface(EGL_READ);
            if (target != m_previous &&
                eglMakeCurrent(target.display, EGL_NO_SURFACE, EGL_NO_SURFACE, target.context) !=
                    EGL_TRUE) {
                return;  // EGL leaves the previous binding intact on failure (including
                         // EGL_BAD_ACCESS).
            }
            m_entered = true;
        }
        ~ScopedEglContext() {
            if (!m_entered) return;
            const EGLDisplay display = eglGetCurrentDisplay();
            if (eglGetCurrentContext() == m_previous.context && display == m_previous.display &&
                eglGetCurrentSurface(EGL_DRAW) == m_draw &&
                eglGetCurrentSurface(EGL_READ) == m_read)
                return;
            // Unbinding a previously empty thread needs the *current* display, not EGL_NO_DISPLAY.
            if (eglMakeCurrent(m_previous.valid() ? m_previous.display : display,
                    m_draw,
                    m_read,
                    m_previous.context) != EGL_TRUE) {
                std::fprintf(
                    stderr, "hyprimate: cannot restore caller EGL binding (0x%x)\n", eglGetError());
                if (display != EGL_NO_DISPLAY)
                    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
        }
        [[nodiscard]] explicit operator bool() const noexcept {
            return m_entered;
        }
        ScopedEglContext(const ScopedEglContext &) = delete;
        ScopedEglContext &operator=(const ScopedEglContext &) = delete;

      private:
        EglContextId m_previous;
        EGLSurface m_draw = EGL_NO_SURFACE;
        EGLSurface m_read = EGL_NO_SURFACE;
        bool m_entered = false;
    };
}  // namespace hyprimate::gl
