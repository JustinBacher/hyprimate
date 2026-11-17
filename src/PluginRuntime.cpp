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
#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/swarm/SwarmConfig.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

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

    // ARGB config value to a float colour; channel bytes are unpacked, not treated as one packed
    // integer.
    CHyprColor color_from_argb(std::uint32_t argb) {
        const auto CHANNEL = [argb](unsigned int shift) {
            return static_cast<float>((argb >> shift) & 0xFFU) / 255.F;
        };
        return {CHANNEL(16), CHANNEL(8), CHANNEL(0), CHANNEL(24)};
    }

}  // namespace

CPluginRuntime::CPluginRuntime(HANDLE handle) : m_handle(handle) {
    std::random_device entropy;
    m_nextSeed = (std::uint64_t{entropy()} << 32U) | entropy();
    hyprimate::config::registerConfigValues(m_handle, m_values);
    m_config = hyprimate::config::readConfig(m_values);
    m_bubbleResidue->setEnabled(bubbleResidueEnabled());

    if (pluginEnabled() && wobbleEnabled()) {
        notify(WOBBLE_MESSAGE);
        throw std::runtime_error(WOBBLE_MESSAGE);
    }

    // Close transitions are optional: if the compositor symbols are not found, movement deformation
    // still runs.
    m_close = std::make_unique<CAnimationCloseRenderer>(m_handle, m_closeSolid, m_shade, m_sprites);
    std::string close_error;
    if (!m_close->install(close_error)) {
        notify("[hyprimate] close transitions unavailable: " + close_error);
    }

    // Listeners are registered last so a failed constructor never leaves callbacks pointing at a
    // half-built runtime.
    m_listenOpenLate = Event::bus()->m_events.window.openLate.listen(
        [this](const PHLWINDOW &window) { track(window); });

    m_listenClose = Event::bus()->m_events.window.close.listen([this](const PHLWINDOW &window) {
        if (auto *entry = find(window)) {
            // Stage before detaching so a matching animation can continue into the close.
            if (closeEnabled() && eligible(window)) {
                auto *transformer = transformerOf(window);
                const bool MATCHED = transformer && !materialChanged(*transformer);
                std::optional<SStyleState> fresh;
                if (!MATCHED) {
                    fresh.emplace(initialStyle(
                        m_config.style, effectiveMaterialParams(), clothMovementParams(), 0));
                }
                auto &state = MATCHED ? transformer->styleState() : *fresh;
                if (auto *bubble = std::get_if<SBubbleStyle>(&state)) {
                    bubble->renderer = m_bubble;
                    bubble->residue = m_bubbleResidue;
                }

                const auto EDGE = [&] { return edgeInputFor(window, false, entry->openEdge); };
                const auto OBSTACLES = [&] -> std::span<const hyprimate::Box> {
                    m_neighbours.clear();
                    gatherClothObstacles(window);
                    return m_neighbours;
                };
                m_close->stage(
                    window,
                    state,
                    MATCHED && transformer->active(),
                    m_config,
                    effectiveMaterialParams(),
                    EDGE,
                    OBSTACLES,
                    [this] { return m_nextSeed++; },
                    [&] { return swarmSides(window, false); });
            }
            detachWindow(*entry, window);
        }
        forget(window);
    });

    m_listenDestroy = Event::bus()->m_events.window.destroy.listen([this](PHLWINDOWREF window) {
        std::erase_if(m_tracked, [&window](const STracked &entry) {
            return entry.window.expired() || entry.window.get() == window.get();
        });
    });

    // A solitary client bypasses the monitor scene entirely, and can also direct-scan out before
    // render.pre. Remove it for this frame while residue exists; restore native selection when the
    // last deposit expires.
    m_listenRenderPreChecks =
        Event::bus()->m_events.render.preChecks.listen([this](const PHLMONITOR &monitor) {
            if (monitor && hasWorkspaceSwarmTail(monitor)) {
                monitor->m_solitaryClient.reset();
                return;
            }
            if (!monitor || !m_bubbleResidue->active(monitor) ||
                monitor->m_solitaryClient.expired()) {
                return;
            }
            if (std::ranges::none_of(m_residueInhibited,
                    [&](const auto &entry) { return entry.lock() == monitor; })) {
                m_residueInhibited.emplace_back(monitor);
            }
            monitor->m_solitaryClient.reset();
        });
    m_listenMonitorLayout =
        Event::bus()->m_events.monitor.layoutChanged.listen([this] { clearBubbleResidue(); });
    m_listenMonitorRemoved = Event::bus()->m_events.monitor.preRemoved.listen(
        [this](const PHLMONITOR &) { clearBubbleResidue(); });

    // Everything advances once per rendered frame of its monitor, after native animations have
    // ticked and before the frame collects damage. Animation ticks are not used: a self-requested
    // tick re-fires every millisecond.
    m_listenRenderPre = Event::bus()->m_events.render.pre.listen(
        [this](const PHLMONITOR &monitor) { onRenderPre(monitor); });
    // Requests made in render.pre can be consumed by the current commit. Keep requesting frames
    // after rendering while live animations, retained closes, or residue need them.
    m_listenRenderPost =
        Event::bus()->m_events.render.stage.listen([this](const Event::SRenderStageEvent &event) {
            if (event.stage == RENDER_POST_WALLPAPER && event.context) {
                for (const auto &entry : m_tracked) {
                    const auto WINDOW = entry.window.lock();
                    if (!WINDOW || WINDOW->m_monitor.lock() != event.monitor) {
                        continue;
                    }
                    auto *transformer = transformerOf(WINDOW);
                    const auto *bubble = transformer
                                             ? std::get_if<SBubbleStyle>(&transformer->styleState())
                                             : nullptr;
                    if (!bubble || !bubble->bubble.opening() || bubble->bubble.stirred() ||
                        !bubble->wallpaper || bubble->wallpaper->queued) {
                        continue;
                    }
                    const auto PRESENTATION = WINDOW->presentation().renderPresentation();
                    const auto SOURCE =
                        WINDOW->getFullWindowBoundingBox()
                            .translate(PRESENTATION.workspaceOffset + PRESENTATION.floatingOffset -
                                       event.monitor->m_position)
                            .scale(event.monitor->m_scale);
                    queueBubbleWallpaper(
                        event.context->get(), bubble->wallpaper, SOURCE, false, SOURCE);
                }
                return;
            }
            // A wake lies just under the window that left it; beads, and wakes whose window was
            // not drawn, lie over every window.
            if (event.stage == RENDER_PRE_WINDOW && event.context && event.monitor) {
                auto &ctx = event.context->get();
                const auto WINDOW = ctx.m_data.currentWindow.lock();
                auto *transformer = WINDOW ? transformerOf(WINDOW) : nullptr;
                auto *bubble =
                    transformer ? std::get_if<SBubbleStyle>(&transformer->styleState()) : nullptr;
                // This must precede the window's transformed draw in the monitor pass: its
                // framebuffer then contains lower windows but not this window itself.
                if (bubble && transformer->active() && bubble->bubble.stirred()) {
                    if (!bubble->wallpaper) {
                        bubble->wallpaper = makeShared<SBubbleWallpaper>();
                    }
                    const auto PRESENTATION = WINDOW->presentation().renderPresentation();
                    const auto LOGICAL = WINDOW->getFullWindowBoundingBox().translate(
                        PRESENTATION.workspaceOffset + PRESENTATION.floatingOffset -
                        event.monitor->m_position);
                    // UV axes describe the undeformed source box; capture the larger drawn
                    // footprint and refraction margin without changing that coordinate system.
                    const auto SOURCE = LOGICAL.copy().scale(event.monitor->m_scale);
                    const auto BOUNDS = transformer->transformedExtents(LOGICAL).expand(8.0).scale(
                        event.monitor->m_scale);
                    queueBubbleWallpaper(ctx, bubble->wallpaper, SOURCE, true, BOUNDS);
                }
                m_bubbleResidue->queueUnder(
                    ctx, event.monitor, ctx.m_data.currentWindow.lock(), m_bubble);
                return;
            }
            if (event.stage == RENDER_POST_WINDOWS && event.context && event.monitor) {
                m_bubbleResidue->queue(event.context->get(), event.monitor, m_bubble);
                queueWorkspaceSwarmTails(event.context->get(), event.monitor);
                return;
            }
            if (event.stage != RENDER_POST) {
                return;
            }
            if (event.monitor &&
                (m_bubbleResidue->active(event.monitor) || m_close->needsFrame(event.monitor) ||
                    hasWorkspaceSwarmTail(event.monitor))) {
                event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            }
            for (const auto &entry : m_tracked) {
                const auto WINDOW = entry.window.lock();
                if (!WINDOW || WINDOW->m_monitor.lock() != event.monitor) {
                    continue;
                }
                const auto *transformer = transformerOf(WINDOW);
                if (transformer && transformer->active()) {
                    event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
                    break;
                }
            }
        });

    m_listenReload = Event::bus()->m_events.config.reloaded.listen([this] { onReload(); });

    for (const auto &window : Desktop::windowState()->windows()) {
        track(window);
    }
}

CPluginRuntime::~CPluginRuntime() {
    // Unload order: stop callbacks, unhook the close path, drop its records, then detach every
    // window.
    m_listenOpenLate.reset();
    m_listenClose.reset();
    m_listenDestroy.reset();
    m_listenRenderPreChecks.reset();
    m_listenRenderPre.reset();
    m_listenRenderPost.reset();
    m_listenReload.reset();
    m_listenMonitorLayout.reset();
    m_listenMonitorRemoved.reset();
    clearBubbleResidue();
    m_close->shutdown();
    detachAll();
    m_closeSolid.reset();
    m_openSolid.reset();
    m_shade.reset();
    m_sprites.reset();
    m_bubble.reset();
    m_bubbleResidue.reset();
}

bool CPluginRuntime::pluginEnabled() const {
    return m_config.enabled;
}

double CPluginRuntime::movementStrength() const {
    return with_live_style(m_config.style, [&](auto style) { return style.strength(*this); });
}

hyprimate::MaterialParams CPluginRuntime::effectiveMaterialParams() const {
    const auto &liquid = m_config.movement.liquid;
    return {
        .viscosity = liquid.viscosity,
        .elasticity = liquid.elasticity,
        .momentum = liquid.momentum,
        .variation = liquid.variation,
    };
}

hyprimate::ClothMovementParams CPluginRuntime::clothMovementParams() const {
    const auto &cloth = m_config.movement.cloth;
    return {
        .strength = cloth.strength,
        .flutter = cloth.flutter,
        .shading = cloth.shading,
        .variation = cloth.variation,
        .shine = cloth.shine,
        .springiness = cloth.springiness,
        .gravity = cloth.gravity,
    };
}

bool CPluginRuntime::movementEnabled() const {
    return pluginEnabled() && m_config.movement.enabled && materialUsable() &&
           movementStrength() > 0.0;
}

bool CPluginRuntime::openEnabled() const {
    return pluginEnabled() && !m_inert && m_config.open.enabled;
}

bool CPluginRuntime::closeEnabled() const {
    return pluginEnabled() && !m_inert && m_config.close.enabled;
}

bool CPluginRuntime::bubbleResidueEnabled() const {
    // Glass beads come from ruptures; film trails and free microbubbles from movement.
    return (openEnabled() || closeEnabled() || (movementEnabled() && !m_inert)) &&
           m_config.style == hyprimate::config::Style::Bubble;
}

bool CPluginRuntime::workspaceHasOtherWindows(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
            other->mapped() && !other->isHidden()) {
            return true;
        }
    }
    return false;
}

// Tiled windows on the workspace, as GOAL boxes, for the contact rule. Floating windows are not
// neighbours.
void CPluginRuntime::gatherNeighbours(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
            other->mapped() && !other->isHidden() && !other->isFloating()) {
            const CBox GOAL_BOX = other->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
            m_neighbours.push_back(
                {.x = GOAL_BOX.x, .y = GOAL_BOX.y, .w = GOAL_BOX.w, .h = GOAL_BOX.h});
        }
    }
}

void CPluginRuntime::gatherClothObstacles(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace &&
            other->mapped() && !other->isHidden()) {
            const CBox WINDOW_BOX = other->getFullWindowBoundingBox();
            m_neighbours.push_back(
                {.x = WINDOW_BOX.x, .y = WINDOW_BOX.y, .w = WINDOW_BOX.w, .h = WINDOW_BOX.h});
        }
    }
}

hyprimate::EdgeInput CPluginRuntime::edgeInputFor(
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
        gatherNeighbours(window);
    }
    return {
        .body = BOX(BODY),
        .capture = BOX(REST),
        .screen = BOX(MONITOR->logicalBox()),
        .workArea = BOX(MONITOR->logicalBoxMinusReserved()),
        .neighbours = m_neighbours,
        .origin = origin,
        .lastWindow = !workspaceHasOtherWindows(window),
    };
}

bool CPluginRuntime::materialUsable() const {
    return with_live_style(m_config.style, [&](auto style) { return style.usable(*this); });
}

bool CPluginRuntime::materialChanged(const CAnimationTransformer &transformer) const {
    return !transformer.builtFor(m_config.style, effectiveMaterialParams(), clothMovementParams());
}

bool CPluginRuntime::wobbleEnabled() const {
    return *m_wobble != 0;
}

void CPluginRuntime::notify(const std::string &text) const {
    HyprlandAPI::addNotification(m_handle, text, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
}

void CPluginRuntime::onReload() {
    m_config = hyprimate::config::readConfig(m_values);
    m_bubbleResidue->setEnabled(bubbleResidueEnabled());
    clearBubbleResidue();
    detachAll();
    m_close->clear();

    if (pluginEnabled() && wobbleEnabled()) {
        if (!m_inert) {
            notify(WOBBLE_MESSAGE);
        }
        m_inert = true;
        return;
    }

    m_inert = false;
    baselineAll();
}

hyprimate::EdgeSet CPluginRuntime::swarmSides(const PHLWINDOW &window, bool opening) const {
    const auto MONITOR = window->m_monitor.lock();
    if (!MONITOR) {
        return {};
    }
    // Alone on its workspace, the window has no side it appears from: the configured sides apply.
    if (!workspaceHasOtherWindows(window)) {
        return ::swarmSides(
            opening ? m_config.open.swarm.loneWindowSides : m_config.close.swarm.loneWindowSides);
    }
    m_neighbours.clear();
    if (!window->isFloating()) {
        gatherNeighbours(window);
    }
    const CBox BODY = window->geometricBox(opening ? Desktop::View::IGeometric::GEOMETRIC_GOAL
                                                   : Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return hyprimate::resolveEdgeSides(toBox(BODY), toBox(MONITOR->logicalBox()), m_neighbours);
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
    entry.tracker.track(toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
        toBox(window->getFullWindowBoundingBox()),
        monitor_id(window));
    m_tracked.push_back(entry);
}

void CPluginRuntime::forget(const PHLWINDOW &window) {
    std::erase_if(
        m_tracked, [&window](const STracked &entry) { return entry.window.get() == window.get(); });
}

void CPluginRuntime::rebaseline(STracked &entry, const PHLWINDOW &window) const {
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.tracker.baseline(
        toBox(CURRENT), toBox(window->getFullWindowBoundingBox()), monitor_id(window));
}

void CPluginRuntime::baselineAll() {
    for (auto &entry : m_tracked) {
        if (const auto WINDOW = entry.window.lock()) {
            rebaseline(entry, WINDOW);
        }
    }
}

bool CPluginRuntime::eligible(const PHLWINDOW &window) const {
    // Opening animations are deformable; the sampler rebaselines geometry while they run.
    return pluginEnabled() && !m_inert && window->mapped() &&
           !Fullscreen::controller()->isFullscreen(window) &&
           is_usable_box(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
}

void CPluginRuntime::onRenderPre(const PHLMONITOR &monitor) {
    // Config is read live: runtime edits (hl.config, hyprctl keyword) need not fire a reload. The
    // texture is rebuilt only when the value actually changes.
    m_config = hyprimate::config::readConfig(m_values);
    // Compile the bubble material before any bubble needs it: done mid-animation, the compile reads
    // as a stall.
    if (m_config.style == hyprimate::config::Style::Bubble) {
        m_bubble->warmUp();
    }
    const bool RESIDUE_ENABLED = bubbleResidueEnabled();
    m_bubbleResidue->setEnabled(RESIDUE_ENABLED);
    if (!RESIDUE_ENABLED) {
        clearBubbleResidue();
    } else {
        m_bubbleResidue->tick(clock::now(), monitor);
        if (!m_bubbleResidue->active(monitor)) {
            restoreBubbleScanout(monitor);
        }
    }
    m_openSolid->setColor(color_from_argb(m_config.open.liquid.color));
    m_closeSolid->setColor(color_from_argb(m_config.close.liquid.color));
    // Sampling runs on each window's own monitor only, so a window steps once per frame of the
    // monitor it is on.
    const auto NOW = clock::now();
    for (auto &entry : m_tracked) {
        const auto WINDOW = entry.window.lock();
        if (!WINDOW || WINDOW->m_monitor.lock() != monitor) {
            continue;
        }
        updateWindow(entry, WINDOW, NOW);
    }

    std::erase_if(m_tracked, [](const STracked &entry) { return entry.window.expired(); });

    // A live disable also drops retained closes, restoring native fades where still available.
    if (!closeEnabled() || !closeAnimationEnabled(m_config)) {
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
}

void CPluginRuntime::updateWindow(STracked &entry, const PHLWINDOW &window, clock::time_point now) {
    const double ELAPSED =
        std::max(0.0, std::chrono::duration<double>(now - entry.lastUpdate).count());
    entry.lastUpdate = now;
    const bool ANIMATING = window->presentation().animatingIn();
    // A window that is not stepping, not deforming, and not opening cannot change until it becomes
    // eligible or starts animating, so it skips the snapshot entirely.
    const auto *existing = transformerOf(window);
    const bool STEPPING = (existing != nullptr) && existing->active();
    if (!STEPPING && entry.tracker.dormant(ANIMATING) && !eligible(window)) {
        return;
    }

    const hyprimate::TrackDecision DECISION =
        entry.tracker.update(snapshotOf(window, ANIMATING, ELAPSED));
    if (DECISION.action == hyprimate::TrackAction::Rebaseline) {
        // Covers a material change, a stall, a movement jump, and a window that stopped being
        // deformable.
        detachWindow(entry, window);
        if (DECISION.begin_open) {
            beginOpening(entry, window);
        }
        return;
    }
    if (DECISION.cancel_opening) {
        if (auto *transformer = transformerOf(window)) {
            transformer->cancelOpening();
        }
    }
    if (DECISION.action == hyprimate::TrackAction::Opening) {
        beginOpening(entry, window);
        // A cloth opening may warp native popin geometry to its goal. Baseline the box actually
        // displayed.
        rebaseline(entry, window);
        stepWindow(entry, window, 0.0);
        return;
    }
    if (DECISION.action == hyprimate::TrackAction::Move) {
        recordMovement(entry, window, DECISION, ELAPSED);
    }
    stepWindow(entry, window, ELAPSED);
}

hyprimate::WindowSnapshot CPluginRuntime::snapshotOf(
    const PHLWINDOW &window, bool animating, double elapsed) {
    const auto *transformer = transformerOf(window);
    hyprimate::WindowSnapshot window_snapshot;
    window_snapshot.current =
        toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
    window_snapshot.capture = toBox(window->getFullWindowBoundingBox());
    window_snapshot.deformable = eligible(window);
    window_snapshot.animating_in = animating;
    with_live_style(
        m_config.style, [&](auto style) { style.snapshot(transformer, window_snapshot); });
    window_snapshot.material_changed = (transformer != nullptr) && materialChanged(*transformer);
    window_snapshot.open_enabled = openEnabled();
    window_snapshot.movement_enabled = movementEnabled();
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
        // Native hiding ends the slide, not the swarm's flight. Preserve its last screen-space
        // placement while the retained window image supplies the remaining creatures.
        const auto *tail = workspaceSwarmTail(window);
        if (tail != nullptr) {
            window_snapshot.workspace_offset = {
                .x = tail->workspaceCapture.x - window_snapshot.capture.x,
                .y = tail->workspaceCapture.y - window_snapshot.capture.y,
            };
        }
        window_snapshot.deformable =
            window_snapshot.deformable &&
            (WORKSPACE->visible() || window_snapshot.workspace_moving || (tail != nullptr));
    }
    return window_snapshot;
}

void CPluginRuntime::recordMovement(STracked &entry,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double elapsed) {
    const bool ENABLED = movementEnabled();
    const bool MOVING = decision.translated && ENABLED;
    auto *transformer = transformerOf(window);
    if (!MOVING && (transformer == nullptr)) {
        return;
    }
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    if (transformer == nullptr) {
        transformer = acquireTransformer(window);
    }
    if (decision.cancel_movement) {
        transformer->cancelMovement();
    }
    with_live_style(m_config.style, [&](auto style) {
        style.movement(*this, *transformer, window, decision, elapsed, ENABLED, MOVING);
    });
}

void CPluginRuntime::beginOpening(STracked &entry, const PHLWINDOW &window) {
    // Every open attempt starts from no edge, so an early return never leaves a stale origin
    // behind.
    entry.openEdge.reset();
    if (!openEnabled()) {
        return;
    }

    if (!with_live_style(
            m_config.style, [&](auto style) { return style.open(*this, entry, window); })) {
        return;
    }

    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
    }
    entry.footprint = worldFootprint(window);
    entry.hasFootprint = true;

    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

void CPluginRuntime::stepWindow(STracked &entry, const PHLWINDOW &window, double elapsed) {
    auto *transformer = transformerOf(window);
    if ((transformer == nullptr) || !transformer->active()) {
        if (transformer != nullptr) {
            transformer->cancelOpening();  // a driven open that went inactive (fullscreen, unmap)
        }
        // returns the layout box
        if (entry.hasFootprint) {
            g_pHyprRenderer->damageBox(entry.footprint);
            entry.hasFootprint = false;
            g_pHyprRenderer->damageWindow(window);
            scheduleAnimation(window);
        }
        return;
    }
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    if (!is_usable_box(CAPTURE)) {
        detachWindow(entry, window);
        return;
    }
    with_live_style(m_config.style, [&](auto style) {
        style.step(*this, *transformer, elapsed, {CAPTURE.w, CAPTURE.h}, movementEnabled());
    });
    if (auto *swarm = std::get_if<SSwarmStyle>(&transformer->styleState());
        (swarm != nullptr) && window->m_workspace && window->m_workspace->visible() &&
        !window->m_workspace->m_renderOffset->isBeingAnimated()) {
        swarm->workspaceSnapshot.reset();
        swarm->captureWorkspaceFrame = false;
    }
    // The step may drive the layout box. Baselining after it keeps that drive from reading as
    // window movement.
    entry.tracker.baseline(
        toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
        toBox(window->getFullWindowBoundingBox()),
        monitor_id(window));
    if (transformer->active()) {
        entry.footprint = worldFootprint(window);
        entry.hasFootprint = true;
    } else {
        // Settled fields have already restored the undeformed mesh; pruning drops the transformer.
        window->effects().transformers()->removeInactive();
        if (const auto MONITOR = window->m_monitor.lock()) {
            MONITOR->recheckSolitary();
        }
    }
    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

CAnimationTransformer *CPluginRuntime::transformerOf(const PHLWINDOW &window) const {
    return window->effects().transformers()->get<CAnimationTransformer>();
}

CAnimationTransformer *CPluginRuntime::acquireTransformer(const PHLWINDOW &window) {
    const auto &list = window->effects().transformers();
    if (auto *existing = list->get<CAnimationTransformer>()) {
        return existing;
    }
    // Each window draws from its own stream, so windows moved together (scrolling, workspace
    // slides) still differ.
    auto state = initialStyle(
        m_config.style, effectiveMaterialParams(), clothMovementParams(), m_nextSeed++);
    if (auto *bubble = std::get_if<SBubbleStyle>(&state)) {
        bubble->renderer = m_bubble;
        bubble->residue = m_bubbleResidue;
    }
    return list->emplace<CAnimationTransformer>(
        window->m_self, std::move(state), m_openSolid, m_shade, m_sprites);
}

CBox CPluginRuntime::worldFootprint(const PHLWINDOW &window) const {
    CBox box = window->getFullWindowBoundingBox();
    if (const auto *tail = workspaceSwarmTail(window)) {
        const auto &capture = tail->workspaceCapture;
        return window->effects().transformBoxForDamage(
            CBox{capture.x, capture.y, capture.w, capture.h});
    }
    const auto WORKSPACE = window->m_workspace;
    if (WORKSPACE && WORKSPACE->m_renderOffset->isBeingAnimated() &&
        !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)) {
        box.translate(WORKSPACE->m_renderOffset->value());
    }
    box.translate(window->presentation().floatingOffset());
    return window->effects().transformBoxForDamage(box);
}

void CPluginRuntime::scheduleAnimation(const PHLWINDOW &window) const {
    if (const auto MONITOR = window->m_monitor.lock()) {
        MONITOR->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }
}

void CPluginRuntime::detachWindow(STracked &entry, const PHLWINDOW &window) {
    const bool HAD_FOOTPRINT = entry.hasFootprint;
    if (HAD_FOOTPRINT) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    auto *transformer = transformerOf(window);
    if (transformer != nullptr) {
        transformer->markDetached();
        window->effects().transformers()->removeInactive();
        if (const auto MONITOR = window->m_monitor.lock()) {
            MONITOR->recheckSolitary();
        }
        // No pointer use after pruning: plugin vtables are gone before unload.
    }
    if (HAD_FOOTPRINT || (transformer != nullptr)) {
        g_pHyprRenderer->damageWindow(window);
        scheduleAnimation(window);
    }
}

void CPluginRuntime::detachAll() {
    for (auto &entry : m_tracked) {
        if (const auto WINDOW = entry.window.lock()) {
            detachWindow(entry, WINDOW);
        }
    }
}

void CPluginRuntime::restoreBubbleScanout(const PHLMONITOR &monitor) {
    std::erase_if(m_residueInhibited, [&](const auto &entry) {
        const auto CANDIDATE = entry.lock();
        if (!CANDIDATE) {
            return true;
        }
        if (CANDIDATE != monitor) {
            return false;
        }
        CANDIDATE->recheckSolitary();
        return true;
    });
}

void CPluginRuntime::clearBubbleResidue() {
    m_bubbleResidue->clear();
    for (const auto &entry : m_residueInhibited) {
        if (const auto MONITOR = entry.lock()) {
            MONITOR->recheckSolitary();
        }
    }
    m_residueInhibited.clear();
}

SSwarmStyle *CPluginRuntime::workspaceSwarmTail(const PHLWINDOW &window) const {
    if (!window || !window->mapped() || !window->m_workspace || m_inert || !m_config.enabled ||
        !m_config.movement.enabled || !m_config.movement.workspaceMovement ||
        m_config.style != hyprimate::config::Style::Swarm || window->m_workspace->visible() ||
        window->m_workspace->m_renderOffset->isBeingAnimated()) {
        return nullptr;
    }
    auto *transformer = transformerOf(window);
    auto *state =
        (transformer != nullptr) ? std::get_if<SSwarmStyle>(&transformer->styleState()) : nullptr;
    if ((state == nullptr) || !state->swarm.active() || !state->workspaceSnapshot) {
        return nullptr;
    }
    const auto BOUNDS = state->swarm.bounds();
    return BOUNDS.w > 0.0 && BOUNDS.h > 0.0 ? state : nullptr;
}

bool CPluginRuntime::hasWorkspaceSwarmTail(const PHLMONITOR &monitor) const {
    return std::ranges::any_of(m_tracked, [&](const auto &entry) {
        const auto WINDOW = entry.window.lock();
        return WINDOW && WINDOW->m_monitor.lock() == monitor && workspaceSwarmTail(WINDOW);
    });
}

void CPluginRuntime::queueWorkspaceSwarmTails(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor) {
    for (const auto &entry : m_tracked) {
        const auto WINDOW = entry.window.lock();
        if (!WINDOW || WINDOW->m_monitor.lock() != monitor ||
            !ctx.shouldRenderContent(WINDOW->m_ruleApplicator->noScreenShare().valueOrDefault())) {
            continue;
        }
        auto *state = workspaceSwarmTail(WINDOW);
        if (state == nullptr) {
            continue;
        }
        const auto *sheet = m_sprites->sheet(state->creature);
        const auto ATLAS =
            (sheet != nullptr) ? m_sprites->texture(state->creature) : SP<Render::ITexture>{};
        const auto TEXTURE = state->workspaceSnapshot->getTexture();
        if ((sheet == nullptr) || !ATLAS || !TEXTURE) {
            continue;
        }
        const auto &capture = state->workspaceCapture;
        const double SCALE = monitor->m_scale;
        const CBox SOURCE = CBox{capture.x, capture.y, capture.w, capture.h}
                                .translate(-monitor->m_position)
                                .scale(SCALE);
        const auto BOUNDS = state->swarm.bounds();
        const CBox OUTPUT{SOURCE.x + (BOUNDS.x * SCALE),
            SOURCE.y + (BOUNDS.y * SCALE),
            BOUNDS.w * SCALE,
            BOUNDS.h * SCALE};
        fillSwarmMeshes(state->swarm,
            *sheet,
            SOURCE,
            OUTPUT,
            {.x = SOURCE.x + state->snapshotOffset.x, .y = SOURCE.y + state->snapshotOffset.y},
            {.x = TEXTURE->m_size.x, .y = TEXTURE->m_size.y},
            state->meshes);
        CSwarmClosePassElement::SData data;
        data.tex = TEXTURE;
        data.atlas = ATLAS;
        data.sprites = m_sprites;
        data.layerSize = swarmLayerSize(SOURCE);
        data.box = OUTPUT;
        data.meshes = &state->meshes;
        data.scale = SCALE;
        data.damage = CRegion{OUTPUT};
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CSwarmClosePassElement>(std::move(data)));
    }
}
