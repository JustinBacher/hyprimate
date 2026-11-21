#include <array>
#include <semaphore>
#include <thread>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "gl/EglContext.hpp"
#include "gl/NativeRetirement.hpp"

namespace {
    struct Owner : hyprimate::gl::EglContextId {
        bool ensure_current() const { return current(); }
    };
    struct NativeBuffer {
        GLuint name;
        EGLContext &destroyed_in;
        ~NativeBuffer() {
            destroyed_in = eglGetCurrentContext();
            glDeleteBuffers(1, &name);
        }
    };
}

int main() {
    gl_harness::CContext fixture(8, 8);
    if (!fixture.valid()) return 1;
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext caller = eglGetCurrentContext();
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW);
    EGLint config_id = 0, count = 0;
    EGLConfig config{};
    if (!eglQueryContext(display, caller, EGL_CONFIG_ID, &config_id)) return 1;
    const std::array filter{EGL_CONFIG_ID, config_id, EGL_NONE};
    if (!eglChooseConfig(display, filter.data(), &config, 1, &count) || count != 1) return 1;
    const std::array attrs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const std::array size{EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    const EGLContext owner = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs.data());
    const EGLSurface read = eglCreatePbufferSurface(display, config, size.data());
    if (owner == EGL_NO_CONTEXT || read == EGL_NO_SURFACE || !eglMakeCurrent(display, draw, read, caller)) return 1;

    const auto new_buffer = [] {
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        return buffer;
    };
    const GLuint caller_buffer = new_buffer();
    GLuint owner_buffer = 0;
    const hyprimate::gl::EglContextId target{display, owner};
    using Retained = hyprimate::gl::NativeRetirement<NativeBuffer, Owner, hyprimate::gl::ScopedEglContext>;
    Retained retained;
    const Owner origin{target};
    EGLContext cached_destroyed = EGL_NO_CONTEXT, borrowed_destroyed = EGL_NO_CONTEXT;
    SP<NativeBuffer> cached, borrowed;
    {
        const hyprimate::gl::ScopedEglContext enter(target);
        if (!enter || eglGetCurrentContext() != owner) return 1;
        owner_buffer = new_buffer();
        cached = makeShared<NativeBuffer>(new_buffer(), cached_destroyed);
        borrowed = makeShared<NativeBuffer>(new_buffer(), borrowed_destroyed);
        retained.keep(cached, origin);
        CHECK(retained.current(cached));
    }
    // These independent object namespaces deliberately assign the same numeric name.
    if (!owner_buffer || owner_buffer != caller_buffer) return 1;
    CHECK(eglGetCurrentContext() == caller);
    CHECK(!retained.current(cached));
    const GLuint second_caller = new_buffer(), third_caller = new_buffer();
    if (second_caller != cached->name || third_caller != borrowed->name) return 1;
    // The native pool still owns this borrowed reference. Never pin it in order to retire ours.
    auto pool = borrowed;
    retained.drop_borrowed(borrowed, origin);
    CHECK(pool.strongRef() == 1);
    CHECK(eglGetCurrentSurface(EGL_READ) == read);
    CHECK(eglGetCurrentSurface(EGL_DRAW) == draw);

    std::binary_semaphore occupied(0), release(0);
    EGLBoolean worker_current = EGL_FALSE;
    std::thread worker([&] {
        worker_current = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, owner);
        occupied.release();
        release.acquire();
        if (worker_current) eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglReleaseThread();
    });
    occupied.acquire();
    CHECK(worker_current == EGL_TRUE);
    if (worker_current) {
        const hyprimate::gl::ScopedEglContext busy(target);
        cached.reset();
        retained.drop_borrowed(pool, origin); // Now the last reference; must defer deletion.
        retained.collect();
        CHECK(cached_destroyed == EGL_NO_CONTEXT && borrowed_destroyed == EGL_NO_CONTEXT);
        CHECK(glIsBuffer(second_caller) && glIsBuffer(third_caller));
        CHECK(!busy);
        if (busy) glDeleteBuffers(1, &owner_buffer);
        CHECK(glIsBuffer(caller_buffer));
        CHECK(eglGetCurrentContext() == caller);
    }
    release.release();
    worker.join();
    retained.collect();
    CHECK(cached_destroyed == owner && borrowed_destroyed == owner);
    CHECK(eglGetCurrentContext() == caller);
    CHECK(glIsBuffer(second_caller) && glIsBuffer(third_caller));
    glDeleteBuffers(1, &second_caller);
    glDeleteBuffers(1, &third_caller);

    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) return 1;
    {
        const hyprimate::gl::ScopedEglContext retry(target);
        if (!retry) return 1;
        CHECK(glIsBuffer(owner_buffer));
        glDeleteBuffers(1, &owner_buffer);
        CHECK(!glIsBuffer(owner_buffer));
    }
    CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT);
    if (!eglMakeCurrent(display, draw, draw, caller)) return 1;
    CHECK(glIsBuffer(caller_buffer));
    glDeleteBuffers(1, &caller_buffer);
    eglDestroySurface(display, read);
    eglDestroyContext(display, owner);
    return g_failures ? 1 : 0;
}
