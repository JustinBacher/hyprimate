#include "PluginRuntime.hpp"

#include "LiquidMesh.hpp"

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

constexpr const char* WOBBLE_MESSAGE   = "[liquid-glass] Disable decoration:wobble:enabled before enabling liquid-glass";
#include "config_constants.inc"
constexpr double      MAX_GAP_SECONDS  = 0.25;
constexpr double      MAX_JUMP_FACTOR  = 1.5;

bool isUsableBox(const CBox& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
}

double clampStrength(double value) {
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, 0.0, MAX_STRENGTH);
}

// Non-finite input stays NaN so material validation rejects it instead of silently clamping it.
double clampUnit(double value) {
    if (!std::isfinite(value))
        return std::numeric_limits<double>::quiet_NaN();
    return std::clamp(value, 0.0, 1.0);
}

// ARGB config value to a float colour; channel bytes are unpacked, not treated as one packed integer.
CHyprColor colorFromArgb(std::uint32_t argb) {
    const auto channel = [argb](int shift) { return static_cast<float>((argb >> shift) & 0xFFU) / 255.F; };
    return CHyprColor(channel(16), channel(8), channel(0), channel(24));
}

} // namespace

CPluginRuntime::CPluginRuntime(HANDLE handle) : m_handle(handle) {
    std::random_device entropy;
    m_nextSeed = (std::uint64_t{entropy()} << 32) | entropy();
#include "config_registration.inc"

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
        if (auto* entry = find(window)) {
            // Staging must precede detachWindow, which marks the transformer detached. A resting window
            // closes from a fresh field; a moving one carries its current displacement and velocity.
            // Close drains rigidly: emerge_drag is an open-only option, so the close drives no travel.
            if (closeEnabled() && eligible(window)) {
                auto* transformer = transformerOf(window);
                if (closePaper()) {
                    // Paper crumples in place from the window's current box; it needs no edge and no strength gate.
                    m_close->stagePaper(window, transformer ? transformer->paper() : liquid_glass::PaperCrumple{},
                                        paperParams(m_closePaperDuration->value(), m_closePaperCrumpliness->value(), m_closePaperShading->value(), m_closePaperVariation->value()),
                                        m_nextSeed++);
                } else {
                    const bool continuing = transformer && transformer->transition().active();
                    const auto closeFrame = continuing ? std::optional<liquid_glass::EdgeFrame>{} : edgeFrameFor(window, false, entry->openEdge);
                    if (transformer && transformer->active())
                        m_close->stage(window, transformer->closePose(), clampStrength(m_closeLiquidStrength->value()), closeFrame, 0.0, m_closeSplash->value(),
                                       transitionParams(m_closeElasticity->value(), m_closeMomentum->value()));
                    else {
                        const liquid_glass::LiquidMesh     mesh{effectiveMode(), effectiveMaterialParams()};
                        const liquid_glass::EdgeTransition transition;
                        m_close->stage(window, SLiquidPose{mesh, transition}, clampStrength(m_closeLiquidStrength->value()), closeFrame, 0.0, m_closeSplash->value(),
                                       transitionParams(m_closeElasticity->value(), m_closeMomentum->value()));
                    }
                }
            }
            detachWindow(*entry, window);
        }
        forget(window);
    });

    m_listenDestroy = Event::bus()->m_events.window.destroy.listen([this](PHLWINDOWREF window) {
        std::erase_if(m_tracked, [&window](const STracked& entry) { return entry.window.expired() || entry.window.get() == window.get(); });
    });

    m_listenTick = Event::bus()->m_events.tick.listen([this]() { onTick(); });

    m_listenRenderPre = Event::bus()->m_events.render.pre.listen([this](PHLMONITOR) { onRenderPre(); });

    m_listenReload = Event::bus()->m_events.config.reloaded.listen([this]() { onReload(); });

    for (const auto& window : Desktop::windowState()->windows())
        track(window);
}

CPluginRuntime::~CPluginRuntime() {
    // Unload order: stop callbacks, unhook the close path, drop its records, then detach every window.
    m_listenOpenLate.reset();
    m_listenClose.reset();
    m_listenDestroy.reset();
    m_listenTick.reset();
    m_listenRenderPre.reset();
    m_listenReload.reset();
    m_close->shutdown();
    detachAll();
    m_closeSolid.reset();
    m_openSolid.reset();
    m_shade.reset();
}

bool CPluginRuntime::pluginEnabled() const {
    return m_enabled->value();
}

double CPluginRuntime::movementStrength() const {
    const auto value = effectiveMode() == liquid_glass::MotionMode::Wiggle ? m_movementWiggleStrength : m_movementLiquidStrength;
    return clampStrength(value->value());
}

double CPluginRuntime::effectiveViscosity() const {
    return clampUnit(m_viscosity->value());
}

liquid_glass::MotionMode CPluginRuntime::effectiveMode() const {
    return m_mode->value() == "wiggle" ? liquid_glass::MotionMode::Wiggle : liquid_glass::MotionMode::Liquid;
}

liquid_glass::MaterialParams CPluginRuntime::effectiveMaterialParams() const {
    return liquid_glass::MaterialParams{
        .viscosity     = effectiveViscosity(),
        .elasticity    = clampUnit(m_elasticity->value()),
        .momentum      = clampUnit(m_momentum->value()),
        .variation     = clampUnit(m_variation->value()),
    };
}

bool CPluginRuntime::movementEnabled() const {
    return pluginEnabled() && m_movementEnabled->value() && materialUsable() && movementStrength() > 0.0;
}

bool CPluginRuntime::openEnabled() const {
    return pluginEnabled() && !m_inert && m_openEnabled->value();
}

bool CPluginRuntime::closeEnabled() const {
    return pluginEnabled() && !m_inert && m_closeEnabled->value();
}

bool CPluginRuntime::openPaper() const {
    return m_openStyle->value() == "paper";
}

bool CPluginRuntime::closePaper() const {
    return m_closeStyle->value() == "paper";
}


liquid_glass::MaterialParams CPluginRuntime::transitionParams(double elasticity, double momentum) const {
    // Edge transitions use only elasticity and momentum; the movement field's viscosity and variation do not apply.
    return liquid_glass::MaterialParams{
        .viscosity     = 0.0,
        .elasticity    = clampUnit(elasticity),
        .momentum      = clampUnit(momentum),
        .variation     = 0.0,
    };
}

liquid_glass::PaperParams CPluginRuntime::paperParams(double duration, double crumpliness, double shading, double variation) const {
    return {
        .duration    = duration,
        .crumpliness = clampUnit(crumpliness),
        .shading     = clampUnit(shading),
        .variation   = clampUnit(variation),
    };
}

double CPluginRuntime::effectiveEmergeDrag() const {
    const double drag = m_emergeDrag->value();
    if (!std::isfinite(drag))
        return 0.0;
    return std::clamp(drag, 0.0, 1.0);
}


liquid_glass::Edge CPluginRuntime::firstWindowEdge() const {
    const auto& side = m_firstWindowSide->value();
    if (side == "top")
        return liquid_glass::Edge::Top;
    if (side == "left")
        return liquid_glass::Edge::Left;
    if (side == "right")
        return liquid_glass::Edge::Right;
    return liquid_glass::Edge::Bottom;
}

bool CPluginRuntime::workspaceHasOtherWindows(const PHLWINDOW& window) const {
    for (const auto& other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() && !other->isHidden())
            return true;
    }
    return false;
}

// Tiled windows on the workspace, as GOAL boxes, for the contact rule. Floating windows are not neighbours.
void CPluginRuntime::gatherNeighbours(const PHLWINDOW& window) const {
    for (const auto& other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() && !other->isHidden() &&
            !other->isFloating()) {
            const CBox b = other->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
            m_neighbours.push_back({b.x, b.y, b.w, b.h});
        }
    }
}

CBox CPluginRuntime::edgeBoundsBox(const PHLMONITOR& monitor, bool opening) const {
    const auto& bounds = opening ? m_edgeBounds->value() : m_closeEdgeBounds->value();
    if (bounds == "work_area")
        return monitor->logicalBoxMinusReserved();
    return monitor->logicalBox();
}

std::optional<liquid_glass::EdgeFrame> CPluginRuntime::edgeFrameFor(const PHLWINDOW& window, bool opening, std::optional<liquid_glass::Edge> origin) const {
    const auto monitor = window->m_monitor.lock();
    if (!monitor)
        return std::nullopt;
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox body    = opening ? window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL) : current;
    const CBox capture = window->getFullWindowBoundingBox();
    // Decoration margins (border, shadow) do not scale with native popin, so they carry over to the rest box.
    const CBox rest{body.x + (capture.x - current.x), body.y + (capture.y - current.y), body.w + (capture.w - current.w),
                    body.h + (capture.h - current.h)};
    const auto box = [](const CBox& b) { return liquid_glass::Box{b.x, b.y, b.w, b.h}; };

    // Fallback edge: a workspace's first window uses first_window_side; close_side=origin prefers the opening edge.
    // Otherwise the contact rule picks the side away from tiled neighbours; close_side=nearest ignores them.
    liquid_glass::Edge fallback       = firstWindowEdge();
    bool               preferFallback = opening && !workspaceHasOtherWindows(window);
    if (!opening && origin && m_closeSide->value() == "origin") {
        fallback       = *origin;
        preferFallback = true;
    }
    m_neighbours.clear();
    const bool useContact = !window->isFloating() && (opening || m_closeSide->value() != "nearest");
    if (useContact)
        gatherNeighbours(window);
    return liquid_glass::resolveEdgeFrame(box(body), box(rest), box(edgeBoundsBox(monitor, opening)), m_neighbours, fallback, preferFallback);
}

bool CPluginRuntime::materialUsable() const {
    if (effectiveMode() == liquid_glass::MotionMode::Wiggle)
        return true;
    const auto params = effectiveMaterialParams();
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) && std::isfinite(params.momentum)
        && std::isfinite(params.variation);
}

bool CPluginRuntime::materialChanged(const liquid_glass::LiquidMesh& mesh) const {
    if (!movementEnabled())
        return false;
    if (mesh.mode() != effectiveMode())
        return true;
    return mesh.mode() == liquid_glass::MotionMode::Liquid && mesh.params() != effectiveMaterialParams();
}

bool CPluginRuntime::wobbleEnabled() const {
    return *m_wobble != 0;
}

void CPluginRuntime::notify(const std::string& text) const {
    HyprlandAPI::addNotification(m_handle, text, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
}

void CPluginRuntime::onReload() {
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

CPluginRuntime::STracked* CPluginRuntime::find(const PHLWINDOW& window) {
    for (auto& entry : m_tracked) {
        if (entry.window.get() == window.get())
            return &entry;
    }
    return nullptr;
}

void CPluginRuntime::track(const PHLWINDOW& window) {
    if (!window)
        return;
    if (find(window))
        return;

    STracked entry;
    entry.window         = PHLWINDOWREF{window};
    // Starts false so a window that is already opening registers the rising edge on its first sample.
    entry.wasAnimatingIn = false;
    baseline(entry, window);
    m_tracked.push_back(entry);
}

void CPluginRuntime::forget(const PHLWINDOW& window) {
    std::erase_if(m_tracked, [&window](const STracked& entry) { return entry.window.get() == window.get(); });
}

void CPluginRuntime::baseline(STracked& entry, const PHLWINDOW& window) const {
    entry.lastBox         = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.lastCaptureSize = window->getFullWindowBoundingBox().size();
    entry.monitor         = window->m_monitor;
    entry.baselined       = true;
    entry.lastAdvance     = std::chrono::steady_clock::now();
    entry.lastSample      = entry.lastAdvance;
}

void CPluginRuntime::baselineAll() {
    for (auto& entry : m_tracked) {
        if (const auto window = entry.window.lock())
            baseline(entry, window);
    }
}

bool CPluginRuntime::eligible(const PHLWINDOW& window) const {
    // Opening animations are deformable; the sampler rebaselines geometry while they run.
    return pluginEnabled() && !m_inert
        && window->mapped() && !Fullscreen::controller()->isFullscreen(window)
        && isUsableBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
}

void CPluginRuntime::onRenderPre() {
    // Colour is read live: runtime config edits (hl.config, hyprctl keyword) need not fire a reload. The texture is
    // rebuilt only when the value actually changes.
    m_openSolid->setColor(colorFromArgb(static_cast<std::uint32_t>(m_openLiquidColor->value())));
    m_closeSolid->setColor(colorFromArgb(static_cast<std::uint32_t>(m_closeLiquidColor->value())));
    // Disabled/nonfinite strength must clear an existing field, not skip cleanup.
    for (auto& entry : m_tracked) {
        if (const auto window = entry.window.lock())
            sampleWindow(entry, window);
    }
}

void CPluginRuntime::onTick() {
    // Sampling also handles disable/fullscreen/config eligibility transitions.
    const auto now = clock::now();
    for (auto& entry : m_tracked) {
        const auto window = entry.window.lock();
        if (!window)
            continue;

        sampleWindow(entry, window);
        advanceWindow(entry, window, now);
    }

    std::erase_if(m_tracked, [](const STracked& entry) { return entry.window.expired(); });

    // A live disable also drops retained closes, restoring native fades where still available.
    if (!closeEnabled() || (!closePaper() && clampStrength(m_closeLiquidStrength->value()) <= 0.0)) {
        m_close->clear();
    } else if (m_close->tick(now, m_closeMonitors)) {
        Animation::mgr()->requestTick();
        for (const auto& monitorRef : m_closeMonitors) {
            if (const auto monitor = monitorRef.lock()) {
                g_pHyprRenderer->damageMonitor(monitor);
                monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            }
        }
    }
}

void CPluginRuntime::sampleWindow(STracked& entry, const PHLWINDOW& window) {
    const auto now = clock::now();
    const bool animating = window->presentation().animatingIn();
    const bool wasAnimating = entry.wasAnimatingIn;
    const bool openingEdge = animating && !wasAnimating;
    entry.wasAnimatingIn = animating;

    const bool canDeform = eligible(window);
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox capture = window->getFullWindowBoundingBox();
    const Vector2D captureSize = capture.size();

    if (!canDeform || !entry.wasEligible || !entry.baselined || !isUsableBox(entry.lastBox)
        || !isUsableBox(current) || !isUsableBox(capture) || entry.monitor != window->m_monitor
        || std::chrono::duration<double>(now - entry.lastSample).count() > MAX_GAP_SECONDS) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = canDeform && isUsableBox(capture);
        if (openingEdge && entry.wasEligible)
            beginOpening(entry, window);
        return;
    }
    entry.lastSample = now;

    if (auto* existing = transformerOf(window); existing && materialChanged(existing->mesh())) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = true;
        return;
    }

    if (!openEnabled()) {
        if (auto* existing = transformerOf(window))
            existing->cancelOpening();
    }

    if (openingEdge) {
        beginOpening(entry, window);
        entry.lastBox         = current;
        entry.lastCaptureSize = captureSize;
        return;
    }
    const auto* existing = transformerOf(window);
    if (animating || wasAnimating || (existing && (existing->transition().active() || existing->paper().active()))) {
        // Native open/close motion and the driven open box move the window; neither is a drag impulse.
        entry.lastBox         = current;
        entry.lastCaptureSize = captureSize;
        return;
    }

    const auto delta = current.pos() - entry.lastBox.pos();
    const bool translated = delta.x != 0.0 || delta.y != 0.0;
    const bool resized = current.size() != entry.lastBox.size();
    const bool captureResized = captureSize != entry.lastCaptureSize;
    const double largest = std::max({current.w, current.h, entry.lastBox.w, entry.lastBox.h});
    entry.lastBox = current;
    entry.lastCaptureSize = captureSize;
    if (!translated && !resized && !captureResized)
        return;

    if (std::hypot(delta.x, delta.y) > MAX_JUMP_FACTOR * largest) {
        detachWindow(entry, window);
        baseline(entry, window);
        return;
    }

    auto* transformer = transformerOf(window);
    const bool moving = translated && movementEnabled();
    if (!moving && !transformer)
        return;
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    if (!transformer)
        transformer = acquireTransformer(window);
    const bool wasActive = transformer->active();
    if (moving)
        transformer->mesh().recordTranslation({delta.x, delta.y}, {captureSize.x, captureSize.y}, movementStrength());
    else if (movementEnabled())
        transformer->mesh().advance(0.0, {captureSize.x, captureSize.y});
    else
        transformer->mesh().reset();
    transformer->refreshPose({captureSize.x, captureSize.y});
    if (!wasActive)
        entry.lastAdvance = now;

    if (transformer->active()) {
        entry.footprint = worldFootprint(window);
        entry.hasFootprint = true;
        Animation::mgr()->requestTick();
    } else {
        window->effects().transformers()->removeInactive();
    }
    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

void CPluginRuntime::beginOpening(STracked& entry, const PHLWINDOW& window) {
    // Every open attempt starts from no edge, so an early return never leaves a stale origin behind.
    entry.openEdge.reset();
    if (!openEnabled())
        return;

    if (openPaper()) {
        // Paper needs no edge and never drives the box: it starts as a ball at the window's own box.
        const CBox capture = window->getFullWindowBoundingBox();
        acquireTransformer(window)->beginPaperOpen(paperParams(m_openPaperDuration->value(), m_openPaperCrumpliness->value(), m_openPaperShading->value(), m_openPaperVariation->value()), m_nextSeed++, {capture.w, capture.h});
    } else {
        const auto frame = edgeFrameFor(window, true);
        if (!frame)
            return; // no usable edge: the native opening runs, movement deformation unaffected
        entry.openEdge = frame->edge;
        if (clampStrength(m_openLiquidStrength->value()) <= 0.0)
            return;
        auto* transformer = acquireTransformer(window);
        const CBox capture = window->getFullWindowBoundingBox();
        transformer->beginOpen(*frame, transitionParams(m_openElasticity->value(), m_openMomentum->value()), clampStrength(m_openLiquidStrength->value()), {capture.w, capture.h});
    }
    entry.lastAdvance = clock::now();

    if (entry.hasFootprint)
        g_pHyprRenderer->damageBox(entry.footprint);
    entry.footprint    = worldFootprint(window);
    entry.hasFootprint = true;

    Animation::mgr()->requestTick();
    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

void CPluginRuntime::advanceWindow(STracked& entry, const PHLWINDOW& window, std::chrono::steady_clock::time_point now) {
    auto* transformer = transformerOf(window);
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

    if (materialChanged(transformer->mesh())) {
        detachWindow(entry, window);
        baseline(entry, window);
        entry.wasEligible = eligible(window);
        return;
    }

    if (elapsed > MAX_GAP_SECONDS) {
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
    entry.lastCaptureSize = capture.size();
    const liquid_glass::Vec2 size{capture.w, capture.h};
    const bool moving = movementEnabled();
    if (!moving)
        transformer->mesh().reset();
    advanceLiquidPose(transformer->mesh(), transformer->transition(), elapsed, size, movementStrength(), moving ? effectiveEmergeDrag() : 0.0);
    transformer->advancePaper(elapsed, size);
    transformer->refreshPose(size);
    transformer->driveBox();
    entry.lastBox         = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    entry.lastCaptureSize = window->getFullWindowBoundingBox().size();
    const bool stillActive = transformer->active();
    if (stillActive) {
        entry.footprint    = worldFootprint(window);
        entry.hasFootprint = true;
        Animation::mgr()->requestTick();
    } else {
        // Settled fields have already restored the undeformed mesh; pruning drops the transformer.
        window->effects().transformers()->removeInactive();
    }

    g_pHyprRenderer->damageWindow(window);
    scheduleAnimation(window);
}

CLiquidGlassTransformer* CPluginRuntime::transformerOf(const PHLWINDOW& window) const {
    return window->effects().transformers()->get<CLiquidGlassTransformer>();
}

CLiquidGlassTransformer* CPluginRuntime::acquireTransformer(const PHLWINDOW& window) {
    const auto& list = window->effects().transformers();
    if (auto* existing = list->get<CLiquidGlassTransformer>())
        return existing;
    // Each window draws from its own stream, so windows moved together (scrolling, workspace slides) still differ.
    return list->emplace<CLiquidGlassTransformer>(window->m_self, effectiveMode(), effectiveMaterialParams(), m_nextSeed++, m_openSolid, m_shade);
}

CBox CPluginRuntime::worldFootprint(const PHLWINDOW& window) const {
    CBox box = window->getFullWindowBoundingBox();
    const auto workspace = window->m_workspace;
    if (workspace && workspace->m_renderOffset->isBeingAnimated() && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED))
        box.translate(workspace->m_renderOffset->value());
    box.translate(window->presentation().floatingOffset());
    return window->effects().transformBoxForDamage(box);
}

void CPluginRuntime::scheduleAnimation(const PHLWINDOW& window) const {
    if (const auto monitor = window->m_monitor.lock())
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}

void CPluginRuntime::detachWindow(STracked& entry, const PHLWINDOW& window) {
    const bool hadFootprint = entry.hasFootprint;
    if (hadFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    auto* transformer = transformerOf(window);
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
    for (auto& entry : m_tracked) {
        if (const auto window = entry.window.lock())
            detachWindow(entry, window);
    }
}
