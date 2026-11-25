#include <algorithm>
#include <utility>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/UnifiedWorkspaceSwipeGesture.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

#include "AnimationTransformer.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimationPass.hpp"
#include "gl/NativeResource.hpp"
#include "window/WindowAnimations.hpp"
#include "window/bubble/BubbleWallpaper.hpp"
#include "workspace/WorkspaceCapturePolicy.hpp"
#include "HyprWindowPortGeometry.hpp"

using namespace hyprimate::animation;
using namespace hyprimate::window_port_detail;
namespace {
    hyprimate::Box box_of(const CBox &box) {
        return {.x = box.x, .y = box.y, .w = box.w, .h = box.h};
    }
}  // namespace

void CHyprWindowPort::bind(HyprWindowAnimations &module) {
    m_module = &module;
}

void CHyprWindowPort::update_workspace_switch(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    const bool GESTURE = g_pUnifiedWorkspaceSwipe && g_pUnifiedWorkspaceSwipe->isGestureInProgress();
    bool active = false;
    for (const auto &workspace : State::Workspace::state()->workspaces()) {
        if (workspace->m_monitor.lock() != monitor) {
            continue;
        }
        const auto &offset = workspace->m_renderOffset;
        const auto &alpha = workspace->m_alpha;
        const bool PARTICIPATING = GESTURE && (workspace->visible() || workspace->m_forceRendering);
        const bool DISPLACED = PARTICIPATING &&
            ((offset && offset->value() != Vector2D{}) || (alpha && alpha->value() != 1.F));
        if (hyprimate::workspace::native_workspace_switch(offset && offset->isBeingAnimated(),
                alpha && alpha->isBeingAnimated(), GESTURE, PARTICIPATING, DISPLACED)) {
            active = true;
            break;
        }
    }
    auto &states = m_canvas_resources.workspace_switches;
    for (auto &state : states) {
        if (state.monitor.lock() == monitor) {
            state.active = active;
            return;
        }
    }
    states.push_back({.monitor = monitor, .active = active});
}

void CHyprWindowPort::forget_workspace_switch(const PHLMONITOR &monitor) {
    std::erase_if(m_canvas_resources.workspace_switches,
        [&](const auto &state) { return state.monitor.expired() || state.monitor.lock() == monitor; });
}

std::optional<Observation> CHyprWindowPort::observe(const WindowRef &ref) const {
    // Hyprutils weak locking retains the control block; the analyzer cannot model its counters.
    const auto WINDOW = ref.lock();  // NOLINT(clang-analyzer-cplusplus.NewDelete)
    if (!WINDOW) {
        return std::nullopt;
    }
    const auto MONITOR = WINDOW->m_monitor.lock();
    if (!MONITOR) {
        return std::nullopt;
    }
    const auto WORKSPACE = WINDOW->m_workspace;
    const auto PRESENTATION = Render::resolveWindowSnapshotPresentation(
        WINDOW->presentation().renderPresentation(Render::eSceneMode::MONITOR));
    const auto &drag = g_layoutManager->dragController();
    const auto TARGET = drag->target();
    Observation result;
    result.window = identity(ref);
    result.monitor = monitor_identity(MONITOR);
    result.workspace = workspace_identity(WORKSPACE);
    result.body = box_of(WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
    result.capture = box_of(WINDOW->getFullWindowBoundingBox());
    result.goal_body = box_of(WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL));
    result.screen = box_of(MONITOR->logicalBox());
    result.work_area = box_of(MONITOR->logicalBoxMinusReserved());
    result.workspace_offset = vec_of(PRESENTATION.workspaceOffset);
    result.floating_offset = vec_of(PRESENTATION.floatingOffset);
    result.scale = MONITOR->m_scale;
    result.mapped = WINDOW->mapped();
    result.fullscreen = Fullscreen::controller()->isFullscreen(WINDOW);
    result.hidden = WINDOW->isHidden();
    result.floating = WINDOW->isFloating();
    result.animating_in = WINDOW->presentation().animatingIn();
    result.layout_animated = WINDOW->positionAnimation()->isBeingAnimated() ||
                             WINDOW->sizeAnimation()->isBeingAnimated();
    if (TARGET && TARGET->window() == WINDOW) {
        const auto mode = drag->mode();
        result.held = mode == MBIND_MOVE;
        result.resize_held = mode == MBIND_RESIZE || mode == MBIND_RESIZE_FORCE_RATIO ||
                             mode == MBIND_RESIZE_BLOCK_RATIO;
    }
    result.workspace_visible = !WORKSPACE || WORKSPACE->visible();
    result.workspace_moving = PRESENTATION.workspaceOffsetAnimating;
    result.pinned = static_cast<bool>(WINDOW->m_state & Desktop::View::WINDOW_STATE_PINNED);
    result.no_screen_share = WINDOW->m_ruleApplicator->noScreenShare().valueOrDefault();
    return result;
}
bool CHyprWindowPort::mapped_blur_required(const WindowRef &ref) {
    const auto WINDOW = ref.lock();
    return !WINDOW || !WINDOW->mapped() || !WINDOW->wlSurface() ||
           WINDOW->shouldBlur(WINDOW->presentation().renderPresentation(Render::eSceneMode::MONITOR));
}
bool CHyprWindowPort::session_unlocked() {
    return g_pSessionLockManager && !g_pSessionLockManager->isSessionLocked();
}
bool CHyprWindowPort::tail_source_allowed(
    const WindowRef &ref, WindowId expected, bool capture) const {
    const auto WINDOW = ref.lock();
    if (!session_unlocked() || !WINDOW || !WINDOW->mapped() ||
        (capture && WINDOW->m_ruleApplicator->noScreenShare().valueOrDefault())) {
        return false;
    }
    return std::any_of(m_windows.begin(), m_windows.end(), [&](const auto &item) {
        return item.id == expected && item.object.lock() == WINDOW;
    });
}
Neighbourhood CHyprWindowPort::neighbourhood(const WindowRef &ref) {
    m_neighbours.clear();
    m_obstacles.clear();
    const auto WINDOW = ref.lock();
    if (!WINDOW) {
        return {};
    }
    bool alone = true;
    for (const auto &other : Desktop::windowState()->windows()) {
        if (!other || other == WINDOW || other->m_workspace != WINDOW->m_workspace ||
            !other->mapped() || other->isHidden()) {
            continue;
        }
        alone = false;
        m_obstacles.push_back(box_of(other->getFullWindowBoundingBox()));
        if (!WINDOW->isFloating() && !other->isFloating()) {
            m_neighbours.push_back(
                box_of(other->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL)));
        }
    }
    return {.tiled_goals = m_neighbours,
        .obstacles = m_obstacles,
        .alone = alone,
        .cursor = vec_of(g_pInputManager->getMouseCoordsInternal())};
}
void CHyprWindowPort::warp_body(const WindowRef &ref, const hyprimate::Box &box) {
    if (const auto WINDOW = ref.lock()) {
        WINDOW->positionAnimation()->setValueAndWarp(Vector2D{box.x, box.y});
        WINDOW->sizeAnimation()->setValueAndWarp(Vector2D{box.w, box.h});
    }
}
void CHyprWindowPort::settle_open_alpha(const WindowRef &ref) {
    if (const auto WINDOW = ref.lock()) {
        auto &alpha = WINDOW->presentation().alpha(Desktop::View::WINDOW_ALPHA_FADE);
        alpha->setValueAndWarp(alpha->goal());
    }
}
void CHyprWindowPort::ensure_transformer(const WindowRef &ref, Handle handle) {
    const auto WINDOW = ref.lock();
    if (!WINDOW || m_module == nullptr) {
        return;
    }
    const auto &list = WINDOW->effects().transformers();
    if (auto *existing = list->get<CAnimationTransformer>()) {
        if (existing->handle() == handle && existing->active()) {
            return;
        }
        existing->mark_detached();
        list->removeInactive();
    }
    list->emplace<CAnimationTransformer>(ref, *m_module, *this, handle);
}
void CHyprWindowPort::detach_transformer(const WindowRef &ref) {
    if (const auto WINDOW = ref.lock()) {
        const auto &list = WINDOW->effects().transformers();
        if (auto *transformer = list->get<CAnimationTransformer>()) {
            transformer->mark_detached();
            list->removeInactive();
        }
    }
}
void CHyprWindowPort::damage(const hyprimate::Box &box) {
    if (g_pHyprRenderer) {
        g_pHyprRenderer->damageBox(host_box(box));
    }
}
void CHyprWindowPort::damage_window(const WindowRef &ref) {
    if (const auto WINDOW = ref.lock(); WINDOW && g_pHyprRenderer) {
        g_pHyprRenderer->damageWindow(WINDOW);
    }
}
void CHyprWindowPort::schedule(MonitorId monitor_id) {
    if (const auto MONITOR = monitor_for(monitor_id)) {
        MONITOR->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }
}
void CHyprWindowPort::recheck_scanout(MonitorId monitor_id) {
    if (const auto MONITOR = monitor_for(monitor_id)) {
        MONITOR->recheckSolitary();
    }
}
void CHyprWindowPort::capture_backdrop(Handle handle, const WindowRef &window, Canvas &canvas) {
    if (canvas.pass() == PassKind::Inert) {
        return;
    }
    if (m_module == nullptr) {
        return;
    }
    const auto OBSERVATION = observe(window);
    if (!OBSERVATION) {
        return;
    }
    auto &cache = visual_cache(handle);
    auto &slot =
        canvas.pass() == PassKind::WorkspaceSource ? cache.scratch_backdrop : cache.backdrop;
    if (!slot) {
        slot = makeShared<SBubbleWallpaper>();
    }
    auto source = host_box(OBSERVATION->capture);
    source
        .translate(Vector2D{OBSERVATION->workspace_offset.x + OBSERVATION->floating_offset.x -
                                OBSERVATION->screen.x,
            OBSERVATION->workspace_offset.y + OBSERVATION->floating_offset.y -
                OBSERVATION->screen.y})
        .scale(OBSERVATION->scale);
    auto bounds = host_box(m_module->extents(handle, OBSERVATION->capture));
    bounds.x -= 8.0;
    bounds.y -= 8.0;
    bounds.w += 16.0;
    bounds.h += 16.0;
    bounds
        .translate(Vector2D{OBSERVATION->workspace_offset.x + OBSERVATION->floating_offset.x -
                                OBSERVATION->screen.x,
            OBSERVATION->workspace_offset.y + OBSERVATION->floating_offset.y -
                OBSERVATION->screen.y})
        .scale(OBSERVATION->scale);
    queue_bubble_wallpaper(canvas.context(), slot, source, true, bounds, canvas.pass());
}
bool CHyprWindowPort::bind_fadeout(
    Handle handle, std::uint64_t fadeout, RetainedImage image) {
    auto &cache = visual_cache(handle);
    // Fadeout creation can finish outside a render frame. Its framebuffer is a
    // Hyprland snapshot, so verify that compositor context before retaining it.
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !image || !image->getTexture() || !cache.fadeout_owner.ensure_current()) {
        return false;
    }
    if (handle.style == SShaderModule::KIND) {
        const auto found = std::ranges::find_if(m_fadeouts, [fadeout](const auto &item) {
            return item.id == fadeout;
        });
        const auto native = found == m_fadeouts.end() ? nullptr : found->object.lock();
        if (!session_unlocked() || !native || native->done() || native->source().noScreenShare ||
            native->framebuffer(Render::eSceneMode::MONITOR) != image) {
            return false;
        }
    }
    hyprimate::gl::release_native(cache.fadeout_image, cache.fadeout_owner);
    cache.fadeout_image = std::move(image);
    return true;
}
std::optional<FadeoutFacts> CHyprWindowPort::fadeout_facts(std::uint64_t fadeout_id) const {
    for (const auto &item : m_fadeouts) {
        if (item.id != fadeout_id) {
            continue;
        }
        const auto FADEOUT = item.object.lock();
        if (!FADEOUT) {
            return std::nullopt;
        }
        const auto MONITOR = FADEOUT->monitor().lock();
        const auto IMAGE = FADEOUT->framebuffer(Render::eSceneMode::MONITOR);
        const auto TEXTURE = IMAGE ? IMAGE->getTexture() : nullptr;
        // Lock owns a strong reference before the temporary weak reference is destroyed.
        const auto WORKSPACE =
            FADEOUT->workspace().lock();  // NOLINT(clang-analyzer-cplusplus.NewDelete)
        FadeoutFacts facts;
        facts.now = Clock::now();
        facts.fadeout = fadeout_id;
        facts.monitor = monitor_identity(MONITOR);
        facts.workspace = workspace_identity(WORKSPACE);
        facts.workspace_bound = static_cast<bool>(WORKSPACE);
        facts.native_box = box_of(FADEOUT->renderBox());
        if (TEXTURE) {
            facts.image_size = vec_of(TEXTURE->m_size);
        }
        facts.scale = MONITOR ? MONITOR->m_scale : 1;
        facts.alpha = FADEOUT->alpha();
        facts.alive = !FADEOUT->done();
        facts.blurred = FADEOUT->effects().textureBlur.enabled;
        facts.image_available = static_cast<bool>(TEXTURE);
        facts.no_screen_share = FADEOUT->source().noScreenShare;
        facts.pre_blur = FADEOUT->effects().preBlur.has_value();
        return facts;
    }
    return std::nullopt;
}
void CHyprWindowPort::suppress_native(std::uint64_t fadeout_id, bool suppress) {
    if (suppress) {
        if (std::ranges::find(m_suppressed, fadeout_id) == m_suppressed.end()) {
            m_suppressed.push_back(fadeout_id);
        }
    } else {
        std::erase(m_suppressed, fadeout_id);
    }
}
std::optional<ScreenFacts> CHyprWindowPort::monitor_facts(MonitorId monitor_id) const {
    const auto MONITOR = monitor_for(monitor_id);
    if (!MONITOR) {
        return std::nullopt;
    }
    ScreenFacts facts{};
    facts.monitor = monitor_id;
    facts.now = Clock::now();
    facts.screen = box_of(MONITOR->logicalBox());
    facts.work_area = box_of(MONITOR->logicalBoxMinusReserved());
    facts.scale = MONITOR->m_scale;
    facts.workspace = workspace_identity(MONITOR->m_activeWorkspace);
    facts.special_workspace = workspace_identity(MONITOR->m_activeSpecialWorkspace);
    return facts;
}
std::span<const MonitorId> CHyprWindowPort::monitors_overlapping(const hyprimate::Box &box) {
    m_overlapping.clear();
    const auto HOST = host_box(box);
    for (const auto &monitor : State::monitorState()->monitors()) {
        if (monitor && monitor->logicalBox().overlaps(HOST)) {
            m_overlapping.push_back(monitor_identity(monitor));
        }
    }
    return m_overlapping;
}

std::span<const hyprimate::Box> CHyprWindowPort::visible_outputs(hyprimate::Vec2 presentation_offset) {
    m_visible_outputs.clear();
    for (const auto &monitor : State::monitorState()->monitors()) {
        if (!monitor || !monitor->m_dpmsStatus) { continue; }
        auto box = box_of(monitor->logicalBox());
        box.x -= presentation_offset.x;
        box.y -= presentation_offset.y;
        if (hyprimate::is_usable(box)) { m_visible_outputs.push_back(box); }
    }
    return m_visible_outputs;
}
