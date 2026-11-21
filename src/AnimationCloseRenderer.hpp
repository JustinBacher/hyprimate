#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "HyprWindowAnimations.hpp"
#include "CloseBatch.hpp"
#include "WindowAnimationEntry.hpp"
#include "WindowAnimationPort.hpp"

class CHyprWindowPort;

// Associates native fadeouts with module-owned animations and preserves native batch ordering.
class CAnimationCloseRenderer {
  public:
    CAnimationCloseRenderer(HANDLE handle, HyprWindowAnimations &animations, CHyprWindowPort &port);
    ~CAnimationCloseRenderer();

    CAnimationCloseRenderer(const CAnimationCloseRenderer &) = delete;
    CAnimationCloseRenderer &operator=(const CAnimationCloseRenderer &) = delete;
    CAnimationCloseRenderer(CAnimationCloseRenderer &&) = delete;
    CAnimationCloseRenderer &operator=(CAnimationCloseRenderer &&) = delete;

    bool install(std::string &error);
    // Removes only hooks; entry destruction belongs to the module's explicit unload phase.
    void unhook();

    void on_fadeout_created(const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created);
    [[nodiscard]] bool has_record_for(const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) const;
    bool render_batch(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace);

    static CAnimationCloseRenderer *s_active;
    [[nodiscard]] void *create_original() const {
        return m_create_hook != nullptr ? m_create_hook->m_original : nullptr;
    }
    [[nodiscard]] void *render_original() const {
        return m_render_hook != nullptr ? m_render_hook->m_original : nullptr;
    }

  private:
    struct Record {
        std::uint64_t fadeout = 0;
        PHLMONITORREF monitor;
        WP<Desktop::IFadeout> native;
        PHLWORKSPACEREF workspace;
        bool workspace_bound = false;
        Desktop::eFadeoutPlane plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        int z_index = 0;
        bool no_screen_share = false;
    };
    struct BatchItem {
        SP<Desktop::IFadeout> fadeout;
        std::uint64_t fadeout_id = 0;
        std::optional<hyprimate::animation::DyingView> view;
        bool suppressed = false;
        hyprimate::animation::CloseBatchKey key;
    };
    void prune_records();
    template<class Emit>
    void collect_batch(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace, Emit &&emit);
    void draw_item(Render::CRenderContext &ctx, const PHLMONITOR &monitor, const BatchItem &item);

    HANDLE m_handle;
    HyprWindowAnimations &m_animations;
    CHyprWindowPort &m_port;
    CFunctionHook *m_create_hook = nullptr;
    CFunctionHook *m_render_hook = nullptr;
    std::vector<Record> m_records;
};
