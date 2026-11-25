#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "PluginRuntime.hpp"
#include "config_generated.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/IndexedMesh.hpp"
#include "gl/NativeResource.hpp"
#include "perf/LiveProfiler.hpp"
#ifdef HYPRIMATE_DIAGNOSTICS
#include "perf/GpuStopwatch.hpp"
#endif
#include "perf/RuntimeProfiler.hpp"
#include "shader/hyprland/ShaderWallpaper.hpp"
#include "window/WindowAnimations.hpp"
#include "window/Profiling.hpp"
#include "window/hyprland/AnimationCloseRenderer.hpp"
#include "window/hyprland/AnimationTransformer.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/hyprland/HyprWindowPort.hpp"
#include "window/hyprland/HyprglassCompat.hpp"
#include "window/hyprland/StyleTypes.hpp"
#include "window/shared/SheetResources.hpp"
#include "workspace/WorkspaceAnimation.hpp"

namespace {
    constexpr const char *WOBBLE_MESSAGE =
        "[hyprimate] Disable decoration:wobble:enabled before enabling hyprimate";

    std::uint64_t initial_seed() {
        std::random_device entropy;
        return (std::uint64_t{entropy()} << 32U) | entropy();
    }

}  // namespace

class CPluginRuntime::Impl {
  public:
    explicit Impl(HANDLE handle);
    ~Impl();
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

  private:
    void configure(bool refresh_effects = false);
    void update_inert();
    void warm_style();
    void configure_workspace(bool force = false);
    void configure_wallpaper(bool force = false);
    void reload();
    void render_pre(const PHLMONITOR &monitor);
    void render_stage(const Event::SRenderStageEvent &event);
    void restore_scanout();
    void notify(const std::string &text) const;

    SSharedStyleResources m_shared;
    hyprimate::animation::StyleResources<ProductionStyles> m_resources{m_shared};
    CHyprWindowPort m_port;
    HyprWindowAnimations m_animations{m_port, m_resources, initial_seed()};
    HANDLE m_handle;
    hyprimate::config::SConfigValues m_values;
    hyprimate::config::SConfig m_config;
    CConfigValue<Config::INTEGER> m_wobble{"decoration:wobble:enabled"};
    bool m_inert = false;
    bool m_close_available = false;
    bool m_warm_pending = true;
    std::optional<decltype(m_config.style)> m_warmed_style;
    hyprimate::animation::FrameId m_frame = 0;
    std::vector<PHLMONITORREF> m_inhibited;
    std::unique_ptr<CAnimationCloseRenderer> m_close;
    std::unique_ptr<CHyprglassCompat> m_glass;
    std::unique_ptr<CWorkspaceAnimation> m_workspace;
    hyprimate::shader::ShaderWallpaper m_wallpaper{m_port};
    std::optional<hyprimate::config::SWallpaperSettings> m_wallpaper_request;
    std::unique_ptr<hyprimate::perf::RuntimeProfiler> m_profiler;
    CHyprSignalListener m_open, m_close_event, m_destroy, m_pre_checks, m_pre, m_stage;
    CHyprSignalListener m_reload, m_config_refresh, m_monitor_layout, m_monitor_removed;
    std::optional<hyprimate::gl::IndexedMeshRedirect> m_indexed_mesh;
};

CPluginRuntime::Impl::Impl(HANDLE handle) : m_handle(handle) {
    m_port.bind(m_animations);
    hyprimate::config::registerConfigValues(m_handle, m_values);
    m_config = hyprimate::config::readConfig(m_values);
    if (m_config.enabled && *m_wobble != 0) {
        notify(WOBBLE_MESSAGE);
        throw std::runtime_error(WOBBLE_MESSAGE);
    }
    m_close = std::make_unique<CAnimationCloseRenderer>(m_handle, m_animations, m_port);
    std::string error;
    m_close_available = m_close->install(error);
    if (!m_close_available) {
        notify("[hyprimate] close transitions unavailable: " + error);
    }
    m_workspace = std::make_unique<CWorkspaceAnimation>(m_handle);
    m_workspace->set_prepare_capture([this](const PHLMONITOR &monitor) {
        if (!m_config.duringWorkspaceSwitch && monitor) {
            m_animations.prepare_workspace_capture(m_port.monitor_identity(monitor));
        }
    });
    error.clear();
    if (!m_workspace->install(error)) {
        notify("[hyprimate] workspace transitions unavailable: " + error);
    }
    m_glass = std::make_unique<CHyprglassCompat>(m_handle);
    configure_workspace();
    configure_wallpaper();
    configure();

    m_open = Event::bus()->m_events.window.openLate.listen([this](const PHLWINDOW &window) {
        if (window) {
            m_animations.track(PHLWINDOWREF{window});
        }
    });
    m_close_event = Event::bus()->m_events.window.close.listen([this](const PHLWINDOW &window) {
        if (window) {
            m_animations.close(
                m_port.identity(PHLWINDOWREF{window}), hyprimate::animation::Clock::now());
        }
    });
    m_destroy = Event::bus()->m_events.window.destroy.listen(
        [this](const PHLWINDOWREF &window) { m_animations.destroyed(m_port.identity(window)); });
    m_pre_checks =
        Event::bus()->m_events.render.preChecks.listen([this](const PHLMONITOR &monitor) {
            m_workspace->on_render_pre_checks(monitor);
            if (!monitor || !m_animations.needs_frame(m_port.monitor_identity(monitor)) ||
                monitor->m_solitaryClient.expired()) {
                return;
            }
            if (std::ranges::none_of(
                    m_inhibited, [&](const auto &item) { return item.lock() == monitor; })) {
                m_inhibited.emplace_back(monitor);
            }
            monitor->m_solitaryClient.reset();
        });
    m_monitor_layout = Event::bus()->m_events.monitor.layoutChanged.listen([this] {
        m_workspace->clear();
        for (const auto &monitor : State::monitorState()->monitors()) {
            m_animations.monitor_changed(m_port.monitor_identity(monitor));
        }
        restore_scanout();
    });
    m_monitor_removed =
        Event::bus()->m_events.monitor.preRemoved.listen([this](const PHLMONITOR &monitor) {
            m_workspace->remove_monitor(monitor);
            m_port.forget_workspace_switch(monitor);
            m_animations.monitor_changed(m_port.monitor_identity(monitor));
            restore_scanout();
        });
    m_pre = Event::bus()->m_events.render.pre.listen(
        [this](const PHLMONITOR &monitor) { render_pre(monitor); });
    m_stage = Event::bus()->m_events.render.stage.listen(
        [this](const Event::SRenderStageEvent &event) { render_stage(event); });
    m_reload = Event::bus()->m_events.config.reloaded.listen([this] { reload(); });
    // In 0.57 dynamic hl.config() writes schedule a property refresh even for plugin keys
    // with no compositor refresh bits. Don't read/parse every style's settings every frame.
    m_config_refresh = Event::bus()->m_events.config.props_refreshed.listen([this](bool) {
        m_config = hyprimate::config::readConfig(m_values);
        update_inert();
        configure_workspace();
        configure_wallpaper();
        configure();
    });
    for (const auto &window : Desktop::windowState()->windows()) {
        if (window) {
            m_animations.track(PHLWINDOWREF{window});
        }
    }
    m_profiler = std::make_unique<hyprimate::perf::RuntimeProfiler>(m_handle, m_port,
        [this](std::uint64_t monitor) { m_animations.profile_frame_presented(monitor); });
    m_indexed_mesh.emplace();
    if (hyprimate::gl::indexed_mesh_mode() != hyprimate::gl::IndexedMeshMode::Off) {
        std::string indexed_error;
        if (!m_indexed_mesh->install(indexed_error)) {
            std::fprintf(stderr, "[hyprimate] indexed mesh unavailable: %s\n", indexed_error.c_str());
        }
    }
}

CPluginRuntime::Impl::~Impl() {
#ifdef HYPRIMATE_DIAGNOSTICS
    {
        // Timestamp-query results are read at teardown so a profiled run reports GPU time of the
        // regions it stopwatched; the context must be current to read and free the queries.
        const hyprimate::gl::ScopedCompositorContext CONTEXT{};
        if (CONTEXT) {
            const auto REPORT = hyprimate::perf::gpu_stopwatch_report();
            if (!REPORT.empty()) std::fputs(REPORT.c_str(), stderr);
            hyprimate::perf::gpu_stopwatch_release();
        }
    }
#endif
    m_indexed_mesh.reset();
    m_profiler.reset();
    m_open.reset();
    m_close_event.reset();
    m_destroy.reset();
    m_pre_checks.reset();
    m_pre.reset();
    m_stage.reset();
    m_reload.reset();
    m_config_refresh.reset();
    m_monitor_layout.reset();
    m_monitor_removed.reset();
    // The host owns queued pass elements, including their plugin vtables and framebuffer
    // leases. Remove ours before destroying the animation owners or unloading this library.
    if (g_pHyprRenderer) {
        auto &context = g_pHyprRenderer->context();
        auto &current = Render::IHyprRenderer::currentPass(context);
        for (const auto *name : {"hyprimate-native-close",
                 "hyprimate-window-animation",
                 "hyprimate-burn-ground",
                 "hyprimate-bubble",
                 "hyprimate-bubble-wallpaper",
                 "hyprimate-workspace-wallpaper",
                 "hyprimate-workspace-native-hud-blur",
                 "hyprimate-workspace-presentation"}) {
            current.removeAllOfType(name);
            if (&current != &context.m_pass) {
                context.m_pass.removeAllOfType(name);
            }
        }
    }
    m_workspace->shutdown();
    m_wallpaper.shutdown();
    m_glass.reset();
    m_close->unhook();
    m_animations.detach_transformers();
    m_animations.end_all();
    for (const auto &entry : m_inhibited) {
        if (const auto MONITOR = entry.lock()) {
            MONITOR->recheckSolitary();
        }
    }
}

void CPluginRuntime::Impl::notify(const std::string &text) const {
    HyprlandAPI::addNotification(m_handle, text, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
}

void CPluginRuntime::Impl::configure(bool refresh_effects) {
    m_warm_pending = !m_warmed_style || *m_warmed_style != m_config.style;
    constexpr std::array EVENT_NAMES{"open: ", "close: ", "move: ", "resize: "};
    std::array<std::optional<hyprimate::config::Style>, EVENT_NAMES.size()> event_styles{};
    m_resources.for_each([&]<class Style>(auto &resources) {
        if constexpr (requires { Style::refresh(resources, Style::requests(m_config), refresh_effects); }) {
            const auto errors = Style::refresh(resources, Style::requests(m_config), refresh_effects);
            for (std::size_t index = 0; index < errors.size(); ++index) {
                if (!errors[index].empty()) {
                    notify(std::string{"[hyprimate] external effect "} +
                        EVENT_NAMES[index] + errors[index]);
                }
            }
        } else {
            m_animations.template update_settings<Style>(Style::settings(m_config));
        }
        if constexpr (requires { Style::event_overrides(resources); }) {
            const auto overrides = Style::event_overrides(resources);
            for (std::size_t index = 0; index < overrides.size(); ++index) {
                if (overrides[index]) event_styles[index] = Style::KIND;
            }
        }
    });
    m_animations.configure({.style = m_config.style,
        .open_style = event_styles[0],
        .close_style = event_styles[1],
        .move_style = event_styles[2],
        .resize_style = event_styles[3],
        .enabled = m_config.enabled,
        .inert = m_inert,
        .opening = m_config.open.enabled,
        .movement = m_config.movement.enabled,
        .closing = m_config.close.enabled,
        .workspace_movement = m_config.movement.workspaceMovement,
        .during_workspace_switch = m_config.duringWorkspaceSwitch,
        .close_available = m_close_available});
}

void CPluginRuntime::Impl::warm_style() {
    bool complete = true;
    m_resources.for_each([&]<class Style>(auto &resources) {
        if constexpr (requires { Style::warm(resources); }) {
            if (m_config.style == Style::KIND) {
                const hyprimate::perf::CpuScope PROFILE{
                    hyprimate::animation::profile_key(Style::KIND), hyprimate::perf::Phase::Setup};
                complete = Style::warm(resources);
            }
        }
    });
    if (complete) {
        m_warmed_style = m_config.style;
        m_warm_pending = false;
    }
}

void CPluginRuntime::Impl::configure_workspace(bool force) {
    m_workspace->configure(m_config.workspace, force);
}

void CPluginRuntime::Impl::configure_wallpaper(bool force) {
    const auto &options = m_config.wallpaper;
    if (force || m_wallpaper_request != options) {
        m_wallpaper.configure(options.effect, options.effectParameters, options.effectDuration,
            options.sourceNamespace);
        m_wallpaper_request = options;
    }
}

void CPluginRuntime::Impl::update_inert() {
    const bool INERT = m_config.enabled && *m_wobble != 0;
    if (INERT && !m_inert) {
        notify(WOBBLE_MESSAGE);
    }
    m_inert = INERT;
}

void CPluginRuntime::Impl::reload() {
    const auto started = std::chrono::steady_clock::now();
    m_config = hyprimate::config::readConfig(m_values);
    update_inert();
    configure_workspace(true);
    configure_wallpaper(true);
    m_animations.reload();
    configure(true);
    restore_scanout();
    LOG(Log::DEBUG, "[hyprimate] plugin reload_us={}",
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
}

void CPluginRuntime::Impl::render_pre(const PHLMONITOR &monitor) {
    m_glass->refresh();
    hyprimate::gl::collect_native_resources();
    if (m_warm_pending) {
        warm_style();
    }
    restore_scanout();
    if (monitor) {
        if (!m_config.duringWorkspaceSwitch) {
            m_port.update_workspace_switch(monitor, m_workspace->turn_active(monitor));
        }
        m_animations.frame({.monitor = m_port.monitor_identity(monitor),
            .frame = ++m_frame,
            .now = hyprimate::animation::Clock::now()});
    }
    m_workspace->on_render_pre(monitor);
}

void CPluginRuntime::Impl::render_stage(const Event::SRenderStageEvent &event) {
    if (!event.monitor) {
        return;
    }
    const auto MONITOR = m_port.monitor_identity(event.monitor);
    if (event.stage == RENDER_POST_WALLPAPER && event.context) {
        CHyprAnimationCanvas canvas{event.context->get(), m_port.canvas_resources()};
        m_animations.before_windows(MONITOR, canvas);
    } else if (event.stage == RENDER_PRE_WINDOW && event.context) {
        auto &ctx = event.context->get();
        if (const auto WINDOW = ctx.m_data.currentWindow.lock()) {
            if (m_close_available && ctx.m_renderingSnapshot) {
                m_close->note_monitor_snapshot(WINDOW, event.monitor, ctx);
            }
            CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources()};
            m_animations.before_window(m_port.identity(PHLWINDOWREF{WINDOW}), canvas, MONITOR, m_frame);
            if (m_glass->available()) {
                CAnimationTransformer::prepare_window_underlay(WINDOW, ctx);
            }
        }
    } else if (event.stage == RENDER_POST_WINDOWS && event.context) {
        CHyprAnimationCanvas canvas{event.context->get(), m_port.canvas_resources()};
        m_animations.after_windows(MONITOR, canvas);
    } else if (m_close_available && event.stage == RENDER_BEGIN && event.context) {
        m_close->begin_monitor_frame(event.monitor, event.context->get());
    } else if (m_close_available && event.stage == RENDER_LAST_MOMENT && event.context) {
        m_close->finish_monitor_frame(event.monitor, event.context->get());
    } else if (event.stage == RENDER_POST && m_animations.needs_frame(MONITOR)) {
        event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }
}

void CPluginRuntime::Impl::restore_scanout() {
    std::erase_if(m_inhibited, [this](const auto &entry) {
        const auto MONITOR = entry.lock();
        if (!MONITOR) {
            return true;
        }
        if (m_animations.needs_frame(m_port.monitor_identity(MONITOR))) {
            return false;
        }
        MONITOR->recheckSolitary();
        return true;
    });
}

CPluginRuntime::CPluginRuntime(HANDLE handle) : m_impl(std::make_unique<Impl>(handle)) {
}
CPluginRuntime::~CPluginRuntime() {
    m_impl.reset();  // Drop caches and pending animations before collecting their host references.
    hyprimate::gl::finish_native_resources();
}
