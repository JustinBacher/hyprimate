#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>

#include "workspace/WorkspaceCapture.hpp"
#include "workspace/WorkspaceCapturePlan.hpp"

namespace hyprimate::workspace {


    bool capture_workspace(Render::IHyprRenderer &renderer,
        BeginRender begin,
        const PHLMONITOR &monitor,
        const SP<Render::IFramebuffer> &target,
        const SP<Render::CSceneResources> &resources,
        CaptureKind kind,
        CaptureDraw draw,
        void *userdata,
        CaptureAuthorize authorize) {
        if (!g_pSessionLockManager || g_pSessionLockManager->isSessionLocked() ||
            begin == nullptr || draw == nullptr || !monitor || !target || !target->isAllocated() || !resources ||
            !monitor->m_enabled || !monitor->m_output || monitor->m_drmFormat == DRM_FORMAT_INVALID ||
            monitor->m_transformedSize.x <= 0 || monitor->m_transformedSize.y <= 0 ||
            (kind == CaptureKind::SourceIsolated && !resources->isolated()) ||
            (kind == CaptureKind::DestinationLive &&
                (!monitor->resources() || resources != monitor->resources()->sceneResources())) ||
            renderer.context().active()) {
            return false;
        }
        const auto BACKEND = renderer.glBackend();
        if (renderer.type() != Render::IHyprRenderer::RT_GL || !BACKEND) {
            return false;
        }
        // Mutation hooks can run outside a rendering session.
        BACKEND->makeEGLCurrent();

        CRegion damage{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        Render::SRenderOptions options;
        options.sceneResources = resources;
        try {
            const bool STARTED = begin(&renderer,
                monitor,
                damage,
                Render::RENDER_MODE_FULL_FAKE,
                {},
                target,
                true,
                options,
                nullptr);
            if (!STARTED) {
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
            std::optional<WorkspaceCaptureScope::DestinationLiveScope> live_capture;
            if (kind == CaptureKind::DestinationLive) {
                live_capture.emplace(context);
            }
            struct AbortOnExit {
                Render::IHyprRenderer *renderer;
                explicit AbortOnExit(Render::IHyprRenderer &value) : renderer(&value) {}
                AbortOnExit(const AbortOnExit &) = delete;
                AbortOnExit &operator=(const AbortOnExit &) = delete;
                AbortOnExit(AbortOnExit &&) = delete;
                AbortOnExit &operator=(AbortOnExit &&) = delete;
                ~AbortOnExit() {
                    if (renderer->context().active()) {
                        renderer->abortRender();
                    }
                }
            };
            const AbortOnExit ABORT{renderer};
            // DestinationLive replaces the native child scene, so its surface draws and
            // discards deliver feedback once for this output frame. In Hyprland 0.57,
            // CHyprGLRenderer::endRender merges FULL_FAKE buffer uses into pending uses;
            // the following normal endRender synchronizes those uses with the output.
            context.m_blockSurfaceFeedback = blocks_surface_feedback(kind);
            renderer.draw(context, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
            renderer.startRenderPass(context);
            draw(userdata, context);
            return submit_capture(
                [&] { return authorize == nullptr || authorize(userdata); },
                [&] {
                    renderer.endRender();
                    return !renderer.context().active();
                });
        } catch (...) {
            // A failure before the guard is constructed may still have opened a session.
            if (renderer.context().active()) {
                renderer.abortRender();
            }
            return false;
        }
    }

}  // namespace hyprimate::workspace
