#pragma once

#include "EdgeTransition.hpp"
#include "LiquidGlassTransformer.hpp"
#include "LiquidMesh.hpp"
#include "SolidFill.hpp"

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Deforms the native window-close fadeout through two narrow hooks, each selected by exact demangled signature:
//   * Desktop::CWindowFadeout::create - associates the created fadeout with the staged closing window.
//   * Render::IHyprRenderer::renderFadeouts (MONITOR mode only) - substitutes a mesh element for associated fadeouts.
// A close that is not texture-blurred is owned, whatever the movement mode: its edge transition drives shape and
// placement from a retained snapshot, so it keeps drawing after the native fadeout has finished. Every other fadeout,
// scene mode, or missing association runs the original native code.
class CLiquidCloseRenderer {
  public:
    using clock = std::chrono::steady_clock;

    struct SClosing {
        WP<Desktop::IFadeout>                  fadeout;
        SP<Render::IFramebuffer>               snapshot;               // retained: an owned close may outlive the native fadeout
        PHLMONITORREF                          monitor;
        PHLWORKSPACEREF                        workspace;              // captured at creation
        bool                                   workspaceBound = false; // false: no workspace at creation, matches any (as native)
        Desktop::eFadeoutPlane                 plane         = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        int                                    zIndex        = 0;
        bool                                   noScreenShare = false;
        liquid_glass::LiquidMesh               mesh;
        liquid_glass::EdgeTransition           transition;
        liquid_glass::Vec2                     compensation{1.0, 1.0}; // fixed for the record's lifetime
        bool                                   owned       = false;    // transition drives shape and placement
        float                                  sourceAlpha = 1.F;
        double                                 strength    = 0.0;
        double                                 drive       = 0.0;
        CBox                                   sourceDevice;           // undeformed window box inside the snapshot, device px
        double                                 scale = 1.0;            // monitor scale at staging time
        liquid_glass::Vec2                     captureLogical{};
        Vector2D                               snapshotSize{};         // snapshot texture size, device px
        CBox                                   renderBox;              // owned placement: unit scale, centre follows native
        CBox                                   extentsDevice;          // deformed extents of sourceDevice, device px
        std::vector<SMeshRenderVertex>         vertices;               // normalized to extentsDevice; sized once
        bool                                   hasLiquid = false;      // edge liquid present this frame
        CBox                                   liquidDevice;           // edge liquid box inside the snapshot, device px
        std::vector<SMeshRenderVertex>         liquidVertices;         // normalized to liquidDevice; sized once
        clock::time_point                      lastAdvance{};
    };

    explicit CLiquidCloseRenderer(HANDLE handle, SP<CSolidFill> solid);
    ~CLiquidCloseRenderer();

    CLiquidCloseRenderer(const CLiquidCloseRenderer&)            = delete;
    CLiquidCloseRenderer& operator=(const CLiquidCloseRenderer&) = delete;

    // Installs both hooks. On failure every hook installed so far is removed and `error` is set.
    bool install(std::string& error);
    // Removes both hooks and drops every pending handoff and record. Safe when not installed.
    void shutdown();
    // Captures a closing window's pose (mesh, transition, compensation). A Close begun during an opening continues
    // from its travel; otherwise `closeFrame` picks the edge the window drains behind. Call from the window.close
    // listener, before the window is detached.
    // `closeSplash` plays the splash bump at the dive point after the drain (EdgeTransition::begin). `transition`
    // carries the close's own elasticity/momentum; the movement mesh's params never drive the close edge transition.
    void stage(const PHLWINDOW& window, const SLiquidPose& pose, double strength, std::optional<liquid_glass::EdgeFrame> closeFrame, double drive,
               bool closeSplash, const liquid_glass::MaterialParams& transition);

    // Advances closing records. Returns true while any record still animates; `activeMonitors` is refilled with the
    // monitors to damage, including the frame in which an owned close finishes or a record is pruned.
    bool tick(clock::time_point now, std::vector<PHLMONITORREF>& activeMonitors);

    // Drops pending handoffs and records (topology or config change).
    void clear();

    // Hook bodies. Called by the trampolines; never throw.
    void onFadeoutCreated(const PHLWINDOW& window, const SP<Desktop::CWindowFadeout>& created);
    bool hasRecordFor(const PHLMONITOR& monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE& workspace) const;
    // Submits the MONITOR-mode batch with associated closes deformed. Returns false, having submitted nothing,
    // when the batch exceeds the preallocated scratch; the caller then draws it natively.
    bool renderBatch(Render::CRenderContext& ctx, const PHLMONITOR& monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE& workspace);

    // Trampoline targets, valid only while installed.
    static CLiquidCloseRenderer* s_active;
    void* createOriginal() const { return m_createHook ? m_createHook->m_original : nullptr; }
    void* renderOriginal() const { return m_renderHook ? m_renderHook->m_original : nullptr; }

  private:
    struct SPending {
        PHLWINDOWREF                             window;
        liquid_glass::LiquidMesh                 mesh;
        liquid_glass::EdgeTransition             transition;
        liquid_glass::Vec2                       compensation{1.0, 1.0};
        CBox                                     sourceDevice;
        double                                   scale = 1.0;
        liquid_glass::Vec2                       captureLogical{};
        double                                   strength = 0.0;
        double                                   drive    = 0.0;
        std::optional<liquid_glass::EdgeFrame>   closeFrame;
        bool                                     closeSplash = true;
        liquid_glass::MaterialParams             closeParams{};
        clock::time_point                        staged{};
    };

    // One MONITOR-mode batch entry. `fadeout` is null for an owned record drawn after its native fadeout ended.
    struct SBatchItem {
        SP<Desktop::IFadeout> fadeout;
        SClosing*             record = nullptr;
        int                   zIndex = 0;
    };

    SClosing* recordFor(const SP<Desktop::IFadeout>& fadeout) const;
    void      fillVertices(SClosing& record) const;

    HANDLE                                 m_handle;
    SP<CSolidFill>                         m_solid;
    // Owned by the host hook system; created and removed only through HyprlandAPI.
    CFunctionHook*                         m_createHook = nullptr;
    CFunctionHook*                         m_renderHook = nullptr;
    std::vector<SPending>                  m_pending;
    std::vector<std::unique_ptr<SClosing>> m_records;
    // Reused per renderBatch call; capacity is reserved at install so steady-state draws do not allocate.
    std::vector<SBatchItem>                m_batch;
};

// Draws a deformed closing fadeout. Borrows vertices from its record; records are only removed from tick/clear,
// which run outside the render pass, so the borrowed pointer is valid for the whole frame. The host destroys every
// pass element when the frame ends, so no element of this type survives between frames.
class CLiquidClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<Render::ITexture>                  tex;
        CBox                                  box; // output box, monitor-local device px
        const std::vector<SMeshRenderVertex>* vertices = nullptr;
        float                                 alpha    = 1.F;
        double                                scale    = 1.0;
        CRegion                               damage;
        // Edge liquid: drawn before the window mesh, sampling `solid` over `liquid` vertices in `liquidBox`.
        SP<Render::ITexture>                  solid;
        const std::vector<SMeshRenderVertex>* liquid = nullptr;
        CBox                                  liquidBox;
    };

    explicit CLiquidClosePassElement(SData data);
    ~CLiquidClosePassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    bool                          needsLiveBlur(Render::CRenderContext& ctx) override;
    bool                          needsPrecomputeBlur(Render::CRenderContext& ctx) override;
    const char*                   passName() override;
    ePassElementType              type() override;
    std::optional<CBox>           boundingBox(Render::CRenderContext& ctx) override;
    CRegion                       opaqueRegion(Render::CRenderContext& ctx) override;

  private:
    SData m_data;
};
