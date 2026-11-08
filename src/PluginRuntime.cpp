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
#include <stdexcept>

namespace {

constexpr const char* WOBBLE_MESSAGE   = "[liquid-glass] Disable decoration:wobble:enabled before enabling liquid-glass";
#include "config_constants.inc"
constexpr double      MAX_GAP_SECONDS  = 0.25;
constexpr double      MAX_JUMP_FACTOR  = 1.5;

bool isUsableBox(const CBox& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
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
#include "config_registration.inc"

    if (pluginEnabled() && wobbleEnabled()) {
        notify(WOBBLE_MESSAGE);
        throw std::runtime_error(WOBBLE_MESSAGE);
    }

    // Close transitions are optional: if the compositor symbols are not found, movement deformation still runs.
    m_close = std::make_unique<CLiquidCloseRenderer>(m_handle, m_solid);
    std::string closeError;
    if (!m_close->install(closeError))
        notify("[liquid-glass] close transitions unavailable: " + closeError);

    // Listeners are registered last so a failed constructor never leaves callbacks pointing at a half-built runtime.
    m_listenOpenLate = Event::bus()->m_events.window.openLate.listen([this](PHLWINDOW window) { track(window); });

    m_listenClose = Event::bus()->m_events.window.close.listen([this](PHLWINDOW window) {
        if (auto* entry = find(window)) {
            // Staging must precede detachWindow, which marks the transformer detached. A resting window
            // closes from a fresh field; a moving one carries its current displacement and velocity.
            if (transitionsEnabled() && eligible(window)) {
                auto* transformer     = transformerOf(window);
                const bool continuing = transformer && transformer->transition().active();
                const auto closeFrame = continuing ? std::optional<liquid_glass::EdgeFrame>{} : edgeFrameFor(window, false, entry->openEdge);
                if (transformer && transformer->active())
                    m_close->stage(window, transformer->renderPose(), effectiveStrength(), closeFrame, effectiveEmergeDrag(), m_closeSplash->value());
                else {
                    const liquid_glass::LiquidMesh     mesh{effectiveMode(), effectiveMaterialParams()};
                    const liquid_glass::EdgeTransition transition;
                    m_close->stage(window, SLiquidPose{mesh, transition}, effectiveStrength(), closeFrame, effectiveEmergeDrag(), m_closeSplash->value());
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
    m_solid.reset();
}

bool CPluginRuntime::pluginEnabled() const {
    return m_enabled->value();
}

double CPluginRuntime::effectiveStrength() const {
    const double strength = m_strength->value();
    if (!std::isfinite(strength))
        return 0.0;
    return std::clamp(strength, 0.0, MAX_STRENGTH);
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
    };
}

bool CPluginRuntime::transitionsEnabled() const {
    return pluginEnabled() && !m_inert && m_transitions->value();
}

double CPluginRuntime::effectiveEmergeDrag() const {
    const double drag = m_emergeDrag->value();
    if (!std::isfinite(drag))
        return 0.0;
    return std::clamp(drag, 0.0, 1.0);
}

CHyprColor CPluginRuntime::effectiveLiquidColor() const {
    return colorFromArgb(static_cast<std::uint32_t>(m_liquidColor->value()));
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

CBox CPluginRuntime::edgeBoundsBox(const PHLMONITOR& monitor) const {
    if (m_edgeBounds->value() == "work_area")
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
    return liquid_glass::resolveEdgeFrame(box(body), box(rest), box(edgeBoundsBox(monitor)), m_neighbours, fallback, preferFallback);
}

bool CPluginRuntime::materialUsable() const {
    if (effectiveMode() == liquid_glass::MotionMode::Wiggle)
        return true;
    const auto params = effectiveMaterialParams();
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) && std::isfinite(params.momentum);
}

bool CPluginRuntime::materialChanged(const liquid_glass::LiquidMesh& mesh) const {
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
    return pluginEnabled() && !m_inert && effectiveStrength() > 0.0 && materialUsable()
        && window->mapped() && !Fullscreen::controller()->isFullscreen(window)
        && isUsableBox(window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
}

void CPluginRuntime::onRenderPre() {
    // Colour is read live: runtime config edits (hl.config, hyprctl keyword) need not fire a reload. The texture is
    // rebuilt only when the value actually changes.
    m_solid->setColor(effectiveLiquidColor());
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

    // Closing fadeouts are driven here, outside render, so their records can be pruned safely.
    if (m_close->tick(now, m_closeMonitors)) {
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

    if (!transitionsEnabled()) {
        if (auto* existing = transformerOf(window))
            existing->cancelOpening();
    }

    if (openingEdge) {
        beginOpening(entry, window);
        entry.lastBox         = current;
        entry.lastCaptureSize = captureSize;
        return;
    }
    if (animating || wasAnimating) {
        // Native open/close motion drives geometry; it must not be recorded as a drag impulse.
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
    if (!translated && !transformer)
        return;
    if (entry.hasFootprint) {
        g_pHyprRenderer->damageBox(entry.footprint);
        entry.hasFootprint = false;
    }
    if (!transformer)
        transformer = acquireTransformer(window);
    const bool wasActive = transformer->active();
    if (translated)
        transformer->mesh().recordTranslation({delta.x, delta.y}, {captureSize.x, captureSize.y}, effectiveStrength());
    else
        transformer->mesh().advance(0.0, {captureSize.x, captureSize.y});
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
    if (!transitionsEnabled() || effectiveMode() != liquid_glass::MotionMode::Liquid)
        return;

    const auto frame = edgeFrameFor(window, true);
    if (!frame)
        return; // no usable edge: the native opening runs, movement deformation unaffected
    entry.openEdge = frame->edge;
    auto* transformer = acquireTransformer(window);
    const CBox capture = window->getFullWindowBoundingBox();
    transformer->beginOpen(*frame, effectiveMaterialParams(), effectiveStrength(), {capture.w, capture.h});
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
    advanceLiquidPose(transformer->mesh(), transformer->transition(), elapsed, size, effectiveStrength(), effectiveEmergeDrag());
    transformer->refreshPose(size);
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
    return list->emplace<CLiquidGlassTransformer>(window->m_self, effectiveMode(), effectiveMaterialParams(), m_solid);
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
