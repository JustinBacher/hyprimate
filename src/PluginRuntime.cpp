#include "PluginRuntime.hpp"

#include "LiquidMesh.hpp"

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {

constexpr const char *WOBBLE_MESSAGE = "[liquid-glass] Disable decoration:wobble:enabled before enabling liquid-glass";

bool isUsableBox(const CBox &box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) &&
           box.w > 0.0 && box.h > 0.0;
}

// Identity of the window's monitor; zero when it has none, which matches no live monitor.
std::uintptr_t monitorId(const PHLWINDOW &window) {
    return reinterpret_cast<std::uintptr_t>(window->m_monitor.lock().get());
}

// ARGB config value to a float colour; channel bytes are unpacked, not treated as one packed integer.
CHyprColor colorFromArgb(std::uint32_t argb) {
    const auto channel = [argb](int shift) { return static_cast<float>((argb >> shift) & 0xFFU) / 255.F; };
    return CHyprColor(channel(16), channel(8), channel(0), channel(24));
}

namespace cfg = liquid_glass::config;

liquid_glass::PaperParams toPaper(const cfg::STransitionPaper &paper) {
    return {.duration = paper.duration,
            .crumpliness = paper.crumpliness,
            .shading = paper.shading,
            .variation = paper.variation};
}

liquid_glass::ClothParams toCloth(const cfg::STransitionCloth &cloth) {
    return {.duration = cloth.duration,
            .flutter = cloth.flutter,
            .shading = cloth.shading,
            .variation = cloth.variation,
            .shine = cloth.shine};
}

liquid_glass::TransitionParams toTransition(double elasticity, double momentum) {
    return {.elasticity = elasticity, .momentum = momentum};
}

liquid_glass::CloseSide closeSide(cfg::Side side) {
    switch (side) {
    case cfg::Side::Auto:
        return liquid_glass::CloseSide::Auto;
    case cfg::Side::Nearest:
        return liquid_glass::CloseSide::Nearest;
    case cfg::Side::Origin:
        return liquid_glass::CloseSide::Origin;
    case cfg::Side::Top:
        return liquid_glass::CloseSide::Top;
    case cfg::Side::Bottom:
        return liquid_glass::CloseSide::Bottom;
    case cfg::Side::Left:
        return liquid_glass::CloseSide::Left;
    case cfg::Side::Right:
        return liquid_glass::CloseSide::Right;
    }
    return liquid_glass::CloseSide::Auto;
}

} // namespace

CPluginRuntime::CPluginRuntime(HANDLE handle) : m_handle(handle) {
    std::random_device entropy;
    m_nextSeed = (std::uint64_t{entropy()} << 32) | entropy();
    liquid_glass::config::registerConfigValues(m_handle, m_values);
    m_config = liquid_glass::config::readConfig(m_values);

    if (pluginEnabled() && wobbleEnabled()) {
        notify(WOBBLE_MESSAGE);
        throw std::runtime_error(WOBBLE_MESSAGE);
    }

    // Close transitions are optional: if the compositor symbols are not found, movement deformation still runs.
    m_close = std::make_unique<CLiquidCloseRenderer>(m_handle, m_closeSolid, m_shade);
    std::string closeError;
    if (!m_close->install(closeError))
        notify("[liquid-glass] close transitions unavailable: " + closeError);

    // Listeners are registered last so a failed constructor never leaves callbacks pointing at a half-built runtime.
    m_listenOpenLate = Event::bus()->m_events.window.openLate.listen([this](PHLWINDOW window) { track(window); });

    m_listenClose = Event::bus()->m_events.window.close.listen([this](PHLWINDOW window) {
        if (auto *entry = find(window)) {
            // Staging must precede detachWindow, which marks the transformer detached. A resting window
            // closes from a fresh field; a moving one carries its current displacement and velocity.
            // Close drains rigidly: emerge_drag is an open-only option, so the close drives no travel.
            if (closeEnabled() && eligible(window)) {
                auto *transformer = transformerOf(window);
                // The window's own state continues into the close only when it was built for the configured style.
                const bool matched = transformer && !materialChanged(*transformer);
                if (m_config.style != cfg::Style::Liquid) {
                    const CBox                                      capture = window->getFullWindowBoundingBox();
                    std::optional<liquid_glass::ClothCloseGeometry> closeGeometry;
                    if (m_config.style == cfg::Style::Cloth) {
                        // Resolve first: edgeFrameFor reuses m_neighbours, which is then filled with collision boxes.
                        const auto frame = edgeFrameFor(window, false, entry->openEdge);
                        const auto monitor = window->m_monitor.lock();
                        if (frame && monitor) {
                            const CBox screen = monitor->logicalBox();
                            closeGeometry =
                                liquid_glass::ClothCloseGeometry{frame->edge, {screen.x, screen.y, screen.w, screen.h}};
                        }
                    }
                    m_neighbours.clear();
                    if (m_config.style == cfg::Style::Cloth)
                        gatherClothObstacles(window);
                    m_close->stageSheet(
                        window,
                        matched ? transformer->takeStyle()
                                : initialStyle(m_config.style, effectiveMaterialParams(), clothMovementParams(), 0),
                        toPaper(m_config.close.paper), toCloth(m_config.close.cloth), m_nextSeed++,
                        {capture.x, capture.y, capture.w, capture.h}, m_neighbours, closeGeometry);
                } else {
                    const auto &close = m_config.close.liquid;
                    const bool  continuing = matched && transformer->transitionActive();
                    const auto  closeFrame = continuing ? std::optional<liquid_glass::EdgeFrame>{}
                                                        : edgeFrameFor(window, false, entry->openEdge);
                    const auto  params = toTransition(close.elasticity, close.momentum);
                    if (matched && transformer->active())
                        m_close->stage(window, transformer->closePose(), close.strength, closeFrame, close.splash,
                                       params);
                    else {
                        const liquid_glass::LiquidMesh     mesh{effectiveMaterialParams()};
                        const liquid_glass::EdgeTransition transition;
                        m_close->stage(window, liquid_glass::LiquidPose{mesh, transition}, close.strength, closeFrame,
                                       close.splash, params);
                    }
                }
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

    // Everything advances once per rendered frame of its monitor, after native animations have ticked and before the
    // frame collects damage. Animation ticks are not used: a self-requested tick re-fires every millisecond.
    m_listenRenderPre = Event::bus()->m_events.render.pre.listen([this](PHLMONITOR monitor) { onRenderPre(monitor); });
    // Requests made in render.pre can be consumed by the current commit. Request the next frame while rendering is
    // active so Hyprland retains it; stop as soon as the last transformer on this monitor settles.
    m_listenRenderPost = Event::bus()->m_events.render.stage.listen([this](const Event::SRenderStageEvent &event) {
        if (event.stage != RENDER_POST)
            return;
        for (const auto &entry : m_tracked) {
            const auto window = entry.window.lock();
            if (!window || window->m_monitor.lock() != event.monitor)
                continue;
            const auto *transformer = transformerOf(window);
            if (transformer && transformer->active()) {
                event.monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
                break;
            }
        }
    });

    m_listenReload = Event::bus()->m_events.config.reloaded.listen([this]() { onReload(); });

    for (const auto &window : Desktop::windowState()->windows())
        track(window);
}

CPluginRuntime::~CPluginRuntime() {
    // Unload order: stop callbacks, unhook the close path, drop its records, then detach every window.
    m_listenOpenLate.reset();
    m_listenClose.reset();
    m_listenDestroy.reset();
    m_listenRenderPre.reset();
    m_listenRenderPost.reset();
    m_listenReload.reset();
    m_close->shutdown();
    detachAll();
    m_closeSolid.reset();
    m_openSolid.reset();
    m_shade.reset();
}

bool CPluginRuntime::pluginEnabled() const { return m_config.enabled; }

double CPluginRuntime::movementStrength() const {
    switch (m_config.style) {
    case cfg::Style::Liquid:
        return m_config.movement.liquid.strength;
    case cfg::Style::Cloth:
        return m_config.movement.cloth.strength;
    case cfg::Style::Paper:
        return 0.0; // paper windows move undeformed
    }
    return 0.0;
}

liquid_glass::MaterialParams CPluginRuntime::effectiveMaterialParams() const {
    const auto &liquid = m_config.movement.liquid;
    return {.viscosity = liquid.viscosity,
            .elasticity = liquid.elasticity,
            .momentum = liquid.momentum,
            .variation = liquid.variation};
}

liquid_glass::ClothMovementParams CPluginRuntime::clothMovementParams() const {
    const auto &cloth = m_config.movement.cloth;
    return {.strength = cloth.strength,
            .flutter = cloth.flutter,
            .shading = cloth.shading,
            .variation = cloth.variation,
            .shine = cloth.shine};
}

bool CPluginRuntime::movementEnabled() const {
    return pluginEnabled() && m_config.movement.enabled && materialUsable() && movementStrength() > 0.0;
}

bool CPluginRuntime::openEnabled() const { return pluginEnabled() && !m_inert && m_config.open.enabled; }

bool CPluginRuntime::closeEnabled() const { return pluginEnabled() && !m_inert && m_config.close.enabled; }

liquid_glass::Edge CPluginRuntime::firstWindowEdge() const {
    switch (m_config.open.liquid.firstWindowSide) {
    case cfg::FirstWindowSide::Top:
        return liquid_glass::Edge::Top;
    case cfg::FirstWindowSide::Left:
        return liquid_glass::Edge::Left;
    case cfg::FirstWindowSide::Right:
        return liquid_glass::Edge::Right;
    case cfg::FirstWindowSide::Bottom:
        return liquid_glass::Edge::Bottom;
    }
    return liquid_glass::Edge::Bottom;
}

bool CPluginRuntime::workspaceHasOtherWindows(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() &&
            !other->isHidden())
            return true;
    }
    return false;
}

// Tiled windows on the workspace, as GOAL boxes, for the contact rule. Floating windows are not neighbours.
void CPluginRuntime::gatherNeighbours(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() &&
            !other->isHidden() && !other->isFloating()) {
            const CBox b = other->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
            m_neighbours.push_back({b.x, b.y, b.w, b.h});
        }
    }
}

void CPluginRuntime::gatherClothObstacles(const PHLWINDOW &window) const {
    for (const auto &other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() &&
            !other->isHidden()) {
            const CBox b = other->getFullWindowBoundingBox();
            m_neighbours.push_back({b.x, b.y, b.w, b.h});
        }
    }
}

CBox CPluginRuntime::edgeBoundsBox(const PHLMONITOR &monitor, bool opening) const {
    const auto bounds = opening ? m_config.open.liquid.edgeBounds : m_config.close.liquid.edgeBounds;
    if (bounds == cfg::EdgeBounds::WorkArea)
        return monitor->logicalBoxMinusReserved();
    return monitor->logicalBox();
}

std::optional<liquid_glass::EdgeFrame> CPluginRuntime::edgeFrameFor(const PHLWINDOW &window, bool opening,
                                                                    std::optional<liquid_glass::Edge> origin) const {
    const auto monitor = window->m_monitor.lock();
    if (!monitor)
        return std::nullopt;
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox body = opening ? window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL) : current;
    const CBox capture = window->getFullWindowBoundingBox();
    // Decoration margins (border, shadow) do not scale with native popin, so they carry over to the rest box.
    const CBox rest{body.x + (capture.x - current.x), body.y + (capture.y - current.y),
                    body.w + (capture.w - current.w), body.h + (capture.h - current.h)};
    const auto box = [](const CBox &b) { return liquid_glass::Box{b.x, b.y, b.w, b.h}; };

    m_neighbours.clear();
    if (!window->isFloating())
        gatherNeighbours(window);
    if (!opening)
        return liquid_glass::resolveCloseEdgeFrame(box(body), box(rest), box(edgeBoundsBox(monitor, false)),
                                                   m_neighbours, firstWindowEdge(),
                                                   closeSide(m_config.close.liquid.side), origin);
    return liquid_glass::resolveEdgeFrame(box(body), box(rest), box(edgeBoundsBox(monitor, opening)), m_neighbours,
                                          firstWindowEdge(), !workspaceHasOtherWindows(window));
}

bool CPluginRuntime::materialUsable() const {
    if (m_config.style == cfg::Style::Cloth) {
        const auto params = clothMovementParams();
        return std::isfinite(params.flutter) && std::isfinite(params.shading) && std::isfinite(params.variation);
    }
    const auto params = effectiveMaterialParams();
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) && std::isfinite(params.momentum) &&
           std::isfinite(params.variation);
}

bool CPluginRuntime::materialChanged(const CLiquidGlassTransformer &transformer) const {
    return !transformer.builtFor(m_config.style, effectiveMaterialParams(), clothMovementParams());
}

liquid_glass::Vec2 CPluginRuntime::movementGrab(const PHLWINDOW &window, const CBox &capture, Vector2D delta) const {
    if (window->isFloating() && g_layoutManager && g_pInputManager) {
        const auto &drag = g_layoutManager->dragController();
        const auto  target = drag ? drag->target() : nullptr;
        if (drag && drag->mode() == MBIND_MOVE && target && target->window() == window) {
            const auto cursor = g_pInputManager->getMouseCoordsInternal();
            if (std::isfinite(cursor.x) && std::isfinite(cursor.y))
                return {std::clamp((cursor.x - capture.x) / capture.w, 0.0, 1.0),
                        std::clamp((cursor.y - capture.y) / capture.h, 0.0, 1.0)};
        }
    }
    // Anything else is pulled by its leading side: the corner when it travels diagonally, otherwise some point along
    // the leading edge, drawn afresh for each movement.
    std::uint64_t seed = m_nextSeed;
    const double  along = 0.15 + 0.7 * liquid_glass::unitDraw(seed);
    const double  x = delta.x > 0 ? 1.0 : 0.0, y = delta.y > 0 ? 1.0 : 0.0;
    if (std::abs(delta.x) >= 2 * std::abs(delta.y))
        return {x, along};
    if (std::abs(delta.y) >= 2 * std::abs(delta.x))
        return {along, y};
    return {x, y};
}

bool CPluginRuntime::wobbleEnabled() const { return *m_wobble != 0; }

void CPluginRuntime::notify(const std::string &text) const {
    HyprlandAPI::addNotification(m_handle, text, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
}

void CPluginRuntime::onReload() {
    m_config = liquid_glass::config::readConfig(m_values);
    detachAll();
    m_close->clear();

    if (pluginEnabled() && wobbleEnabled()) {
        if (!m_inert)
            notify(WOBBLE_MESSAGE);
        m_inert = true;
        return;
    }

    m_inert = false;
    baselineAll();
}

CPluginRuntime::STracked *CPluginRuntime::find(const PHLWINDOW &window) {
    for (auto &entry : m_tracked) {
        if (entry.window.get() == window.get())
            return &entry;
    }
    return nullptr;
}

void CPluginRuntime::track(const PHLWINDOW &window) {
    if (!window)
        return;
    if (find(window))
        return;

    STracked entry;
    entry.window = PHLWINDOWREF{window};
    entry.tracker.track(toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
                        toBox(window->getFullWindowBoundingBox()), monitorId(window));
    m_tracked.push_back(entry);
}

void CPluginRuntime::forget(const PHLWINDOW &window) {
    std::erase_if(m_tracked, [&window](const STracked &entry) { return entry.window.get() == window.get(); });
}

void CPluginRuntime::rebaseline(STracked &entry, const PHLWINDOW &window) const {
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.tracker.baseline(toBox(current), toBox(window->getFullWindowBoundingBox()), monitorId(window));
}

void CPluginRuntime::baselineAll() {
    for (auto &entry : m_tracked) {
        if (const auto window = entry.window.lock())
            rebaseline(entry, window);
    }
}

bool CPluginRuntime::eligible(const PHLWINDOW &window) const {
    // Opening animations are deformable; the sampler rebaselines geometry while they run.
    return pluginEnabled() && !m_inert && window->mapped() && !Fullscreen::controller()->isFullscreen(window) &&
           isUsableBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
}

void CPluginRuntime::onRenderPre(const PHLMONITOR &monitor) {
    // Config is read live: runtime edits (hl.config, hyprctl keyword) need not fire a reload. The texture is
    // rebuilt only when the value actually changes.
    m_config = liquid_glass::config::readConfig(m_values);
    m_openSolid->setColor(colorFromArgb(m_config.open.liquid.color));
    m_closeSolid->setColor(colorFromArgb(m_config.close.liquid.color));
    // Sampling runs on each window's own monitor only, so a window steps once per frame of the monitor it is on.
    const auto now = clock::now();
    for (auto &entry : m_tracked) {
        const auto window = entry.window.lock();
        if (!window || window->m_monitor.lock() != monitor)
            continue;
        updateWindow(entry, window, now);
    }

    std::erase_if(m_tracked, [](const STracked &entry) { return entry.window.expired(); });

    // A live disable also drops retained closes, restoring native fades where still available.
    if (!closeEnabled() || (m_config.style == cfg::Style::Liquid && m_config.close.liquid.strength <= 0.0)) {
        m_close->clear();
    } else {
        std::vector<CBox> damage;
        m_close->tick(now, monitor, damage);
        for (const auto &box : damage) {
            g_pHyprRenderer->damageBox(box);
            for (const auto &candidate : State::monitorState()->monitors()) {
                if (candidate->logicalBox().overlaps(box))
                    candidate->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            }
        }
    }
}

void CPluginRuntime::updateWindow(STracked &entry, const PHLWINDOW &window, clock::time_point now) {
    const double elapsed = std::max(0.0, std::chrono::duration<double>(now - entry.lastUpdate).count());
    entry.lastUpdate = now;
    const bool animating = window->presentation().animatingIn();
    // A window that is not stepping, not deforming, and not opening cannot change until it becomes eligible or
    // starts animating, so it skips the snapshot entirely.
    const auto *existing = transformerOf(window);
    const bool  stepping = existing && existing->active();
    if (!stepping && entry.tracker.dormant(animating) && !eligible(window))
        return;

    const liquid_glass::TrackDecision decision = entry.tracker.update(snapshotOf(window, animating, elapsed));
    if (decision.action == liquid_glass::TrackAction::Rebaseline) {
        // Covers a material change, a stall, a movement jump, and a window that stopped being deformable.
        detachWindow(entry, window);
        if (decision.beginOpen)
            beginOpening(entry, window);
        return;
    }
    if (decision.cancelOpening) {
        if (auto *transformer = transformerOf(window))
            transformer->cancelOpening();
    }
    if (decision.action == liquid_glass::TrackAction::Opening) {
        beginOpening(entry, window);
        // A cloth opening may warp native popin geometry to its goal. Baseline the box actually displayed.
        rebaseline(entry, window);
        stepWindow(entry, window, 0.0);
        return;
    }
    if (decision.action == liquid_glass::TrackAction::Move)
        recordMovement(entry, window, decision, elapsed);
    stepWindow(entry, window, elapsed);
}

liquid_glass::WindowSnapshot CPluginRuntime::snapshotOf(const PHLWINDOW &window, bool animating, double elapsed) {
    const auto                  *transformer = transformerOf(window);
    liquid_glass::WindowSnapshot s;
    s.current = toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
    s.capture = toBox(window->getFullWindowBoundingBox());
    s.deformable = eligible(window);
    s.animatingIn = animating;
    s.clothHandoff = m_config.style == cfg::Style::Cloth && transformer &&
                     (transformer->clothOpening() || transformer->clothMoving());
    s.transitionBusy =
        transformer && (transformer->transitionActive() || (transformer->sheetActive() && !s.clothHandoff));
    s.materialChanged = transformer && materialChanged(*transformer);
    s.clothMoving = transformer && transformer->clothMoving();
    s.openEnabled = openEnabled();
    s.movementEnabled = movementEnabled();
    s.monitor = monitorId(window);
    s.elapsed = elapsed;
    return s;
}

void CPluginRuntime::recordMovement(STracked &entry, const PHLWINDOW &window,
                                    const liquid_glass::TrackDecision &decision, double elapsed) {
    const bool enabled = movementEnabled();
    const bool moving = decision.translated && enabled;
    auto      *transformer = transformerOf(window);
    if (!moving && !transformer)
        return;
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    if (!transformer)
        transformer = acquireTransformer(window);
    const CBox     capture = window->getFullWindowBoundingBox();
    const CBox     current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const Vector2D delta{current.x - decision.previousBox.x, current.y - decision.previousBox.y};
    if (decision.cancelMovement)
        transformer->cancelMovement();
    switch (m_config.style) {
    case cfg::Style::Cloth:
        if (moving) {
            // A pull after a pause may take the sheet somewhere else: it is pinched afresh there.
            const auto grab = movementGrab(window, capture, delta);
            if (!transformer->clothMoving() || transformer->clothRegrabs(grab))
                transformer->beginClothMovement(m_nextSeed++, decision.previousCapture, grab);
        }
        if (enabled && transformer->clothMoving())
            transformer->updateClothMovement(toBox(capture), elapsed);
        break;
    case cfg::Style::Liquid:
        transformer->recordLiquidMove({delta.x, delta.y}, {capture.w, capture.h}, movementStrength(), moving, enabled);
        break;
    case cfg::Style::Paper:
        break; // paper windows move undeformed
    }
}

void CPluginRuntime::beginOpening(STracked &entry, const PHLWINDOW &window) {
    // Every open attempt starts from no edge, so an early return never leaves a stale origin behind.
    entry.openEdge.reset();
    if (!openEnabled())
        return;

    switch (m_config.style) {
    case cfg::Style::Paper: {
        // Paper needs no edge and never drives the box: it starts as a ball at the window's own box.
        const CBox capture = window->getFullWindowBoundingBox();
        acquireTransformer(window)->beginPaperOpen(toPaper(m_config.open.paper), m_nextSeed++, {capture.w, capture.h});
        break;
    }
    case cfg::Style::Cloth: {
        // Cloth opens in place, but keeps the same logical origin as liquid for close side=origin.
        if (const auto frame = edgeFrameFor(window, true))
            entry.openEdge = frame->edge;
        const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const CBox capture = window->getFullWindowBoundingBox();
        // Native popin changes both size and position. The physical top attachment belongs to the final box;
        // retain its decoration margins and stop native geometry motion only once the cloth actually starts.
        const CBox rest{goal.x + (capture.x - current.x), goal.y + (capture.y - current.y),
                        goal.w + (capture.w - current.w), goal.h + (capture.h - current.h)};
        if (!isUsableBox(goal) || !isUsableBox(rest))
            return;
        m_neighbours.clear();
        gatherClothObstacles(window);
        auto *transformer = acquireTransformer(window);
        transformer->beginClothOpen(toCloth(m_config.open.cloth), m_nextSeed++, {rest.x, rest.y, rest.w, rest.h},
                                    m_neighbours);
        if (transformer->sheetActive()) {
            g_pHyprRenderer->damageBox(capture);
            window->positionAnimation()->setValueAndWarp(goal.pos());
            window->sizeAnimation()->setValueAndWarp(goal.size());
            rebaseline(entry, window);
        }
        break;
    }
    case cfg::Style::Liquid: {
        const auto frame = edgeFrameFor(window, true);
        if (!frame)
            return; // no usable edge: the native opening runs, movement deformation unaffected
        entry.openEdge = frame->edge;
        const auto &open = m_config.open.liquid;
        if (open.strength <= 0.0)
            return;
        auto      *transformer = acquireTransformer(window);
        const CBox capture = window->getFullWindowBoundingBox();
        transformer->beginOpen(*frame, toTransition(open.elasticity, open.momentum), open.strength,
                               {capture.w, capture.h});
        break;
    }
    }

    if (entry.hasFootprint)
        g_pHyprRenderer->damageBox(entry.footprint);
    entry.footprint = worldFootprint(window);
    entry.hasFootprint = true;

    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

void CPluginRuntime::stepWindow(STracked &entry, const PHLWINDOW &window, double elapsed) {
    auto *transformer = transformerOf(window);
    if (!transformer || !transformer->active()) {
        if (transformer)
            transformer->cancelOpening(); // a driven open that went inactive (fullscreen, unmap) returns the layout box
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
    const CBox capture = window->getFullWindowBoundingBox();
    if (!isUsableBox(capture)) {
        detachWindow(entry, window);
        return;
    }
    const bool enabled = movementEnabled();
    if (!enabled && transformer->clothMoving())
        transformer->cancelMovement();
    transformer->step(elapsed, {capture.w, capture.h}, movementStrength(),
                      enabled && m_config.style == cfg::Style::Liquid, m_config.open.liquid.emergeDrag);
    // The step may drive the layout box. Baselining after it keeps that drive from reading as window movement.
    entry.tracker.baseline(toBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT)),
                           toBox(window->getFullWindowBoundingBox()), monitorId(window));
    if (transformer->active()) {
        entry.footprint = worldFootprint(window);
        entry.hasFootprint = true;
    } else {
        // Settled fields have already restored the undeformed mesh; pruning drops the transformer.
        window->effects().transformers()->removeInactive();
    }
    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

CLiquidGlassTransformer *CPluginRuntime::transformerOf(const PHLWINDOW &window) const {
    return window->effects().transformers()->get<CLiquidGlassTransformer>();
}

CLiquidGlassTransformer *CPluginRuntime::acquireTransformer(const PHLWINDOW &window) {
    const auto &list = window->effects().transformers();
    if (auto *existing = list->get<CLiquidGlassTransformer>())
        return existing;
    // Each window draws from its own stream, so windows moved together (scrolling, workspace slides) still differ.
    return list->emplace<CLiquidGlassTransformer>(
        window->m_self, initialStyle(m_config.style, effectiveMaterialParams(), clothMovementParams(), m_nextSeed++),
        m_openSolid, m_shade);
}

CBox CPluginRuntime::worldFootprint(const PHLWINDOW &window) const {
    CBox       box = window->getFullWindowBoundingBox();
    const auto workspace = window->m_workspace;
    if (workspace && workspace->m_renderOffset->isBeingAnimated() &&
        !(window->m_state & Desktop::View::WINDOW_STATE_PINNED))
        box.translate(workspace->m_renderOffset->value());
    box.translate(window->presentation().floatingOffset());
    return window->effects().transformBoxForDamage(box);
}

void CPluginRuntime::scheduleAnimation(const PHLWINDOW &window) const {
    if (const auto monitor = window->m_monitor.lock())
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}

void CPluginRuntime::detachWindow(STracked &entry, const PHLWINDOW &window) {
    const bool hadFootprint = entry.hasFootprint;
    if (hadFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    auto *transformer = transformerOf(window);
    if (transformer) {
        transformer->markDetached();
        window->effects().transformers()->removeInactive();
        // No pointer use after pruning: plugin vtables are gone before unload.
    }
    if (hadFootprint || transformer) {
        g_pHyprRenderer->damageWindow(window);
        scheduleAnimation(window);
    }
}

void CPluginRuntime::detachAll() {
    for (auto &entry : m_tracked) {
        if (const auto window = entry.window.lock())
            detachWindow(entry, window);
    }
}
