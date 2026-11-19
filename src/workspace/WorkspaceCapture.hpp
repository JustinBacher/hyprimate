#pragma once

#include <optional>

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>

#include "workspace/WorkspaceCapturePolicy.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace hyprimate::workspace {

    // Exact ABI of the protected Hyprland 0.57 beginRender overload. The bridge resolves it by
    // exported signature; calling the public full-fake convenience method would use live caches.
    using BeginRender = bool (*)(Render::IHyprRenderer *,
        PHLMONITOR,
        CRegion &,
        Render::eRenderMode,
        SP<IHLBuffer>,
        SP<Render::IFramebuffer>,
        bool,
        const Render::SRenderOptions &,
        std::optional<Monitor::CDamageRing::CTransaction> *);
    using CaptureDraw = void (*)(void *, Render::CRenderContext &);

    bool captureWorkspace(Render::IHyprRenderer &renderer,
        BeginRender begin,
        const PHLMONITOR &monitor,
        const SP<Render::IFramebuffer> &target,
        const SP<Render::CSceneResources> &resources,
        CaptureKind kind,
        CaptureDraw draw,
        void *userdata);

}  // namespace hyprimate::workspace
