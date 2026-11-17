#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "AnimationCloseRenderer.hpp"
#include "AnimationTransformer.hpp"
#include "WindowTracker.hpp"
#include "animations/bubble/BubbleResidue.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "config_generated.hpp"

// Owns every compositor-facing lifetime for the plugin: config values, event
// listeners, per-window tracking, and the transformers it attaches. Constructed
// transactionally during PLUGIN_INIT; its destructor detaches every window.
class CPluginRuntime {
  public:
    explicit CPluginRuntime(HANDLE handle);
    ~CPluginRuntime();

    CPluginRuntime(const CPluginRuntime &) = delete;
    CPluginRuntime &operator=(const CPluginRuntime &) = delete;
    CPluginRuntime(CPluginRuntime &&) = delete;
    CPluginRuntime &operator=(CPluginRuntime &&) = delete;

    friend struct SLiquidLiveStyle;
    friend struct SPaperLiveStyle;
    friend struct SClothLiveStyle;
    friend struct SSwarmLiveStyle;
    friend struct SBubbleLiveStyle;

  private:
    using Clock = std::chrono::steady_clock;

    struct STracked {
        PHLWINDOWREF window;
        hyprimate::WindowTracker tracker;
        CBox footprint;  // world-space damage footprint of the last deformation
        bool has_footprint = false;
        Clock::time_point last_update =
            Clock::now();  // previous update; stall and physics elapsed run from it
        std::optional<hyprimate::Edge>
            open_edge;  // edge the opening began from; close_side=origin drains here
    };

    bool plugin_enabled() const;
    bool movement_enabled() const;
    bool open_enabled() const;
    bool close_enabled() const;
    double movement_strength() const;
    hyprimate::MaterialParams effective_material_params() const;
    hyprimate::ClothMovementParams cloth_movement_params() const;
    bool material_usable() const;
    bool material_changed(const CAnimationTransformer &transformer) const;
    bool wobble_enabled() const;
    void notify(const std::string &text) const;
    void on_reload();

    // Tracking.
    STracked *find(const PHLWINDOW &window);
    void track(const PHLWINDOW &window);
    void forget(const PHLWINDOW &window);
    static void rebaseline(STracked &entry, const PHLWINDOW &window);
    void baseline_all();
    bool eligible(const PHLWINDOW &window) const;

    // Edge pop-out placement and configuration.
    static bool workspace_has_other_windows(const PHLWINDOW &window);
    void gather_neighbours(const PHLWINDOW &window) const;
    void gather_cloth_obstacles(const PHLWINDOW &window) const;
    hyprimate::EdgeInput edge_input_for(const PHLWINDOW &window,
        bool opening,
        std::optional<hyprimate::Edge> origin = std::nullopt) const;
    // Screen sides a swarm enters from (opening) or leaves through: the configured sides for a
    // window alone on its workspace, otherwise the sides it appears from.
    hyprimate::EdgeSet swarm_sides(const PHLWINDOW &window, bool opening) const;

    // Sampling and simulation.
    void on_render_pre(const PHLMONITOR &monitor);
    void update_window(STracked &entry, const PHLWINDOW &window, Clock::time_point now);
    hyprimate::WindowSnapshot snapshot_of(const PHLWINDOW &window, bool animating, double elapsed);
    void record_movement(STracked &entry,
        const PHLWINDOW &window,
        const hyprimate::TrackDecision &decision,
        double elapsed);
    void step_window(STracked &entry, const PHLWINDOW &window, double elapsed);
    void begin_opening(STracked &entry, const PHLWINDOW &window);

    // Mutations. Each one damages the previous and restored footprints.
    static CAnimationTransformer *transformer_of(const PHLWINDOW &window);
    CAnimationTransformer *acquire_transformer(const PHLWINDOW &window);
    CBox world_footprint(const PHLWINDOW &window) const;
    static void schedule_animation(const PHLWINDOW &window);
    void detach_window(STracked &entry, const PHLWINDOW &window);
    void detach_all();
    bool bubble_residue_enabled() const;
    void clear_bubble_residue();
    void restore_bubble_scanout(const PHLMONITOR &monitor);
    SSwarmStyle *workspace_swarm_tail(const PHLWINDOW &window) const;
    bool has_workspace_swarm_tail(const PHLMONITOR &monitor) const;
    void queue_workspace_swarm_tails(Render::CRenderContext &ctx, const PHLMONITOR &monitor);

    // Must outlive close records and every transformer detached during destruction.
    hyprimate::ClothPhysics m_cloth_physics;

    HANDLE m_handle = nullptr;
    hyprimate::config::SConfigValues m_values;
    hyprimate::config::SConfig m_config;
    CConfigValue<Config::INTEGER> m_wobble{"decoration:wobble:enabled"};

    std::vector<STracked> m_tracked;
    bool m_inert = false;
    SP<CSolidFill> m_open_solid = makeShared<CSolidFill>();
    SP<CSolidFill> m_close_solid = makeShared<CSolidFill>();
    SP<CPaperShade> m_shade = makeShared<CPaperShade>();
    SP<CSwarmSprites> m_sprites = makeShared<CSwarmSprites>();
    SP<CBubbleRenderer> m_bubble = makeShared<CBubbleRenderer>();
    SP<CBubbleResidue> m_bubble_residue = makeShared<CBubbleResidue>();
    std::vector<PHLMONITORREF> m_residue_inhibited;
    std::unique_ptr<CAnimationCloseRenderer> m_close;
    mutable std::vector<hyprimate::Box>
        m_neighbours;  // tiled neighbours for the contact rule; rebuilt per edge choice
    // Next per-window movement seed; random per load (set in the constructor), so loads do not
    // replay characters.
    std::uint64_t m_next_seed = 0;

    CHyprSignalListener m_listen_open_late;
    CHyprSignalListener m_listen_close;
    CHyprSignalListener m_listen_destroy;
    CHyprSignalListener m_listen_render_pre_checks;
    CHyprSignalListener m_listen_render_pre;
    CHyprSignalListener m_listen_render_post;
    CHyprSignalListener m_listen_reload;
    CHyprSignalListener m_listen_monitor_layout;
    CHyprSignalListener m_listen_monitor_removed;
};
