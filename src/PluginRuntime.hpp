#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include "LiquidGlassTransformer.hpp"
#include "WindowTracker.hpp"
#include "LiquidCloseRenderer.hpp"
#include "SolidFill.hpp"
#include "config_generated.hpp"

#include <chrono>
#include <cstdint>
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

    CPluginRuntime(const CPluginRuntime &) = delete;
    CPluginRuntime &operator=(const CPluginRuntime &) = delete;

  private:
    using clock = std::chrono::steady_clock;

    struct STracked {
        PHLWINDOWREF                window;
        liquid_glass::WindowTracker tracker;
        CBox                        footprint; // world-space damage footprint of the last deformation
        bool                        hasFootprint = false;
        clock::time_point           lastUpdate = clock::now(); // previous update; stall and physics elapsed run from it
        std::optional<liquid_glass::Edge> openEdge; // edge the opening began from; close_side=origin drains here
    };

    bool                              pluginEnabled() const;
    bool                              movementEnabled() const;
    bool                              openEnabled() const;
    bool                              closeEnabled() const;
    double                            movementStrength() const;
    liquid_glass::MaterialParams      effectiveMaterialParams() const;
    liquid_glass::ClothMovementParams clothMovementParams() const;
    bool                              materialUsable() const;
    bool                              materialChanged(const CLiquidGlassTransformer &transformer) const;
    bool                              wobbleEnabled() const;
    void                              notify(const std::string &text) const;
    void                              onReload();

    // Tracking.
    STracked *find(const PHLWINDOW &window);
    void      track(const PHLWINDOW &window);
    void      forget(const PHLWINDOW &window);
    void      rebaseline(STracked &entry, const PHLWINDOW &window) const;
    void      baselineAll();
    bool      eligible(const PHLWINDOW &window) const;

    // Edge pop-out placement and configuration.
    liquid_glass::Edge                     firstWindowEdge() const;
    bool                                   workspaceHasOtherWindows(const PHLWINDOW &window) const;
    void                                   gatherNeighbours(const PHLWINDOW &window) const;
    void                                   gatherClothObstacles(const PHLWINDOW &window) const;
    CBox                                   edgeBoundsBox(const PHLMONITOR &monitor, bool opening) const;
    std::optional<liquid_glass::EdgeFrame> edgeFrameFor(const PHLWINDOW &window, bool opening,
                                                        std::optional<liquid_glass::Edge> origin = std::nullopt) const;

    // Sampling and simulation.
    void                         onRenderPre(const PHLMONITOR &monitor);
    void                         updateWindow(STracked &entry, const PHLWINDOW &window, clock::time_point now);
    liquid_glass::WindowSnapshot snapshotOf(const PHLWINDOW &window, bool animating, double elapsed);
    void recordMovement(STracked &entry, const PHLWINDOW &window, const liquid_glass::TrackDecision &decision,
                        double elapsed);
    void stepWindow(STracked &entry, const PHLWINDOW &window, double elapsed);
    void beginOpening(STracked &entry, const PHLWINDOW &window);

    // Mutations. Each one damages the previous and restored footprints.
    CLiquidGlassTransformer *transformerOf(const PHLWINDOW &window) const;
    CLiquidGlassTransformer *acquireTransformer(const PHLWINDOW &window);
    CBox                     worldFootprint(const PHLWINDOW &window) const;
    void                     scheduleAnimation(const PHLWINDOW &window) const;
    void                     detachWindow(STracked &entry, const PHLWINDOW &window);
    void                     detachAll();

    // Must outlive close records and every transformer detached during destruction.
    liquid_glass::ClothPhysics m_clothPhysics;

    HANDLE                              m_handle = nullptr;
    liquid_glass::config::SConfigValues m_values;
    liquid_glass::config::SConfig       m_config;
    CConfigValue<Config::INTEGER>       m_wobble{"decoration:wobble:enabled"};

    std::vector<STracked>                 m_tracked;
    bool                                  m_inert = false;
    SP<CSolidFill>                        m_openSolid = makeShared<CSolidFill>();
    SP<CSolidFill>                        m_closeSolid = makeShared<CSolidFill>();
    SP<CPaperShade>                       m_shade = makeShared<CPaperShade>();
    std::unique_ptr<CLiquidCloseRenderer> m_close;
    mutable std::vector<liquid_glass::Box>
        m_neighbours; // tiled neighbours for the contact rule; rebuilt per edge choice
    // Next per-window movement seed; random per load (set in the constructor), so loads do not replay characters.
    std::uint64_t m_nextSeed = 0;

    CHyprSignalListener m_listenOpenLate;
    CHyprSignalListener m_listenClose;
    CHyprSignalListener m_listenDestroy;
    CHyprSignalListener m_listenRenderPre;
    CHyprSignalListener m_listenRenderPost;
    CHyprSignalListener m_listenReload;
};
