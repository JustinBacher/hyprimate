#include <cstdio>
#include <new>

#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "gl/NativeResource.hpp"
#include "gl/NativeRetirement.hpp"

namespace hyprimate::gl {
    namespace {
        template <class T>
        using Anchors = NativeRetirement<T, CompositorContextOwner, ScopedCompositorContext>;
        struct NativeResources {
            Anchors<Render::CSceneResources> scenes;
            Anchors<Render::IFramebuffer> framebuffers;
            Anchors<Render::ITexture> textures;

            void finish() noexcept {
                const bool gone = !Render::GL::g_pHyprOpenGL;
                // Destroy dependants before attachments. If the backend is gone, these three
                // host destructors skip GL. Do not assume the same of CShader.
                auto unavailable = scenes.finish(gone);
                unavailable += framebuffers.finish(gone);
                unavailable += textures.finish(gone);
                if (unavailable)
                    std::fprintf(stderr,
                        "hyprimate: retained %zu native GL references at unload: owner unavailable "
                        "or resource still in use\n",
                        unavailable);
            }
            ~NativeResources() {
                finish();
            }
        };
        NativeResources resources;

        template <class T>
        SP<T> retain(Anchors<T> &anchors, SP<T> resource) {
            CompositorContextOwner owner;
            if (!resource || !owner.ensure_current()) return nullptr;
            try {
                anchors.keep(resource, owner);
            } catch (const std::bad_alloc &) {
                return nullptr;
            }
            return resource;
        }
    }  // namespace
    SP<Render::CSceneResources> retain_native(SP<Render::CSceneResources> resource) {
        return retain(resources.scenes, std::move(resource));
    }
    SP<Render::IFramebuffer> retain_native(SP<Render::IFramebuffer> resource) {
        return retain(resources.framebuffers, std::move(resource));
    }
    SP<Render::ITexture> retain_native(SP<Render::ITexture> resource) {
        return retain(resources.textures, std::move(resource));
    }
    bool retire_replaced_sheet_shade(const SP<Render::ITexture> &resource,
        const CurrentCompositorContext &current) noexcept {
        return resources.textures.disarm_replaced(
            resource, current, [](Render::ITexture &texture) { texture.m_texID = 0; });
    }
    bool native_current(const SP<Render::IFramebuffer> &resource) noexcept {
        return resources.framebuffers.current(resource);
    }
    bool native_current(const SP<Render::IFramebuffer> &resource,
        const CurrentCompositorContext &current) noexcept {
        return resources.framebuffers.current(resource, current);
    }
    void release_native(
        SP<Render::IFramebuffer> &resource, const CompositorContextOwner &owner) noexcept {
        resources.framebuffers.drop_borrowed(resource, owner);
    }
    void collect_native_resources() noexcept {
        resources.scenes.collect();
        resources.framebuffers.collect();
        resources.textures.collect();
    }
    void finish_native_resources() noexcept {
        resources.finish();
    }
}  // namespace hyprimate::gl
