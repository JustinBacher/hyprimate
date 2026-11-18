#pragma once

#include <unordered_map>
#include <unordered_set>

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>

#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "animations/swarm/SwarmLiveState.hpp"

// A source capture is a native MONITOR composition, not a screencopy. Keep all
// plugin-owned render bookkeeping separate from the live frame, even when a
// native callback nests another render context.
class WorkspaceCaptureScope {
  public:
    // Owned by the controller across captures, and released on reload/unload or GL reset.
    struct Scratch {
        SP<CBubbleRenderer> bubbleRenderer;
        void warm_up() {
            if (!bubbleRenderer) {
                bubbleRenderer = makeShared<CBubbleRenderer>();
            }
            bubbleRenderer->warm_up();
        }
        void clear() {
            bubbleRenderer.reset();
        }
    };

    WorkspaceCaptureScope(Render::CRenderContext &ctx, Scratch &scratch)
        : m_context(&ctx), m_previous(s_current), m_scratch(scratch) {
        s_current = this;
    }
    ~WorkspaceCaptureScope() {
        s_current = m_previous;
    }
    WorkspaceCaptureScope(const WorkspaceCaptureScope &) = delete;
    WorkspaceCaptureScope &operator=(const WorkspaceCaptureScope &) = delete;

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

      private:
        friend class WorkspaceCaptureScope;
        Render::CRenderContext *m_context;
        DestinationLiveScope *m_previous;
    };

    static bool isDestinationLive(const Render::CRenderContext &ctx) {
        for (auto *scope = s_live; scope; scope = scope->m_previous) {
            if (scope->m_context == &ctx) {
                return true;
            }
        }
        return false;
    }

    // The address of the live slot identifies even an as-yet unallocated wallpaper.
    // Outside a capture this returns the live slot and preserves existing ownership.
    static SP<SBubbleWallpaper> &wallpaper(
        Render::CRenderContext &ctx, SP<SBubbleWallpaper> &live) {
        auto *scope = forContext(ctx);
        if (!scope) {
            return live;
        }
        auto &local = scope->m_wallpapers[&live];
        if (!local) {
            local = makeShared<SBubbleWallpaper>();
        }
        return local;
    }

    static SP<CBubbleRenderer> renderer(
        Render::CRenderContext &ctx, const SP<CBubbleRenderer> &live) {
        auto *scope = forContext(ctx);
        if (!scope || !live) {
            return live;
        }
        auto &local = scope->m_scratch.bubbleRenderer;
        if (!local) {
            local = makeShared<CBubbleRenderer>();
        }
        return local;
    }

    // Pass elements may borrow this until the isolated render pass has finished.
    static SSwarmMeshes &swarmMeshes(Render::CRenderContext &ctx, SSwarmMeshes &live) {
        auto *scope = forContext(ctx);
        return scope ? scope->m_swarmMeshes[&live] : live;
    }

    bool wakeQueued(const void *wake) const {
        return m_wakes.contains(wake);
    }
    void markWakeQueued(const void *wake) {
        m_wakes.insert(wake);
    }

  private:
    inline static thread_local DestinationLiveScope *s_live = nullptr;
    inline static thread_local WorkspaceCaptureScope *s_current = nullptr;
    Render::CRenderContext *m_context;
    WorkspaceCaptureScope *m_previous;
    std::unordered_map<const void *, SP<SBubbleWallpaper>> m_wallpapers;
    Scratch &m_scratch;
    std::unordered_map<const void *, SSwarmMeshes> m_swarmMeshes;
    std::unordered_set<const void *> m_wakes;
};
