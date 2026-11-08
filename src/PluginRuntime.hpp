#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include "LiquidGlassTransformer.hpp"
#include "LiquidCloseRenderer.hpp"
#include "SolidFill.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Owns every compositor-facing lifetime for the plugin: config values, event
// listeners, per-window tracking, and the transformers it attaches. Constructed
// transactionally during PLUGIN_INIT; its destructor detaches every window.
class CPluginRuntime {
  public:
    explicit CPluginRuntime(HANDLE handle);
    ~CPluginRuntime();

    CPluginRuntime(const CPluginRuntime&)            = delete;
    CPluginRuntime& operator=(const CPluginRuntime&) = delete;

  private:
    using clock = std::chrono::steady_clock;

    struct STracked {
        PHLWINDOWREF  window;
        PHLMONITORREF monitor;
        CBox          lastBox;              // last sampled current geometry (world space)
        Vector2D      lastCaptureSize;      // decorated, untransformed capture domain
        CBox          footprint;            // world-space damage footprint of the last deformation
        bool          hasFootprint   = false;
        bool          baselined      = false;
        bool          wasAnimatingIn = false;
        bool          wasEligible    = false;
        clock::time_point lastSample = clock::now();
        clock::time_point lastAdvance = clock::now();
        std::optional<liquid_glass::Edge> openEdge; // edge the opening began from; close_side=origin drains here
    };

    // Config and wobble conflict.
    bool                          pluginEnabled() const;
    double                        effectiveStrength() const;
    double                        effectiveViscosity() const;
    liquid_glass::MotionMode      effectiveMode() const;
    liquid_glass::MaterialParams  effectiveMaterialParams() const;
    bool                          transitionsEnabled() const;
    bool                          materialUsable() const;
    bool                          materialChanged(const liquid_glass::LiquidMesh& mesh) const;
    bool                          wobbleEnabled() const;
    void                          notify(const std::string& text) const;
    void                          onReload();

    // Tracking.
    STracked* find(const PHLWINDOW& window);
    void      track(const PHLWINDOW& window);
    void      forget(const PHLWINDOW& window);
    void      baseline(STracked& entry, const PHLWINDOW& window) const;
    void      baselineAll();
    bool      eligible(const PHLWINDOW& window) const;

    // Edge pop-out placement and configuration.
    liquid_glass::Edge                     firstWindowEdge() const;
    bool                                   workspaceHasOtherWindows(const PHLWINDOW& window) const;
    void                                   gatherNeighbours(const PHLWINDOW& window) const;
    CBox                                   edgeBoundsBox(const PHLMONITOR& monitor) const;
    std::optional<liquid_glass::EdgeFrame> edgeFrameFor(const PHLWINDOW& window, bool opening, std::optional<liquid_glass::Edge> origin = std::nullopt) const;
    double                                 effectiveEmergeDrag() const;
    CHyprColor                             effectiveLiquidColor() const;

    // Sampling and simulation.
    void onRenderPre();
    void onTick();
    void sampleWindow(STracked& entry, const PHLWINDOW& window);
    void advanceWindow(STracked& entry, const PHLWINDOW& window, clock::time_point now);
    void beginOpening(STracked& entry, const PHLWINDOW& window);

    // Mutations. Each one damages the previous and restored footprints.
    CLiquidGlassTransformer* transformerOf(const PHLWINDOW& window) const;
    CLiquidGlassTransformer* acquireTransformer(const PHLWINDOW& window);
    CBox                     worldFootprint(const PHLWINDOW& window) const;
    void                     scheduleAnimation(const PHLWINDOW& window) const;
    void                     detachWindow(STracked& entry, const PHLWINDOW& window);
    void                     detachAll();

    HANDLE                            m_handle = nullptr;
    SP<Config::Values::CBoolValue>    m_enabled;
    SP<Config::Values::CFloatValue>   m_strength;
    SP<Config::Values::CFloatValue>   m_viscosity;
    SP<Config::Values::CFloatValue>   m_elasticity;
    SP<Config::Values::CFloatValue>   m_momentum;
    SP<Config::Values::CBoolValue>    m_transitions;
    SP<Config::Values::CBoolValue>    m_closeSplash;
    SP<Config::Values::CStringValue>  m_mode;
    SP<Config::Values::CStringValue>  m_firstWindowSide;
    SP<Config::Values::CStringValue>  m_edgeBounds;
    SP<Config::Values::CStringValue>  m_closeSide;
    SP<Config::Values::CColorValue>   m_liquidColor;
    SP<Config::Values::CFloatValue>   m_emergeDrag;
    CConfigValue<Config::INTEGER>     m_wobble{"decoration:wobble:enabled"};

    std::vector<STracked>                   m_tracked;
    bool                                    m_inert = false;
    SP<CSolidFill>                          m_solid = makeShared<CSolidFill>();
    std::unique_ptr<CLiquidCloseRenderer>   m_close;
    std::vector<PHLMONITORREF>              m_closeMonitors;
    mutable std::vector<liquid_glass::Box> m_neighbours; // tiled neighbours for the contact rule; rebuilt per edge choice

    CHyprSignalListener m_listenOpenLate;
    CHyprSignalListener m_listenClose;
    CHyprSignalListener m_listenDestroy;
    CHyprSignalListener m_listenTick;
    CHyprSignalListener m_listenRenderPre;
    CHyprSignalListener m_listenReload;
};
