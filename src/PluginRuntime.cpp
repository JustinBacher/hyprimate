#include "PluginRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "AnimationCloseRenderer.hpp"
#include "HyprAnimationCanvas.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimations.hpp"
#include "animations/shared/SheetResources.hpp"
#include "config_generated.hpp"
#include "workspace/WorkspaceAnimation.hpp"

namespace {
constexpr const char *WOBBLE_MESSAGE =
    "[hyprimate] Disable decoration:wobble:enabled before enabling hyprimate";

std::uint64_t initial_seed() {
    std::random_device entropy;
    return (std::uint64_t{entropy()} << 32U) | entropy();
}

}

class CPluginRuntime::Impl {
  public:
    explicit Impl(HANDLE handle);
    ~Impl();
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

  private:
    void configure(bool warm);
    void configure_workspace();
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
    hyprimate::animation::FrameId m_frame = 0;
    std::vector<PHLMONITORREF> m_inhibited;
    std::unique_ptr<CAnimationCloseRenderer> m_close;
    std::unique_ptr<CWorkspaceAnimation> m_workspace;
    CHyprSignalListener m_open, m_close_event, m_destroy, m_pre_checks, m_pre, m_stage;
    CHyprSignalListener m_reload, m_monitor_layout, m_monitor_removed;
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
    error.clear();
    if (!m_workspace->install(error)) {
        notify("[hyprimate] workspace transitions unavailable: " + error);
    }
    configure_workspace();
    configure(false);

    m_open = Event::bus()->m_events.window.openLate.listen([this](const PHLWINDOW &window) {
        if (window) {
            m_animations.track(PHLWINDOWREF{window});
        }
    });
    m_close_event = Event::bus()->m_events.window.close.listen([this](const PHLWINDOW &window) {
        if (window) {
            m_animations.close(m_port.identity(PHLWINDOWREF{window}),
                hyprimate::animation::Clock::now());
        }
    });
    m_destroy = Event::bus()->m_events.window.destroy.listen([this](const PHLWINDOWREF &window) {
        m_animations.destroyed(m_port.identity(window));
    });
    m_pre_checks = Event::bus()->m_events.render.preChecks.listen([this](const PHLMONITOR &monitor) {
        m_workspace->on_render_pre_checks(monitor);
        if (!monitor || !m_animations.needs_frame(m_port.monitor_identity(monitor)) ||
            monitor->m_solitaryClient.expired()) {
            return;
        }
        if (std::ranges::none_of(m_inhibited, [&](const auto &item) { return item.lock() == monitor; })) {
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
    m_monitor_removed = Event::bus()->m_events.monitor.preRemoved.listen([this](const PHLMONITOR &monitor) {
        m_workspace->remove_monitor(monitor);
        m_animations.monitor_changed(m_port.monitor_identity(monitor));
        restore_scanout();
    });
    m_pre = Event::bus()->m_events.render.pre.listen([this](const PHLMONITOR &monitor) {
        render_pre(monitor);
    });
    m_stage = Event::bus()->m_events.render.stage.listen([this](const Event::SRenderStageEvent &event) {
        render_stage(event);
    });
    m_reload = Event::bus()->m_events.config.reloaded.listen([this] { reload(); });
    for (const auto &window : Desktop::windowState()->windows()) {
        if (window) {
            m_animations.track(PHLWINDOWREF{window});
        }
    }
}

CPluginRuntime::Impl::~Impl() {
    m_open.reset(); m_close_event.reset(); m_destroy.reset(); m_pre_checks.reset();
    m_pre.reset(); m_stage.reset(); m_reload.reset(); m_monitor_layout.reset(); m_monitor_removed.reset();
    m_workspace->shutdown();
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

void CPluginRuntime::Impl::configure(bool warm) {
    m_resources.for_each([&]<class Style>(auto &resources) {
        m_animations.template update_settings<Style>(Style::settings(m_config));
        if constexpr (requires { Style::warm(resources); }) {
            if (warm && m_config.style == Style::KIND) {
                Style::warm(resources);
            }
        }
    });
    m_animations.configure({.style = m_config.style, .enabled = m_config.enabled, .inert = m_inert,
        .opening = m_config.open.enabled, .movement = m_config.movement.enabled,
        .closing = m_config.close.enabled, .workspace_movement = m_config.movement.workspaceMovement,
        .close_available = m_close_available});
}

void CPluginRuntime::Impl::configure_workspace() {
    m_workspace->configure(m_config.workspace);
}

void CPluginRuntime::Impl::reload() {
    m_config = hyprimate::config::readConfig(m_values);
    const bool INERT = m_config.enabled && *m_wobble != 0;
    if (INERT && !m_inert) {
        notify(WOBBLE_MESSAGE);
    }
    m_inert = INERT;
    m_workspace->clear();
    configure_workspace();
    m_animations.reload();
    configure(false);
    restore_scanout();
}

void CPluginRuntime::Impl::render_pre(const PHLMONITOR &monitor) {
    m_config = hyprimate::config::readConfig(m_values);
    configure(true);
    restore_scanout();
    if (monitor) {
        m_animations.frame({.monitor = m_port.monitor_identity(monitor), .frame = ++m_frame,
            .now = hyprimate::animation::Clock::now()});
    }
    configure_workspace();
    m_workspace->on_render_pre(monitor);
}

void CPluginRuntime::Impl::render_stage(const Event::SRenderStageEvent &event) {
    if (!event.monitor) {
        return;
    }
    const auto MONITOR = m_port.monitor_identity(event.monitor);
    if (event.stage == RENDER_PRE_WINDOW && event.context) {
        auto &ctx = event.context->get();
        if (const auto WINDOW = ctx.m_data.currentWindow.lock()) {
            CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources()};
            m_animations.before_window(m_port.identity(PHLWINDOWREF{WINDOW}), canvas);
        }
    } else if (event.stage == RENDER_POST_WINDOWS && event.context) {
        CHyprAnimationCanvas canvas{event.context->get(), m_port.canvas_resources()};
        m_animations.after_windows(MONITOR, canvas);
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

CPluginRuntime::CPluginRuntime(HANDLE handle) : m_impl(std::make_unique<Impl>(handle)) {}
CPluginRuntime::~CPluginRuntime() = default;
