#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "AnimationCloseRenderer.hpp"
#include "AnimationTransformer.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "config_generated.hpp"
#include "workspace/WorkspaceAnimation.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace {

    constexpr const char *WOBBLE_MESSAGE =
        "[hyprimate] Disable decoration:wobble:enabled before enabling hyprimate";

    bool is_usable_box(const CBox &box) {
        return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
               std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
    }

    // Identity of the window's monitor; zero when it has none, which matches no live monitor.
    std::uintptr_t monitor_id(const PHLWINDOW &window) {
        return reinterpret_cast<std::uintptr_t>(window->m_monitor.lock().get());
    }


    CHyprColor color_from_argb(std::uint32_t argb) {
        const auto CHANNEL = [argb](unsigned int shift) {
            return static_cast<float>((argb >> shift) & 0xFFU) / 255.F;
        };
        return {CHANNEL(16), CHANNEL(8), CHANNEL(0), CHANNEL(24)};
    }

    hyprimate::workspace::GapFill workspace_gap(const hyprimate::config::SConfig &config) {
        using Style = hyprimate::workspace::GapFillStyle;
        hyprimate::workspace::GapFill gap;
        switch (config.workspace.rotation.gapFillStyle) {
            case hyprimate::config::RotationGapFillStyle::Morph: gap.style = Style::Morph; break;
            case hyprimate::config::RotationGapFillStyle::Background: gap.style = Style::Background; break;
            case hyprimate::config::RotationGapFillStyle::Wallpaper: gap.style = Style::Wallpaper; break;
            case hyprimate::config::RotationGapFillStyle::Still: gap.style = Style::Still; break;
        }
        const auto color = color_from_argb(config.workspace.rotation.gapFillColor);
        gap.color = {static_cast<float>(color.r * color.a), static_cast<float>(color.g * color.a),
            static_cast<float>(color.b * color.a), static_cast<float>(color.a)};
        gap.blur = static_cast<float>(config.workspace.rotation.gapFillBlur);
        return gap;
    }

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Listener setup keeps callback lifetime
// and registration order together.
CPluginRuntime::CPluginRuntime(HANDLE handle) : m_handle(handle) {
    std::random_device entropy;
    m_next_seed = (std::uint64_t{entropy()} << 32U) | entropy();
    hyprimate::config::registerConfigValues(m_handle, m_values);
    m_config = hyprimate::config::readConfig(m_values);
    prepare_styles(false);
    m_style_resources.for_each([&](auto live, auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            live.screen_tick(*this, resources, Clock::now(), {});
        }
    });

    if (plugin_enabled() && wobble_enabled()) {
        notify(WOBBLE_MESSAGE);
        throw std::runtime_error(WOBBLE_MESSAGE);
    }

    // Close transitions are optional: if the compositor symbols are not found, movement deformation
    // still runs.
    m_close = std::make_unique<CAnimationCloseRenderer>(m_handle, m_style_resources);
    std::string close_error;
    if (!m_close->install(close_error)) {
        notify("[hyprimate] close transitions unavailable: " + close_error);
    }
    m_workspace = std::make_unique<CWorkspaceAnimation>(m_handle);
    std::string workspace_error;
    if (!m_workspace->install(workspace_error)) {
        notify("[hyprimate] workspace transitions unavailable: " + workspace_error);
    }
    m_workspace->configure(m_config.workspace.enabled,
        m_config.workspace.style == hyprimate::config::WorkspaceStyle::Rotation ? "rotation" : "",
        m_config.workspace.rotation.duration, workspace_gap(m_config),
        m_config.workspace.rotation.curve);

    // Listeners are registered last so a failed constructor never leaves callbacks pointing at a
    // half-built runtime.
    m_listen_open_late = Event::bus()->m_events.window.openLate.listen(
        [this](const PHLWINDOW &window) { track(window); });

    m_listen_close = Event::bus()->m_events.window.close.listen([this](const PHLWINDOW &window) {
        if (auto *entry = find(window)) {
            // Stage before detaching so a matching animation can continue into the close.
            if (close_enabled() && eligible(window)) {
                auto *transformer = transformer_of(window);
                const bool MATCHED = transformer && transformer->built_for(m_config.style);
                std::optional<SStyleState> fresh;
                if (!MATCHED) {
                    fresh.emplace(initial_style(0));
                }
                auto &state = MATCHED ? transformer->style_state() : *fresh;

                const auto EDGE = [&] { return edge_input_for(window, false, entry->open_edge); };
                const auto OBSTACLES = [&] -> std::span<const hyprimate::Box> {
                    m_neighbours.clear();
                    gather_contact_obstacles(window);
                    return m_neighbours;
                };
                m_close->stage(
                    window,
                    state,
                    MATCHED && transformer->active(),
                    EDGE,
                    OBSTACLES,
                    [this] { return m_next_seed++; });
            }
            detach_window(*entry, window);
        }
        forget(window);
    });

    m_listen_destroy = Event::bus()->m_events.window.destroy.listen([this](PHLWINDOWREF window) {
        std::erase_if(m_tracked, [&window](const STracked &entry) {
            return entry.window.expired() || entry.window.get() == window.get();
        });
    });

    // A solitary client can bypass composited screen content before render.pre.
    m_listen_render_pre_checks =
        Event::bus()->m_events.render.preChecks.listen([this](const PHLMONITOR &monitor) {
            m_workspace->on_render_pre_checks(monitor);
            if (!monitor || !screen_needs_frame(monitor) || monitor->m_solitaryClient.expired()) {
                return;
            }
            if (std::ranges::none_of(m_screen_inhibited,
                    [&](const auto &entry) { return entry.lock() == monitor; })) {
                m_screen_inhibited.emplace_back(monitor);
            }
            monitor->m_solitaryClient.reset();
        });
    m_listen_monitor_layout = Event::bus()->m_events.monitor.layoutChanged.listen([this] {
        m_workspace->clear();
        clear_screen_content();
    });
    m_listen_monitor_removed =
        Event::bus()->m_events.monitor.preRemoved.listen([this](const PHLMONITOR &monitor) {
            m_workspace->remove_monitor(monitor);
            clear_screen_content();
        });

    // Everything advances once per rendered frame of its monitor, after native animations have
    // ticked and before the frame collects damage. Animation ticks are not used: a self-requested
    // tick re-fires every millisecond.
    m_listen_render_pre = Event::bus()->m_events.render.pre.listen(
        [this](const PHLMONITOR &monitor) { on_render_pre(monitor); });
    // Requests made in render.pre can be consumed by the current commit. Keep requesting frames
    // after rendering while live animations, retained closes, or Vestige need them.
    m_listen_render_post =
        Event::bus()->m_events.render.stage.listen([this](const Event::SRenderStageEvent &event) {
            // Each Style queues its own screen content around the window pass.
            if (event.stage == RENDER_PRE_WINDOW && event.context && event.monitor) {
                auto &ctx = event.context->get();
                const auto WINDOW = ctx.m_data.currentWindow.lock();
                m_style_resources.for_each([&](auto live, auto &resources) {
                    if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
                        live.screen_before_window(*this, resources, ctx, event.monitor, WINDOW);
                    }
                });
                return;
            }
            if (event.stage == RENDER_POST_WINDOWS && event.context && event.monitor) {
                m_style_resources.for_each([&](auto live, auto &resources) {
                    if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
                        live.screen_after_windows(*this, resources, event.context->get(), event.monitor);
                    }
                });
                return;
            }
            if (event.stage != RENDER_POST) {
                return;
            }
            if (event.monitor &&
                (screen_needs_frame(event.monitor) || m_close->needs_frame(event.monitor))) {
                event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            }
            for (const auto &entry : m_tracked) {
                const auto WINDOW = entry.window.lock();
                if (!WINDOW || WINDOW->m_monitor.lock() != event.monitor) {
                    continue;
                }
                const auto *transformer = transformer_of(WINDOW);
                if (transformer && transformer->active()) {
                    event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
                    break;
                }
            }
        });

    m_listen_reload = Event::bus()->m_events.config.reloaded.listen([this] { on_reload(); });

    for (const auto &window : Desktop::windowState()->windows()) {
        track(window);
    }
}

CPluginRuntime::~CPluginRuntime() {
    // Unload order: stop callbacks, unhook the close path, drop its records, then detach every
    // window.
    m_listen_open_late.reset();
    m_listen_close.reset();
    m_listen_destroy.reset();
    m_listen_render_pre_checks.reset();
    m_listen_render_pre.reset();
    m_listen_render_post.reset();
    m_listen_reload.reset();
    m_listen_monitor_layout.reset();
    m_listen_monitor_removed.reset();
    m_workspace->shutdown();
    clear_screen_content();
    m_close->shutdown();
    detach_all();
}

bool CPluginRuntime::plugin_enabled() const {
    return m_config.enabled;
}

double CPluginRuntime::movement_strength() const {
    return with_live_style(m_config.style, [&](auto style) {
        using Module = typename decltype(style)::Module;
        return style.strength(m_style_resources.get<Module>());
    });
}

SStyleState CPluginRuntime::initial_style(std::uint64_t seed) const {
    return with_live_style(m_config.style, [&](auto live) {
        using Module = typename decltype(live)::Module;
        return SStyleState{live.initial(m_style_resources.get<Module>(), seed)};
    });
}

bool CPluginRuntime::movement_enabled() const {
    return plugin_enabled() && m_config.movement.enabled &&
           with_live_style(m_config.style, [&](auto live) {
               using Module = typename decltype(live)::Module;
               return live.usable(m_style_resources.get<Module>());
           }) &&
           movement_strength() > 0.0;
}

bool CPluginRuntime::open_enabled() const {
    return plugin_enabled() && !m_inert && m_config.open.enabled;
}

bool CPluginRuntime::close_enabled() const {
    return plugin_enabled() && !m_inert && m_config.close.enabled;
}

bool CPluginRuntime::style_selected(hyprimate::config::Style style) const {
    return m_config.style == style;
}

bool CPluginRuntime::workspace_movement_enabled() const {
    return !m_inert && m_config.enabled && m_config.movement.enabled &&
           m_config.movement.workspaceMovement;
}

bool CPluginRuntime::workspace_has_other_windows(const PHLWINDOW &window) {
    return std::ranges::any_of(Desktop::windowState()->windows(), [&window](const auto &other) {
        return other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
               other->mapped() && !other->isHidden();
    });
}

// Tiled windows on the workspace, as GOAL boxes, for the contact rule. Floating windows are not
// neighbours.
void CPluginRuntime::gather_neighbours(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
            other->mapped() && !other->isHidden() && !other->isFloating()) {
            const CBox GOAL_BOX = other->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
            m_neighbours.push_back(
                {.x = GOAL_BOX.x, .y = GOAL_BOX.y, .w = GOAL_BOX.w, .h = GOAL_BOX.h});
        }
    }
}

void CPluginRuntime::gather_contact_obstacles(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
            other->mapped() && !other->isHidden()) {
            const CBox WINDOW_BOX = other->getFullWindowBoundingBox();
            m_neighbours.push_back(
                {.x = WINDOW_BOX.x, .y = WINDOW_BOX.y, .w = WINDOW_BOX.w, .h = WINDOW_BOX.h});
        }
    }
}

hyprimate::EdgeInput CPluginRuntime::edge_input_for(
    const PHLWINDOW &window, bool opening, std::optional<hyprimate::Edge> origin) const {
    const auto MONITOR = window->m_monitor.lock();
    if (!MONITOR) {
        return {};
    }
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox BODY =
        opening ? window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL) : CURRENT;
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    // Decoration margins (border, shadow) do not scale with native popin, so they carry over to the
    // rest box.
    const CBox REST{BODY.x + (CAPTURE.x - CURRENT.x),
        BODY.y + (CAPTURE.y - CURRENT.y),
        BODY.w + (CAPTURE.w - CURRENT.w),
        BODY.h + (CAPTURE.h - CURRENT.h)};
    const auto BOX = [](const CBox &source_box) {
        return hyprimate::Box{
            .x = source_box.x,
            .y = source_box.y,
            .w = source_box.w,
            .h = source_box.h,
        };
    };

    m_neighbours.clear();
    if (!window->isFloating()) {
        gather_neighbours(window);
    }
    return {
        .body = BOX(BODY),
        .capture = BOX(REST),
        .screen = BOX(MONITOR->logicalBox()),
        .work_area = BOX(MONITOR->logicalBoxMinusReserved()),
        .neighbours = m_neighbours,
        .origin = origin,
        .last_window = !workspace_has_other_windows(window),
    };
}


bool CPluginRuntime::wobble_enabled() const {
    return *m_wobble != 0;
}

void CPluginRuntime::notify(const std::string &text) const {
    HyprlandAPI::addNotification(m_handle, text, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
}

void CPluginRuntime::on_reload() {
    m_config = hyprimate::config::readConfig(m_values);
    m_workspace->configure(m_config.workspace.enabled,
        m_config.workspace.style == hyprimate::config::WorkspaceStyle::Rotation ? "rotation" : "",
        m_config.workspace.rotation.duration, workspace_gap(m_config),
        m_config.workspace.rotation.curve);
    m_workspace->clear();
    prepare_styles(false);
    m_style_resources.for_each([&](auto live, auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            live.screen_tick(*this, resources, Clock::now(), {});
        }
    });
    clear_screen_content();
    detach_all();
    m_close->clear();

    if (plugin_enabled() && wobble_enabled()) {
        if (!m_inert) {
            notify(WOBBLE_MESSAGE);
        }
        m_inert = true;
        return;
    }

    m_inert = false;
    baseline_all();
}


CPluginRuntime::STracked *CPluginRuntime::find(const PHLWINDOW &window) {
    for (auto &entry : m_tracked) {
        if (entry.window.get() == window.get()) {
            return &entry;
        }
    }
    return nullptr;
}

void CPluginRuntime::track(const PHLWINDOW &window) {
    if (!window) {
        return;
    }
    if (find(window) != nullptr) {
        return;
    }

    STracked entry;
    entry.window = PHLWINDOWREF{window};
    entry.tracker.track(to_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
        to_box(window->getFullWindowBoundingBox()),
        monitor_id(window));
    m_tracked.push_back(entry);
}

void CPluginRuntime::forget(const PHLWINDOW &window) {
    std::erase_if(
        m_tracked, [&window](const STracked &entry) { return entry.window.get() == window.get(); });
}

void CPluginRuntime::rebaseline(STracked &entry, const PHLWINDOW &window) {
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.tracker.baseline(
        to_box(CURRENT), to_box(window->getFullWindowBoundingBox()), monitor_id(window));
}

void CPluginRuntime::baseline_all() {
    for (auto &entry : m_tracked) {
        if (const auto WINDOW = entry.window.lock()) {
            rebaseline(entry, WINDOW);
        }
    }
}

bool CPluginRuntime::eligible(const PHLWINDOW &window) const {
    // Opening animations are deformable; the sampler rebaselines geometry while they run.
    return plugin_enabled() && !m_inert && window->mapped() &&
           !Fullscreen::controller()->isFullscreen(window) &&
           is_usable_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
}
void CPluginRuntime::prepare_styles(bool warm_selected) {
    m_style_resources.for_each([&](auto live, auto &resources) {
        using Live = decltype(live);
        live.prepare(Live::settings(m_config), resources, warm_selected && m_config.style == Live::KIND);
    });
}


void CPluginRuntime::on_render_pre(const PHLMONITOR &monitor) {
    // Config is read live: runtime edits (hl.config, hyprctl keyword) need not fire a reload. The
    // texture is rebuilt only when the value actually changes.
    m_config = hyprimate::config::readConfig(m_values);
    prepare_styles(true);
    const auto SCREEN_NOW = Clock::now();
    m_style_resources.for_each([&](auto live, auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            live.screen_tick(*this, resources, SCREEN_NOW, monitor);
        }
    });
    // A policy change may have cleared content on every monitor.
    restore_screen_scanout({});
    // Sampling runs on each window's own monitor only, so a window steps once per frame of the
    // monitor it is on.
    const auto NOW = Clock::now();
    for (auto &entry : m_tracked) {
        const auto WINDOW = entry.window.lock();
        if (!WINDOW || WINDOW->m_monitor.lock() != monitor) {
            continue;
        }
        update_window(entry, WINDOW, NOW);
    }

    std::erase_if(m_tracked, [](const STracked &entry) { return entry.window.expired(); });

    // A live disable also drops retained closes, restoring native fades where still available.
    if (!close_enabled() || !close_animation_enabled(m_config, m_style_resources)) {
        m_close->clear();
    } else {
        std::vector<CBox> damage;
        m_close->tick(NOW, monitor, damage);
        for (const auto &box : damage) {
            g_pHyprRenderer->damageBox(box);
            for (const auto &candidate : State::monitorState()->monitors()) {
                if (candidate->logicalBox().overlaps(box)) {
                    candidate->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
                }
            }
        }
    }
    m_workspace->configure(m_config.workspace.enabled,
        m_config.workspace.style == hyprimate::config::WorkspaceStyle::Rotation ? "rotation" : "",
        m_config.workspace.rotation.duration, workspace_gap(m_config),
        m_config.workspace.rotation.curve);
    m_workspace->on_render_pre(monitor);
}

void CPluginRuntime::update_window(
    STracked &entry, const PHLWINDOW &window, Clock::time_point now) {
    const double ELAPSED =
        std::max(0.0, std::chrono::duration<double>(now - entry.last_update).count());
    entry.last_update = now;
    const bool ANIMATING = window->presentation().animatingIn();
    // A window that is not stepping, not deforming, and not opening cannot change until it becomes
    // eligible or starts animating, so it skips the snapshot entirely.
    const auto *existing = transformer_of(window);
    const bool STEPPING = (existing != nullptr) && existing->active();
    if (!STEPPING && entry.tracker.dormant(ANIMATING) && !eligible(window)) {
        return;
    }

    const hyprimate::TrackDecision DECISION =
        entry.tracker.update(snapshot_of(window, ANIMATING, ELAPSED));
    if (DECISION.action == hyprimate::TrackAction::Rebaseline) {
        // Covers a material change, a stall, a movement jump, and a window that stopped being
        // deformable.
        detach_window(entry, window);
        if (DECISION.begin_open) {
            begin_opening(entry, window);
        }
        return;
    }
    if (DECISION.cancel_opening) {
        if (auto *transformer = transformer_of(window)) {
            transformer->cancel_opening();
        }
    }
    if (DECISION.action == hyprimate::TrackAction::Opening) {
        begin_opening(entry, window);
        // An opening may warp native popin geometry to its goal. Baseline the box actually
        // displayed.
        rebaseline(entry, window);
        step_window(entry, window, 0.0);
        return;
    }
    if (DECISION.action == hyprimate::TrackAction::Move) {
        record_movement(entry, window, DECISION, ELAPSED);
    }
    step_window(entry, window, ELAPSED);
}

hyprimate::WindowSnapshot CPluginRuntime::snapshot_of(
    const PHLWINDOW &window, bool animating, double elapsed) {
    const auto *transformer = transformer_of(window);
    hyprimate::WindowSnapshot window_snapshot;
    window_snapshot.current =
        to_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
    window_snapshot.capture = to_box(window->getFullWindowBoundingBox());
    window_snapshot.deformable = eligible(window);
    window_snapshot.animating_in = animating;
    with_live_style(m_config.style, [&](auto style) {
        using Module = typename decltype(style)::Module;
        style.snapshot(transformer, window_snapshot, m_style_resources.get<Module>());
    });
    window_snapshot.material_changed =
        (transformer != nullptr) && !transformer->built_for(m_config.style);
    window_snapshot.open_enabled = open_enabled();
    window_snapshot.movement_enabled = movement_enabled();
    window_snapshot.monitor = monitor_id(window);
    window_snapshot.elapsed = elapsed;
    window_snapshot.workspace_movement = m_config.movement.workspaceMovement;
    if (const auto WORKSPACE = window->m_workspace;
        WORKSPACE && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)) {
        window_snapshot.workspace = reinterpret_cast<std::uintptr_t>(WORKSPACE.get());
        window_snapshot.workspace_moving = WORKSPACE->m_renderOffset->isBeingAnimated();
        if (window_snapshot.workspace_moving) {
            const auto OFFSET = WORKSPACE->m_renderOffset->value();
            window_snapshot.workspace_offset = {.x = OFFSET.x, .y = OFFSET.y};
        }
        // Retained screen content may preserve its last world-space placement after hiding.
        const auto CAPTURE = screen_window_capture(window, transformer);
        if (CAPTURE) {
            window_snapshot.workspace_offset = {
                .x = CAPTURE->x - window_snapshot.capture.x,
                .y = CAPTURE->y - window_snapshot.capture.y,
            };
        }
        window_snapshot.deformable =
            window_snapshot.deformable &&
            (WORKSPACE->visible() || window_snapshot.workspace_moving || CAPTURE.has_value());
    }
    return window_snapshot;
}

void CPluginRuntime::record_movement(STracked &entry,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double elapsed) {
    const bool ENABLED = movement_enabled();
    const bool MOVING = ENABLED && with_live_style(m_config.style,
        [&](auto live) { return live.movement_requested(decision); });
    auto *transformer = transformer_of(window);
    if (!MOVING && (transformer == nullptr)) {
        return;
    }
    if (entry.has_footprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.has_footprint = false;
    }
    if (transformer == nullptr) {
        transformer = acquire_transformer(window);
    }
    if (decision.cancel_movement) {
        transformer->cancel_movement();
    }
    with_live_style(m_config.style, [&](auto style) {
        using Module = typename decltype(style)::Module;
        style.movement(*this, *transformer, window, decision, elapsed, ENABLED, MOVING,
            m_style_resources.get<Module>());
    });
}

void CPluginRuntime::begin_opening(STracked &entry, const PHLWINDOW &window) {
    // Every open attempt starts from no edge, so an early return never leaves a stale origin
    // behind.
    entry.open_edge.reset();
    if (!open_enabled()) {
        return;
    }

    if (!with_live_style(m_config.style, [&](auto style) {
            using Module = typename decltype(style)::Module;
            return style.open(*this, entry, window, m_style_resources.get<Module>());
        })) {
        return;
    }

    if (entry.has_footprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
    }
    entry.footprint = world_footprint(window);
    entry.has_footprint = true;

    g_pHyprRenderer->damageWindow(window);
    schedule_animation(window);
}

void CPluginRuntime::step_window(STracked &entry, const PHLWINDOW &window, double elapsed) {
    auto *transformer = transformer_of(window);
    if ((transformer == nullptr) || !transformer->active()) {
        if (transformer != nullptr) {
            transformer->cancel_opening();  // a driven open that went inactive (fullscreen, unmap)
        }
        // returns the layout box
        if (entry.has_footprint) {
            g_pHyprRenderer->damageBox(entry.footprint);
            entry.has_footprint = false;
            g_pHyprRenderer->damageWindow(window);
            schedule_animation(window);
        }
        return;
    }
    if (entry.has_footprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.has_footprint = false;
    }
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    if (!is_usable_box(CAPTURE)) {
        detach_window(entry, window);
        return;
    }
    with_live_style(m_config.style, [&](auto style) {
        using Module = typename decltype(style)::Module;
        style.step(*this, *transformer, elapsed, {CAPTURE.w, CAPTURE.h}, movement_enabled(),
            m_style_resources.get<Module>());
    });
    // The step may drive the layout box. Baselining after it keeps that drive from reading as
    // window movement.
    entry.tracker.baseline(
        to_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
        to_box(window->getFullWindowBoundingBox()),
        monitor_id(window));
    if (transformer->active()) {
        entry.footprint = world_footprint(window);
        entry.has_footprint = true;
    } else {
        // Settled fields have already restored the undeformed mesh; pruning drops the transformer.
        window->effects().transformers()->removeInactive();
        if (const auto MONITOR = window->m_monitor.lock()) {
            MONITOR->recheckSolitary();
        }
    }
    g_pHyprRenderer->damageWindow(window);
    schedule_animation(window);
}

CAnimationTransformer *CPluginRuntime::transformer_of(const PHLWINDOW &window) {
    return window->effects().transformers()->get<CAnimationTransformer>();
}

CAnimationTransformer *CPluginRuntime::acquire_transformer(const PHLWINDOW &window) {
    const auto &list = window->effects().transformers();
    if (auto *existing = list->get<CAnimationTransformer>()) {
        return existing;
    }
    // Each window draws from its own stream, so windows moved together (scrolling, workspace
    // slides) still differ.
    auto state = initial_style(m_next_seed++);
    return list->emplace<CAnimationTransformer>(window->m_self, std::move(state), m_style_resources);
}

CBox CPluginRuntime::world_footprint(const PHLWINDOW &window) const {
    CBox box = window->getFullWindowBoundingBox();
    if (const auto CAPTURE = screen_window_capture(window, transformer_of(window))) {
        return window->effects().transformBoxForDamage(*CAPTURE);
    }
    const auto WORKSPACE = window->m_workspace;
    if (WORKSPACE && WORKSPACE->m_renderOffset->isBeingAnimated() &&
        !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)) {
        box.translate(WORKSPACE->m_renderOffset->value());
    }
    box.translate(window->presentation().floatingOffset());
    return window->effects().transformBoxForDamage(box);
}

void CPluginRuntime::schedule_animation(const PHLWINDOW &window) {
    if (const auto MONITOR = window->m_monitor.lock()) {
        MONITOR->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }
}

void CPluginRuntime::detach_window(STracked &entry, const PHLWINDOW &window) {
    const bool HAD_FOOTPRINT = entry.has_footprint;
    if (HAD_FOOTPRINT) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.has_footprint = false;
    }
    auto *transformer = transformer_of(window);
    if (transformer != nullptr) {
        transformer->mark_detached();
        window->effects().transformers()->removeInactive();
        if (const auto MONITOR = window->m_monitor.lock()) {
            MONITOR->recheckSolitary();
        }
        // No pointer use after pruning: plugin vtables are gone before unload.
    }
    if (HAD_FOOTPRINT || (transformer != nullptr)) {
        g_pHyprRenderer->damageWindow(window);
        schedule_animation(window);
    }
}

void CPluginRuntime::detach_all() {
    for (auto &entry : m_tracked) {
        if (const auto WINDOW = entry.window.lock()) {
            detach_window(entry, WINDOW);
        }
    }
}

bool CPluginRuntime::screen_needs_frame(const PHLMONITOR &monitor) const {
    bool needed = false;
    m_style_resources.for_each([&](auto live, const auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            needed = needed || live.screen_needs_frame(*this, resources, monitor);
        }
    });
    return needed;
}

std::optional<CBox> CPluginRuntime::screen_window_capture(
    const PHLWINDOW &window, const CAnimationTransformer *transformer) const {
    std::optional<CBox> capture;
    m_style_resources.for_each([&](auto live, const auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            if (!capture) {
                capture = live.screen_window_capture(*this, resources, window, transformer);
            }
        }
    });
    return capture;
}

void CPluginRuntime::restore_screen_scanout(const PHLMONITOR &monitor) {
    std::erase_if(m_screen_inhibited, [&](const auto &entry) {
        const auto CANDIDATE = entry.lock();
        if (!CANDIDATE) {
            return true;
        }
        if ((monitor && CANDIDATE != monitor) || screen_needs_frame(CANDIDATE)) {
            return false;
        }
        CANDIDATE->recheckSolitary();
        return true;
    });
}

void CPluginRuntime::clear_screen_content() {
    m_style_resources.for_each([&](auto live, auto &resources) {
        if constexpr (decltype(live)::HAS_SCREEN_CONTENT) {
            live.screen_clear(*this, resources);
        }
    });
    for (const auto &entry : m_screen_inhibited) {
        if (const auto MONITOR = entry.lock()) {
            MONITOR->recheckSolitary();
        }
    }
    m_screen_inhibited.clear();
}

