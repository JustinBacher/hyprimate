#pragma once

#include <hyprland/src/render/Context.hpp>

// A source capture is a native MONITOR composition, not a screencopy. Keep all
// plugin-owned render bookkeeping separate from the live frame, even when a
// native callback nests another render context.
class WorkspaceCaptureScope {
  public:
    explicit WorkspaceCaptureScope(Render::CRenderContext &ctx)
        : m_context(&ctx), m_previous(s_current) {
        s_current = this;
    }
    ~WorkspaceCaptureScope() {
        s_current = m_previous;
    }
    WorkspaceCaptureScope(const WorkspaceCaptureScope &) = delete;
    WorkspaceCaptureScope &operator=(const WorkspaceCaptureScope &) = delete;
    WorkspaceCaptureScope(WorkspaceCaptureScope &&) = delete;
    WorkspaceCaptureScope &operator=(WorkspaceCaptureScope &&) = delete;

    static WorkspaceCaptureScope *forContext(const Render::CRenderContext &ctx) {
        for (auto *scope = s_current; scope; scope = scope->m_previous) {
            if (scope->m_context == &ctx) {
                return scope;
            }
        }
        return nullptr;
    }

    // The live destination replaces the monitor scene, while other FULL_FAKE sessions remain
    // read-only. Keep this marker alive until the destination pass finishes executing.
    class DestinationLiveScope {
      public:
        explicit DestinationLiveScope(Render::CRenderContext &ctx)
            : m_context(&ctx), m_previous(s_live) {
            s_live = this;
        }
        ~DestinationLiveScope() {
            s_live = m_previous;
        }
        DestinationLiveScope(const DestinationLiveScope &) = delete;
        DestinationLiveScope &operator=(const DestinationLiveScope &) = delete;
        DestinationLiveScope(DestinationLiveScope &&) = delete;
        DestinationLiveScope &operator=(DestinationLiveScope &&) = delete;

      private:
        friend class WorkspaceCaptureScope;
        Render::CRenderContext *m_context;
        DestinationLiveScope *m_previous;
    };

    static bool isDestinationLive(const Render::CRenderContext &ctx) {
        for (const auto *scope = s_live; scope != nullptr; scope = scope->m_previous) {
            if (scope->m_context == &ctx) {
                return true;
            }
        }
        return false;
    }

  private:
    inline static thread_local DestinationLiveScope *s_live = nullptr;
    inline static thread_local WorkspaceCaptureScope *s_current = nullptr;
    Render::CRenderContext *m_context;
    WorkspaceCaptureScope *m_previous;
};
