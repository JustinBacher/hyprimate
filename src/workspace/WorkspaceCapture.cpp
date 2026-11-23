#include <exception>

#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/FramebufferAllocationState.hpp"
#include "gl/NativeResource.hpp"
#include "workspace/WorkspaceCapture.hpp"
#include "workspace/WorkspaceCapturePlan.hpp"
#include "workspace/WorkspaceProfiling.hpp"

namespace hyprimate::workspace {

    bool capture_workspace(Render::IHyprRenderer &renderer,
        BeginRender begin,
        const PHLMONITOR &monitor,
        SP<Render::IFramebuffer> target,
        const SP<Render::CSceneResources> &resources,
        CaptureKind kind,
        CaptureDraw draw,
        void *userdata,
        CaptureAuthorize authorize) try {
        if (!g_pSessionLockManager || g_pSessionLockManager->isSessionLocked() ||
            begin == nullptr || draw == nullptr || !monitor || !target || !target->isAllocated() ||
            !resources || !monitor->m_enabled || !monitor->m_output ||
            monitor->m_drmFormat == DRM_FORMAT_INVALID || monitor->m_transformedSize.x <= 0 ||
            monitor->m_transformedSize.y <= 0 ||
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
        // Mutation hooks can run outside a rendering session. Restore the caller's
        // exact EGL display, context and read/draw surfaces after the native pass.
        const gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT) {
            return false;
        }
        if (!gl::native_current(target) ||
            (resources->isolated() && !gl::native_current(resources->blurFramebuffer()))) {
            return false;
        }
        // begin/end execute native GL work; draw() itself may only enqueue it.
        // Keep the query inside the compositor context and across pass execution.
        const ProfileGpuScope GPU;

        CRegion damage{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
        Render::SRenderOptions options;
        options.sceneResources = resources;
        // Hyprland rolls back a partial begin itself. On refusal there is no
        // session to abort: a reentrant caller may already own the context.
        {
            const gl::FramebufferAllocationState ALLOCATION;
            if (!begin(&renderer,
                    monitor,
                    damage,
                    Render::RENDER_MODE_FULL_FAKE,
                    {},
                    target,
                    true,
                    options,
                    nullptr)) {
                return false;
            }
        }

        auto &context = renderer.context();
        const WorkspaceCaptureScope capture{context, kind, *target, *options.sceneResources};
        const auto OWNS = [&] { return capture.owns(context); };
        return run_capture_pass(
            OWNS,
            [&] {
                // DestinationLive delivers surface feedback once for this output;
                // FULL_FAKE transfers sampled buffers to the next normal submission.
                context.m_blockSurfaceFeedback = blocks_surface_feedback(kind);
                renderer.draw(context, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
                renderer.startRenderPass(context);
                draw(userdata, context);
                return authorize == nullptr || authorize(userdata);
            },
            [&] {
                renderer.endRender();
                return !context.active();
            },
            [&] { renderer.abortRender(); });
    } catch (const std::exception &error) {
        LOG(Log::WARN,
            "[hyprimate] workspace capture failed kind={} monitor={}: {}",
            static_cast<int>(kind),
            monitor ? monitor->m_id : -1,
            error.what());
        return false;
    } catch (...) {
        LOG(Log::WARN,
            "[hyprimate] workspace capture failed kind={} monitor={}: unknown exception",
            static_cast<int>(kind),
            monitor ? monitor->m_id : -1);
        return false;
    }

}  // namespace hyprimate::workspace
