#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "workspace/WorkspaceCapture.hpp"

namespace hyprimate::workspace {

    bool captureWorkspace(Render::IHyprRenderer &renderer,
        BeginRender begin,
        const PHLMONITOR &monitor,
        const SP<Render::IFramebuffer> &target,
        const SP<Render::CSceneResources> &resources,
        CaptureKind kind,
        CaptureDraw draw,
        void *userdata) {
        if (!begin || !draw || !monitor || !target || !target->isAllocated() || !resources ||
            (kind == CaptureKind::SourceIsolated && !resources->isolated()) ||
            (kind == CaptureKind::DestinationLive &&
                (!monitor->resources() || resources != monitor->resources()->sceneResources())) ||
            renderer.context().active()) {
            return false;
        }
        const auto backend = renderer.glBackend();
        if (renderer.type() != Render::IHyprRenderer::RT_GL || !backend) {
            return false;
        }
        // Mutation hooks can run outside a rendering session.
        backend->makeEGLCurrent();

        CRegion damage{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        Render::SRenderOptions options;
        options.sceneResources = resources;
        try {
            const bool started = begin(&renderer,
                monitor,
                damage,
                Render::RENDER_MODE_FULL_FAKE,
                {},
                target,
                true,
                options,
                nullptr);
            if (!started) {
                if (renderer.context().active()) {
                    renderer.abortRender();
                }
                return false;
            }

            auto &context = renderer.context();
            std::optional<WorkspaceCaptureScope> capture;
            if (kind == CaptureKind::SourceIsolated) {
                capture.emplace(context);
            }
            std::optional<WorkspaceCaptureScope::DestinationLiveScope> liveCapture;
            if (kind == CaptureKind::DestinationLive) {
                liveCapture.emplace(context);
            }
            struct AbortOnExit {
                Render::IHyprRenderer &renderer;
                ~AbortOnExit() {
                    if (renderer.context().active()) {
                        renderer.abortRender();
                    }
                }
            } abort{renderer};
            // DestinationLive replaces the native child scene, so its surface draws and
            // discards deliver feedback once for this output frame. In Hyprland 0.57,
            // CHyprGLRenderer::endRender merges FULL_FAKE buffer uses into pending uses;
            // the following normal endRender synchronizes those uses with the output.
            context.m_blockSurfaceFeedback = blocks_surface_feedback(kind);
            renderer.draw(context, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
            renderer.startRenderPass(context);
            draw(userdata, context);
            renderer.endRender();
            return !renderer.context().active();
        } catch (...) {
            // A failure before the guard is constructed may still have opened a session.
            if (renderer.context().active()) {
                renderer.abortRender();
            }
            return false;
        }
    }

}  // namespace hyprimate::workspace
