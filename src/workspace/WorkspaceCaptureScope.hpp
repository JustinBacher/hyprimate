#pragma once

#include <hyprland/src/render/Context.hpp>

#include "workspace/WorkspaceCapturePolicy.hpp"

// The host reuses its context object. Match the session's output and resources,
// not just its address, when classifying draws in a synchronous callback.
class WorkspaceCaptureScope {
  public:
    using Kind = hyprimate::workspace::CaptureKind;
    WorkspaceCaptureScope(Render::CRenderContext &ctx,
        Kind kind,
        const Render::IFramebuffer &output,
        const Render::CSceneResources &resources) noexcept
        : m_context(&ctx),
          m_output(&output),
          m_resources(&resources),
          m_kind(kind),
          m_previous(s_current) {
        s_current = this;
    }
    ~WorkspaceCaptureScope() {
        s_current = m_previous;
    }
    WorkspaceCaptureScope(const WorkspaceCaptureScope &) = delete;
    WorkspaceCaptureScope &operator=(const WorkspaceCaptureScope &) = delete;
    WorkspaceCaptureScope(WorkspaceCaptureScope &&) = delete;
    WorkspaceCaptureScope &operator=(WorkspaceCaptureScope &&) = delete;

    bool owns(const Render::CRenderContext &ctx) const {
        return m_context == &ctx && ctx.active() && ctx.m_mode == Render::RENDER_MODE_FULL_FAKE &&
               ctx.m_data.outFB.get() == m_output && ctx.sceneResources().get() == m_resources;
    }
    static WorkspaceCaptureScope *for_context(const Render::CRenderContext &ctx) {
        auto *capture = current(ctx);
        return capture && hyprimate::workspace::blocks_surface_feedback(capture->m_kind)
                   ? capture
                   : nullptr;
    }
    static bool is_destination_live(const Render::CRenderContext &ctx) {
        const auto *capture = current(ctx);
        return capture && capture->m_kind == Kind::DestinationLive;
    }

  private:
    static WorkspaceCaptureScope *current(const Render::CRenderContext &ctx) {
        for (auto *scope = s_current; scope != nullptr; scope = scope->m_previous) {
            if (scope->owns(ctx)) {
                return scope;
            }
        }
        return nullptr;
    }

    inline static thread_local WorkspaceCaptureScope *s_current = nullptr;
    Render::CRenderContext *m_context;
    const Render::IFramebuffer *m_output;
    const Render::CSceneResources *m_resources;
    Kind m_kind;
    WorkspaceCaptureScope *m_previous = nullptr;
};
