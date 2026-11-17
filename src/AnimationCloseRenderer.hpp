#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "AnimationTransformer.hpp"
#include "animations/bubble/BubbleCloseStyleData.hpp"
#include "animations/cloth/ClothCloseStyleData.hpp"
#include "animations/liquid/LiquidCloseStyleData.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/paper/PaperCloseStyleData.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/SwarmCloseStyleData.hpp"
bool close_animation_enabled(const hyprimate::config::SConfig &config);
using SCloseStyleData = std::
    variant<SLiquidCloseData, SPaperCloseData, SClothCloseData, SSwarmCloseData, SBubbleCloseData>;

// Deforms the native window-close fadeout through two narrow hooks, each selected by exact
// demangled signature:
//   * Desktop::CWindowFadeout::create - associates the created fadeout with the staged closing
//   window.
//   * Render::IHyprRenderer::renderFadeouts (MONITOR mode only) - substitutes a mesh element for
//   associated fadeouts.
// A close that is not texture-blurred is owned, whatever the style: its edge transition (liquid) or
// sheet (paper, cloth) drives shape and placement from a retained snapshot, so it keeps drawing
// after the native fadeout has finished. Every other fadeout, scene mode, or missing association
// runs the original native code.
class CAnimationCloseRenderer {
  public:
    using Clock = std::chrono::steady_clock;

    struct SClosing {
        WP<Desktop::IFadeout> fadeout;
        SP<Render::IFramebuffer>
            snapshot;  // retained: an owned close may outlive the native fadeout
        PHLMONITORREF monitor;
        PHLWORKSPACEREF workspace;  // captured at creation
        bool workspace_bound = false;  // false: no workspace at creation, matches any (as native)
        Desktop::eFadeoutPlane plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        int z_index = 0;
        bool no_screen_share = false;
        SStyleState style;
        SCloseStyleData close_data;
        bool owned = false;  // the style's motion drives shape and placement
        float source_alpha = 1.F;
        CBox source_device;  // undeformed window box inside the snapshot, device px
        double scale = 1.0;  // monitor scale at staging time
        hyprimate::Vec2 capture_logical{};
        Vector2D snapshot_size;  // snapshot texture size, device px
        CBox render_box;  // owned placement: unit scale, centre follows native
        CBox extents_device;  // deformed extents of source_device, device px
        CBox drawn_box;  // owned: global logical box last drawn; empty when none
        Clock::time_point last_advance;
    };

    explicit CAnimationCloseRenderer(
        HANDLE handle, SP<CSolidFill> solid, SP<CPaperShade> shade, SP<CSwarmSprites> sprites);
    ~CAnimationCloseRenderer();

    CAnimationCloseRenderer(const CAnimationCloseRenderer &) = delete;
    CAnimationCloseRenderer &operator=(const CAnimationCloseRenderer &) = delete;
    CAnimationCloseRenderer(CAnimationCloseRenderer &&) = delete;
    CAnimationCloseRenderer &operator=(CAnimationCloseRenderer &&) = delete;

    // Installs both hooks. On failure every hook installed so far is removed and `error` is set.
    bool install(std::string &error);
    // Removes both hooks and drops every pending handoff and record. Safe when not installed.
    void shutdown();
    // The style owns the close handoff; a matching live state may be moved from before detachment.
    void stage(const PHLWINDOW &window,
        SStyleState &state,
        bool state_active,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams material,
        const std::function<hyprimate::EdgeInput()> &edge_input,
        const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &next_seed,
        const std::function<hyprimate::EdgeSet()> &swarm_sides);

    // Called before `rendering` draws a frame. Prunes every finished record and advances the
    // records drawn on `rendering`. `damage` is refilled with the global logical boxes (what
    // Hyprland's damageBox takes) the owned records occupied before and after this frame,
    // deduplicated; unowned native-fadeout records contribute none. A record that stops drawing
    // (finished or pruned) contributes its last drawn box.
    void tick(Clock::time_point now, const PHLMONITOR &rendering, std::vector<CBox> &damage);

    // Owned closes need post-render frame requests until their last pixels have been cleared.
    [[nodiscard]] bool needs_frame(const PHLMONITOR &monitor) const;

    // Drops pending handoffs and records (topology or config change).
    void clear();

    // Hook bodies. Called by the trampolines; never throw.
    void on_fadeout_created(const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created);
    [[nodiscard]] bool has_record_for(const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane,
        const PHLWORKSPACE &workspace) const;
    // Submits the MONITOR-mode batch with associated closes deformed. Returns false, having
    // submitted nothing, when the batch exceeds the preallocated scratch; the caller then draws it
    // natively.
    bool render_batch(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane,
        const PHLWORKSPACE &workspace);

    // Trampoline targets, valid only while installed.
    static CAnimationCloseRenderer *s_active;
    [[nodiscard]] void *create_original() const {
        return (m_create_hook != nullptr) ? m_create_hook->m_original : nullptr;
    }
    [[nodiscard]] void *render_original() const {
        return (m_render_hook != nullptr) ? m_render_hook->m_original : nullptr;
    }

    struct SPending {
        PHLWINDOWREF window;
        SStyleState style;
        SCloseStyleData close_data;
        CBox source_device;
        double scale = 1.0;
        hyprimate::Vec2 capture_logical{};
        Clock::time_point staged;
    };

  private:
    // One MONITOR-mode batch entry. `fadeout` is null for an owned record drawn after its native
    // fadeout ended.
    struct SBatchItem {
        SP<Desktop::IFadeout> fadeout;
        SClosing *record = nullptr;
        int z_index = 0;
    };

    [[nodiscard]] SClosing *record_for(const SP<Desktop::IFadeout> &fadeout) const;
    // Refills the record's extents, edge liquid box, and meshes from its current state; clears
    // vertices_stale. Runs the gain search, so callers invoke it once per advance.
    static void fill_vertices(SClosing &record);
    [[nodiscard]] static std::optional<SPending> pending_for(const PHLWINDOW &window);
    void push(SPending pending);

    HANDLE m_handle;
    SP<CSolidFill> m_solid;
    SP<CPaperShade> m_shade;
    SP<CSwarmSprites> m_sprites;
    // Owned by the host hook system; created and removed only through HyprlandAPI.
    CFunctionHook *m_create_hook = nullptr;
    CFunctionHook *m_render_hook = nullptr;
    std::vector<SPending> m_pending;
    std::vector<std::unique_ptr<SClosing>> m_records;
    // Reused per render_batch call; capacity is reserved at install so steady-state draws do not
    // allocate.
    std::vector<SBatchItem> m_batch;
};
