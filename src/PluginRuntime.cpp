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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {

constexpr const char *WOBBLE_MESSAGE = "[liquid-glass] Disable decoration:wobble:enabled before enabling liquid-glass";
constexpr double      MAX_JUMP_FACTOR = 1.5;

bool isUsableBox(const CBox &box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) &&
           box.w > 0.0 && box.h > 0.0;
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
    return {
        .duration = cloth.duration, .flutter = cloth.flutter, .shading = cloth.shading, .variation = cloth.variation};
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
                    const bool  continuing = matched && transformer->transition().active();
                    const auto  closeFrame = continuing ? std::optional<liquid_glass::EdgeFrame>{}
                                                        : edgeFrameFor(window, false, entry->openEdge);
                    const auto  params = toTransition(close.elasticity, close.momentum);
                    if (matched && transformer->active())
                        m_close->stage(window, transformer->closePose(), close.strength, closeFrame, close.splash,
                                       params);
                    else {
                        const liquid_glass::LiquidMesh     mesh{effectiveMaterialParams()};
                        const liquid_glass::EdgeTransition transition;
                        m_close->stage(window, SLiquidPose{mesh, transition}, close.strength, closeFrame, close.splash,
                                       params);
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
    return {
        .strength = cloth.strength, .flutter = cloth.flutter, .shading = cloth.shading, .variation = cloth.variation};
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
    // An axis without travel chooses the top/left side. The selected corner stays fixed for this run.
    return {delta.x > 0 ? 1.0 : 0.0, delta.y > 0 ? 1.0 : 0.0};
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
    // Starts false so a window that is already opening registers the rising edge on its first sample.
    entry.wasAnimatingIn = false;
    baseline(entry, window);
    m_tracked.push_back(entry);
}

void CPluginRuntime::forget(const PHLWINDOW &window) {
    std::erase_if(m_tracked, [&window](const STracked &entry) { return entry.window.get() == window.get(); });
}

void CPluginRuntime::baseline(STracked &entry, const PHLWINDOW &window) const {
    entry.lastBox = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.lastCapture = window->getFullWindowBoundingBox();
    entry.monitor = window->m_monitor;
    entry.baselined = true;
    entry.lastAdvance = std::chrono::steady_clock::now();
    entry.lastSample = entry.lastAdvance;
}

void CPluginRuntime::baselineAll() {
    for (auto &entry : m_tracked) {
        if (const auto window = entry.window.lock())
            baseline(entry, window);
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
    // Every monitor's frame samples all windows: sampling also handles disable/fullscreen/config eligibility
    // transitions. Only windows on the monitor about to draw advance, so each animation steps once per its frame.
    const auto now = clock::now();
    for (auto &entry : m_tracked) {
        const auto window = entry.window.lock();
        if (!window)
            continue;

        sampleWindow(entry, window);
        if (entry.monitor.lock() == monitor)
            advanceWindow(entry, window, now);
    }

    std::erase_if(m_tracked, [](const STracked &entry) { return entry.window.expired(); });

    // A live disable also drops retained closes, restoring native fades where still available.
    if (!closeEnabled() || (m_config.style == cfg::Style::Liquid && m_config.close.liquid.strength <= 0.0)) {
        m_close->clear();
    } else if (m_close->tick(now, monitor, m_closeMonitors)) {
        for (const auto &monitorRef : m_closeMonitors) {
            if (const auto damaged = monitorRef.lock()) {
                g_pHyprRenderer->damageMonitor(damaged);
                damaged->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            }
        }
    }
}

void CPluginRuntime::sampleWindow(STracked &entry, const PHLWINDOW &window) {
    const auto now = clock::now();
    const bool animating = window->presentation().animatingIn();
    const bool wasAnimating = entry.wasAnimatingIn;
    const bool openingEdge = animating && !wasAnimating;
    entry.wasAnimatingIn = animating;

    const bool     canDeform = eligible(window);
    const CBox     current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox     capture = window->getFullWindowBoundingBox();
    const Vector2D captureSize = capture.size();
    const double   sampleElapsed = std::chrono::duration<double>(now - entry.lastSample).count();

    if (!canDeform || !entry.wasEligible || !entry.baselined || !isUsableBox(entry.lastBox) || !isUsableBox(current) ||
        !isUsableBox(entry.lastCapture) || !isUsableBox(capture) || entry.monitor != window->m_monitor ||
        sampleElapsed > liquid_glass::kStallSeconds) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = canDeform && isUsableBox(capture);
        if (openingEdge && entry.wasEligible)
            beginOpening(entry, window);
        return;
    }
    entry.lastSample = now;

    if (auto *existing = transformerOf(window); existing && materialChanged(*existing)) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = true;
        return;
    }

    if (!openEnabled()) {
        if (auto *existing = transformerOf(window))
            existing->cancelOpening();
    }

    if (openingEdge) {
        beginOpening(entry, window);
        // A cloth opening may warp native popin geometry to its goal. Baseline the box actually displayed.
        baseline(entry, window);
        return;
    }
    auto      *transformer = transformerOf(window);
    const bool clothHandoff = m_config.style == cfg::Style::Cloth && transformer &&
                              (transformer->clothOpening() || transformer->clothMoving());
    const bool liquidBusy = transformer && transformer->liquidStyle() && transformer->transition().active();
    const bool sheetBusy = transformer && transformer->sheetActive() && !clothHandoff;
    if (((animating || wasAnimating) && !clothHandoff) || liquidBusy || sheetBusy) {
        // Native opening and a liquid box-driven transition are not movement impulses. Cloth's opening box
        // is already fixed at its goal, so subsequent geometry changes can take over its displayed sheet.
        entry.lastBox = current;
        entry.lastCapture = capture;
        return;
    }

    const CBox   previousCapture = entry.lastCapture;
    const auto   delta = current.pos() - entry.lastBox.pos();
    const bool   translated = delta.x != 0.0 || delta.y != 0.0;
    const bool   resized = current.size() != entry.lastBox.size();
    const bool   captureChanged = capture.pos() != previousCapture.pos() || capture.size() != previousCapture.size();
    const double largest = std::max({current.w, current.h, entry.lastBox.w, entry.lastBox.h});
    entry.lastBox = current;
    entry.lastCapture = capture;

    if (std::hypot(delta.x, delta.y) > MAX_JUMP_FACTOR * largest) {
        detachWindow(entry, window);
        baseline(entry, window);
        return;
    }

    const bool enabled = movementEnabled();
    const bool cancelledMovement = transformer && transformer->clothMoving() && !enabled;
    if (!translated && !resized && !captureChanged && !cancelledMovement)
        return;
    const bool moving = translated && enabled;
    if (!moving && !transformer)
        return;
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    if (!transformer)
        transformer = acquireTransformer(window);
    const bool wasActive = transformer->active();
    if (cancelledMovement)
        transformer->cancelMovement();
    switch (m_config.style) {
    case cfg::Style::Cloth:
        if (moving && !transformer->clothMoving())
            transformer->beginClothMovement(
                m_nextSeed++, {previousCapture.x, previousCapture.y, previousCapture.w, previousCapture.h},
                movementGrab(window, capture, delta));
        if (enabled && transformer->clothMoving())
            transformer->updateClothMovement({capture.x, capture.y, capture.w, capture.h}, sampleElapsed);
        break;
    case cfg::Style::Liquid:
        if (moving)
            transformer->mesh().recordTranslation({delta.x, delta.y}, {captureSize.x, captureSize.y},
                                                  movementStrength());
        else if (enabled)
            transformer->mesh().advance(0.0, {captureSize.x, captureSize.y});
        else
            transformer->mesh().reset();
        transformer->refreshPose({captureSize.x, captureSize.y});
        break;
    case cfg::Style::Paper:
        break; // paper windows move undeformed
    }
    if (!wasActive) {
        // The first live target spans the observed geometry interval, even though this frame's advance follows the
        // sample immediately. Otherwise its initial velocity would be compressed into almost zero time.
        entry.lastAdvance = m_config.style == cfg::Style::Cloth && moving
                                ? now - std::chrono::duration_cast<clock::duration>(
                                            std::chrono::duration<double>(std::max(0.0, sampleElapsed)))
                                : now;
    }

    if (transformer->active()) {
        entry.footprint = worldFootprint(window);
        entry.hasFootprint = true;
    } else {
        window->effects().transformers()->removeInactive();
    }
    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
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
            baseline(entry, window);
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
    entry.lastAdvance = clock::now();

    if (entry.hasFootprint)
        g_pHyprRenderer->damageBox(entry.footprint);
    entry.footprint = worldFootprint(window);
    entry.hasFootprint = true;

    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

void CPluginRuntime::advanceWindow(STracked &entry, const PHLWINDOW &window,
                                   std::chrono::steady_clock::time_point now) {
    auto        *transformer = transformerOf(window);
    const double elapsed = std::max(0.0, std::chrono::duration<double>(now - entry.lastAdvance).count());
    entry.lastAdvance = now;

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

    if (materialChanged(*transformer)) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = eligible(window);
        return;
    }

    if (elapsed > liquid_glass::kStallSeconds) {
        detachWindow(entry, window);
        baseline(entry, window);
        return;
    }

    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }

    const CBox capture = window->getFullWindowBoundingBox();
    if (!isUsableBox(capture)) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = false;
        return;
    }
    entry.lastCapture = capture;
    const liquid_glass::Vec2 size{capture.w, capture.h};
    const bool               enabled = movementEnabled();
    const bool               moving = enabled && m_config.style == cfg::Style::Liquid;
    if (!enabled && transformer->clothMoving())
        transformer->cancelMovement();
    transformer->advanceSheet(elapsed, size);
    if (transformer->liquidStyle()) {
        if (!moving)
            transformer->mesh().reset();
        advanceLiquidPose(transformer->mesh(), transformer->transition(), elapsed, size, movementStrength(),
                          moving ? m_config.open.liquid.emergeDrag : 0.0);
        transformer->refreshPose(size);
        transformer->driveBox();
    }
    entry.lastBox = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.lastCapture = window->getFullWindowBoundingBox();
    const bool stillActive = transformer->active();
    if (stillActive) {
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
