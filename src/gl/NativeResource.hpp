#pragma once

#include "gl/CompositorContext.hpp"

namespace Render {
    class IFramebuffer;
    class ITexture;
    class CSceneResources;
}

namespace hyprimate::gl {
    // Only for freshly plugin-created objects, in their creating context. Returning the original
    // smart pointer preserves host identity, casts and equality. Never pin a work-buffer pool entry.
    [[nodiscard]] SP<Render::IFramebuffer> retain_native(SP<Render::IFramebuffer>);
    [[nodiscard]] SP<Render::ITexture> retain_native(SP<Render::ITexture>);
    [[nodiscard]] SP<Render::CSceneResources> retain_native(SP<Render::CSceneResources>);
    [[nodiscard]] bool native_current(const SP<Render::IFramebuffer> &) noexcept;

    // For a borrowed native object retained by a cache or queued pass. Do not extend a pool
    // lease if another reference (including the pool itself) can take over its lifetime.
    void release_native(SP<Render::IFramebuffer> &, const CompositorContextOwner &) noexcept;
    void collect_native_resources() noexcept;
    void finish_native_resources() noexcept;
} // namespace hyprimate::gl
