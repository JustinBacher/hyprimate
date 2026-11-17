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
bool closeAnimationEnabled(const hyprimate::config::SConfig &config);
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
    using clock = std::chrono::steady_clock;

    struct SClosing {
        WP<Desktop::IFadeout> fadeout;
        SP<Render::IFramebuffer>
            snapshot;  // retained: an owned close may outlive the native fadeout
        PHLMONITORREF monitor;
        PHLWORKSPACEREF workspace;  // captured at creation
        bool workspaceBound = false;  // false: no workspace at creation, matches any (as native)
        Desktop::eFadeoutPlane plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        int zIndex = 0;
        bool noScreenShare = false;
        SStyleState style;
        SCloseStyleData closeData;
        bool owned = false;  // the style's motion drives shape and placement
        float sourceAlpha = 1.F;
        CBox sourceDevice;  // undeformed window box inside the snapshot, device px
        double scale = 1.0;  // monitor scale at staging time
        hyprimate::Vec2 captureLogical{};
        Vector2D snapshotSize{};  // snapshot texture size, device px
        CBox renderBox;  // owned placement: unit scale, centre follows native
        CBox extentsDevice;  // deformed extents of sourceDevice, device px
        CBox drawnBox;  // owned: global logical box last drawn; empty when none
        clock::time_point lastAdvance{};
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
        bool stateActive,
        const hyprimate::config::SConfig &config,
        hyprimate::MaterialParams material,
        const std::function<hyprimate::EdgeInput()> &edgeInput,
        const std::function<std::span<const hyprimate::Box>()> &obstacles,
        const std::function<std::uint64_t()> &nextSeed,
        const std::function<hyprimate::EdgeSet()> &swarmSides);

    // Called before `rendering` draws a frame. Prunes every finished record and advances the
    // records drawn on `rendering`. `damage` is refilled with the global logical boxes (what
    // Hyprland's damageBox takes) the owned records occupied before and after this frame,
    // deduplicated; unowned native-fadeout records contribute none. A record that stops drawing
    // (finished or pruned) contributes its last drawn box.
    void tick(clock::time_point now, const PHLMONITOR &rendering, std::vector<CBox> &damage);

    // Owned closes need post-render frame requests until their last pixels have been cleared.
    bool needsFrame(const PHLMONITOR &monitor) const;

    // Drops pending handoffs and records (topology or config change).
    void clear();

    // Hook bodies. Called by the trampolines; never throw.
    void onFadeoutCreated(const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created);
    bool hasRecordFor(const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane,
        const PHLWORKSPACE &workspace) const;
    // Submits the MONITOR-mode batch with associated closes deformed. Returns false, having
    // submitted nothing, when the batch exceeds the preallocated scratch; the caller then draws it
    // natively.
    bool renderBatch(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        Desktop::eFadeoutPlane plane,
        const PHLWORKSPACE &workspace);

    // Trampoline targets, valid only while installed.
    static CAnimationCloseRenderer *s_active;
    void *createOriginal() const {
        return (m_createHook != nullptr) ? m_createHook->m_original : nullptr;
    }
    void *renderOriginal() const {
        return (m_renderHook != nullptr) ? m_renderHook->m_original : nullptr;
    }

    struct SPending {
        PHLWINDOWREF window;
        SStyleState style;
        SCloseStyleData closeData;
        CBox sourceDevice;
        double scale = 1.0;
        hyprimate::Vec2 captureLogical{};
        clock::time_point staged{};
    };

  private:
    // One MONITOR-mode batch entry. `fadeout` is null for an owned record drawn after its native
    // fadeout ended.
    struct SBatchItem {
        SP<Desktop::IFadeout> fadeout;
        SClosing *record = nullptr;
        int zIndex = 0;
    };

    SClosing *recordFor(const SP<Desktop::IFadeout> &fadeout) const;
    // Refills the record's extents, edge liquid box, and meshes from its current state; clears
    // verticesStale. Runs the gain search, so callers invoke it once per advance.
    void fillVertices(SClosing &record);
    std::optional<SPending> pendingFor(const PHLWINDOW &window) const;
    void push(SPending pending);

    HANDLE m_handle;
    SP<CSolidFill> m_solid;
    SP<CPaperShade> m_shade;
    SP<CSwarmSprites> m_sprites;
    // Owned by the host hook system; created and removed only through HyprlandAPI.
    CFunctionHook *m_createHook = nullptr;
    CFunctionHook *m_renderHook = nullptr;
    std::vector<SPending> m_pending;
    std::vector<std::unique_ptr<SClosing>> m_records;
    // Reused per renderBatch call; capacity is reserved at install so steady-state draws do not
    // allocate.
    std::vector<SBatchItem> m_batch;
};
