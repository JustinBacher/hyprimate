#pragma once

#include <array>
#include <cstdint>

#include "Check.hpp"
#include "gl/NativeRetirement.hpp"
#include "window/shared/SheetShadeCache.hpp"

namespace sheet_shade_test {
    using hyprimate::gl::ContextOwnerState;
    using hyprimate::gl::EglContextId;
    inline EglContextId backend;

    struct Current {
        EglContextId id = backend.current() ? backend : EglContextId{};
    };
    struct Owner {
        EglContextId id;
        bool ensure_current() {
            if (!backend.current()) return false;
            if (!id.valid()) id = backend;
            return id == backend;
        }
        ContextOwnerState classify_current(const Current &current) const {
            return hyprimate::gl::detail::classify_context_owner(id, current.id, id == current.id);
        }
        void reset() { id = {}; }
    };
    struct Scope {
        Scope() : scope(backend) {}
        explicit Scope(const Owner &owner)
            : scope(owner.id == backend ? owner.id : EglContextId{}) {}
        explicit operator bool() const { return static_cast<bool>(scope); }
        hyprimate::gl::ScopedEglContext scope;
    };
    // Stand-in for the host wrapper; deletion is real GL, but the host destructor is not linked.
    struct Texture {
        GLuint m_texID = 0;
        EGLContext *destroyed_in = nullptr;
        unsigned *delete_calls = nullptr;
        ~Texture() {
            if (destroyed_in) *destroyed_in = eglGetCurrentContext();
            if (m_texID) {
                if (delete_calls) ++*delete_calls;
                glDeleteTextures(1, &m_texID);
            }
        }
    };
    using Anchors = hyprimate::gl::NativeRetirement<Texture, Owner, Scope>;
    using Cache = hyprimate::gl::SheetShadeCache<Texture, Owner, Current, Scope>;

    inline std::array<unsigned char, 4> pixel(GLuint texture, int x) {
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        std::array<unsigned char, 4> result{};
        glReadPixels(x, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        return result;
    }

    inline void run(EGLDisplay display, EGLSurface surface, EGLConfig config,
        EGLContext a, EGLContext b) {
        const std::array attrs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        const EGLContext c = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs.data());
        if (c == EGL_NO_CONTEXT) { CHECK(false); return; }
        EGLContext stale_destroyed_in = EGL_NO_CONTEXT;
        unsigned stale_delete_calls = 0;
        Anchors anchors;
        unsigned uploads = 0;
        auto create = [&](std::uint8_t *pixels, int size) {
            auto texture = makeShared<Texture>();
            glGenTextures(1, &texture->m_texID);
            glBindTexture(GL_TEXTURE_2D, texture->m_texID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            ++uploads;
            return texture;
        };
        auto retain = [&](SP<Texture> texture) {
            Owner owner;
            if (!owner.ensure_current()) return SP<Texture>{};
            anchors.keep(texture, owner);
            return texture;
        };
        auto retire = [&](const SP<Texture> &texture, const Current &current) {
            return anchors.disarm_replaced(texture, current,
                [](Texture &stale) { stale.m_texID = 0; });
        };
        const auto sample = [](GLuint texture) {
            CHECK((pixel(texture, 1) == std::array<unsigned char, 4>{0, 0, 0, 153}));
            CHECK((pixel(texture, 128) == std::array<unsigned char, 4>{0, 0, 0, 0}));
            CHECK((pixel(texture, 255) == std::array<unsigned char, 4>{115, 115, 115, 115}));
        };
        {
            Cache cache;
            backend = {display, a};
            CHECK(eglMakeCurrent(display, surface, surface, a));
            auto old = cache.texture(create, retain, retire);
            if (!old) { CHECK(false); eglDestroyContext(display, c); return; }
            const GLuint old_name = old->m_texID;
            old->destroyed_in = &stale_destroyed_in;
            old->delete_calls = &stale_delete_calls;
            WP<Texture> weak{old};
            sample(old_name);
            CHECK(uploads == 1);
            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
            CHECK(!cache.texture(create, retain, retire));
            CHECK(old->m_texID == old_name && uploads == 1);
            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, b));
            CHECK(!cache.texture(create, retain, retire));
            CHECK(old->m_texID == old_name && uploads == 1);
            CHECK(eglMakeCurrent(display, surface, surface, a));
            CHECK(cache.texture(create, retain, retire) == old);
            CHECK(uploads == 1);

            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, b));
            backend = {display, b};
            GLuint sentinel = 0;
            glGenTextures(1, &sentinel);
            glBindTexture(GL_TEXTURE_2D, sentinel);
            const std::array<unsigned char, 4> sentinel_pixel{23, 67, 111, 255};
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, sentinel_pixel.data());
            auto fresh = cache.texture(create, retain, retire);
            CHECK(fresh && old->m_texID == 0 && uploads == 2);
            if (fresh) {
                sample(fresh->m_texID);
                CHECK(cache.texture(create, retain, retire) == fresh);
                CHECK(uploads == 2);
            }
            anchors.collect();
            CHECK(!weak.expired());
            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
            backend = {display, c};
            old.reset();
            CHECK(!weak.expired());
            CHECK(stale_destroyed_in == EGL_NO_CONTEXT && stale_delete_calls == 0);
            anchors.collect();
            CHECK(weak.expired());
            CHECK(stale_destroyed_in == c && stale_delete_calls == 0);
            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, b));
            backend = {display, b};
            CHECK(glIsTexture(sentinel));
            CHECK((pixel(sentinel, 0) == sentinel_pixel));
            glDeleteTextures(1, &sentinel);
            fresh.reset();
        }
        anchors.collect();
        CHECK(eglMakeCurrent(display, surface, surface, a));
        eglDestroyContext(display, c);
    }
}  // namespace sheet_shade_test
