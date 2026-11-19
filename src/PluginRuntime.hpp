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

#define HYPRIMATE_FRIEND(kind, tag, module) friend struct tag;
    HYPRIMATE_STYLE_LIST(HYPRIMATE_FRIEND, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_FRIEND

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
    bool style_selected(hyprimate::config::Style style) const;
    bool workspace_movement_enabled() const;
    bool open_enabled() const;
    bool close_enabled() const;
    double movement_strength() const;
    SStyleState initial_style(std::uint64_t seed) const;
    bool wobble_enabled() const;
    void notify(const std::string &text) const;
    void on_reload();
    void prepare_styles(bool warm_selected);

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
    void gather_contact_obstacles(const PHLWINDOW &window) const;
    hyprimate::EdgeInput edge_input_for(const PHLWINDOW &window,
        bool opening,
        std::optional<hyprimate::Edge> origin = std::nullopt) const;

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
    static void detach_window(STracked &entry, const PHLWINDOW &window);
    void detach_all();
    bool screen_needs_frame(const PHLMONITOR &monitor) const;
    void clear_screen_content();
    void restore_screen_scanout(const PHLMONITOR &monitor);
    std::optional<CBox> screen_window_capture(
        const PHLWINDOW &window, const CAnimationTransformer *transformer) const;

    // Resources and physics outlive all close records and detached transformers.
    SSharedStyleResources m_shared_style_resources;
    SStyleResources<StyleModules> m_style_resources{m_shared_style_resources};

    HANDLE m_handle = nullptr;
    hyprimate::config::SConfigValues m_values;
    hyprimate::config::SConfig m_config;
    CConfigValue<Config::INTEGER> m_wobble{"decoration:wobble:enabled"};

    std::vector<STracked> m_tracked;
    bool m_inert = false;
    std::vector<PHLMONITORREF> m_screen_inhibited;
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
