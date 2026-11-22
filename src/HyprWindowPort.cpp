#include <algorithm>
#include <stdexcept>
#include <utility>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "AnimationTransformer.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimationPass.hpp"
#include "WindowAnimations.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "gl/NativeResource.hpp"

using namespace hyprimate::animation;
namespace {
    hyprimate::Box box_of(const CBox &box) {
        return {.x = box.x, .y = box.y, .w = box.w, .h = box.h};
    }
    CBox host_box(const hyprimate::Box &box) {
        return {box.x, box.y, box.w, box.h};
    }
    hyprimate::Vec2 vec_of(const Vector2D &value) {
        return {.x = value.x, .y = value.y};
    }
}  // namespace

WindowId CHyprWindowPort::identity(const WindowRef &ref) const {
    const auto WINDOW = ref.lock();
    if (!WINDOW) {
        return 0;
    }
    for (const auto &item : m_windows) {
        if (item.object.lock() == WINDOW) {
            return item.id;
        }
    }
    std::erase_if(m_windows, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_window++;
    m_windows.push_back({.id = IDENTITY, .object = ref});
    return IDENTITY;
}
MonitorId CHyprWindowPort::monitor_identity(const PHLMONITOR &monitor) const {
    if (!monitor) {
        return 0;
    }
    for (const auto &item : m_monitors) {
        if (item.object.lock() == monitor) {
            return item.id;
        }
    }
    std::erase_if(m_monitors, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_monitor++;
    m_monitors.push_back({.id = IDENTITY, .object = PHLMONITORREF{monitor}});
    return IDENTITY;
}
WorkspaceId CHyprWindowPort::workspace_identity(const PHLWORKSPACE &workspace) const {
    if (!workspace) {
        return 0;
    }
    for (const auto &item : m_workspaces) {
        if (item.object.lock() == workspace) {
            return item.id;
        }
    }
    std::erase_if(m_workspaces, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_workspace++;
    m_workspaces.push_back({.id = IDENTITY, .object = PHLWORKSPACEREF{workspace}});
    return IDENTITY;
}
std::uint64_t CHyprWindowPort::register_fadeout(const SP<Desktop::CWindowFadeout> &fadeout) {
    if (!fadeout) {
        return 0;
    }
    const SP<Desktop::IFadeout> BASE = fadeout;
    if (const auto IDENTITY = fadeout_identity(BASE)) {
        return IDENTITY;
    }
    std::erase_if(m_fadeouts, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_fadeout++;
    m_fadeouts.push_back({.id = IDENTITY, .object = WP<Desktop::IFadeout>{BASE}});
    return IDENTITY;
}
std::uint64_t CHyprWindowPort::fadeout_identity(const SP<Desktop::IFadeout> &fadeout) const {
    if (!fadeout) {
        return 0;
    }
    for (const auto &item : m_fadeouts) {
        if (item.object.lock() == fadeout) {
            return item.id;
        }
    }
    return 0;
}
PHLMONITOR CHyprWindowPort::monitor_for(MonitorId monitor_id) const {
    for (const auto &item : m_monitors) {
        if (item.id == monitor_id) {
            return item.object.lock();
        }
    }
    return {};
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
bool CHyprWindowPort::session_unlocked() {
    return g_pSessionLockManager && !g_pSessionLockManager->isSessionLocked();
}
bool CHyprWindowPort::tail_source_allowed(const WindowRef &ref, WindowId expected, bool capture) const {
    const auto WINDOW = ref.lock();
    if (!session_unlocked() || !WINDOW || !WINDOW->mapped() ||
        (capture && WINDOW->m_ruleApplicator->noScreenShare().valueOrDefault())) { return false; }
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
    return {.tiled_goals = m_neighbours, .obstacles = m_obstacles, .alone = alone,
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
CHyprWindowPort::VisualCache &CHyprWindowPort::acquire_visuals(Handle handle) {
    m_visuals.push_back({.handle = handle, .cache = std::make_unique<VisualCache>()});
    return *m_visuals.back().cache;
}
CHyprWindowPort::VisualCache &CHyprWindowPort::visual_cache(Handle handle) {
    for (auto &item : m_visuals) {
        if (item.handle == handle) {
            return *item.cache;
        }
    }
    throw std::logic_error("missing window animation visuals");
}
const CHyprWindowPort::VisualCache *CHyprWindowPort::find_visuals(Handle handle) const {
    for (const auto &item : m_visuals) {
        if (item.handle == handle) {
            return item.cache.get();
        }
    }
    return nullptr;
}
void CHyprWindowPort::release_visuals(Handle handle) {
    std::erase_if(m_visuals, [handle](const auto &item) { return item.handle == handle; });
}
const CHyprWindowPort::Backdrop *CHyprWindowPort::backdrop(Handle handle, PassKind pass) const {
    const auto *cache = find_visuals(handle);
    if (cache == nullptr || pass == PassKind::Inert) {
        return nullptr;
    }
    const auto &slot =
        pass == PassKind::WorkspaceSource ? cache->scratch_backdrop : cache->backdrop;
    return slot && slot->owner.ensure_current() ? &slot : nullptr;
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
void CHyprWindowPort::retain_mapped_image(Handle handle,
    const Canvas::Image &image,
    hyprimate::Vec2 origin,
    hyprimate::Box capture,
    FrameId frame) {
    auto &cache = visual_cache(handle);
    if (!cache.image && image && cache.image_owner.ensure_current()) {
        cache.image = image;
        cache.image_origin = origin;
        cache.capture = capture;
        cache.frame = frame;
    }
}
void CHyprWindowPort::release_mapped_image(Handle handle) {
    auto &cache = visual_cache(handle);
    hyprimate::gl::release_native(cache.image, cache.image_owner);
    cache.image_owner.reset();
    cache.image_origin = {};
    cache.capture = {};
    cache.frame = 0;
}
void CHyprWindowPort::queue_tail(
    Handle handle, Canvas &canvas, const Canvas::Image &image, const TailDraw &draw) {
    if (m_module == nullptr || !image) {
        return;
    }
    CWindowAnimationPass::SData data;
    data.animations = m_module;
    data.resources = &m_canvas_resources;
    data.handle = handle;
    data.image = image;
    data.image_owner = visual_cache(handle).image_owner;
    data.retained_tail = true;
    data.no_screen_share = draw.no_screen_share;
    data.source = draw.source;
    data.output = draw.output;
    data.canvas_box = draw.canvas_box;
    data.image_origin = draw.image_origin;
    data.image_size = draw.image_size;
    data.scale = draw.scale;
    data.alpha = draw.alpha;
    data.monitor = draw.monitor;
    data.frame = draw.frame;
    data.damage = CRegion{host_box(draw.canvas_box)};
    Render::IHyprRenderer::addPassElement(
        canvas.context(), makeUnique<CWindowAnimationPass>(std::move(data)));
}
std::optional<RetainedFrame<CHyprWindowPort::Canvas>> CHyprWindowPort::retained_mapped_image(
    Handle handle) const {
    const auto *cache = find_visuals(handle);
    if (cache == nullptr || !cache->image || !cache->image_owner.ensure_current()) {
        return std::nullopt;
    }
    const auto TEXTURE = cache->image->getTexture();
    const auto SIZE = TEXTURE ? vec_of(TEXTURE->m_size) : hyprimate::Vec2{};
    return RetainedFrame<Canvas>{.image = &cache->image,
        .image_origin = cache->image_origin,
        .image_size = SIZE,
        .capture = cache->capture,
        .frame = cache->frame};
}
bool CHyprWindowPort::bind_fadeout(Handle handle, [[maybe_unused]] std::uint64_t fadeout, RetainedImage image) {
    auto &cache = visual_cache(handle);
    // Fadeout creation can finish outside a render frame. Its framebuffer is a
    // Hyprland snapshot, so verify that compositor context before retaining it.
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !image || !image->getTexture() || !cache.fadeout_owner.ensure_current()) return false;
    hyprimate::gl::release_native(cache.fadeout_image, cache.fadeout_owner);
    cache.fadeout_image = std::move(image);
    return true;
}
CHyprWindowPort::RetainedImage CHyprWindowPort::fadeout_image(Handle handle) const {
    const auto *cache = find_visuals(handle);
    return cache != nullptr && cache->fadeout_image && cache->fadeout_owner.ensure_current()
        ? cache->fadeout_image : RetainedImage{};
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
        const auto WORKSPACE = FADEOUT->workspace().lock();  // NOLINT(clang-analyzer-cplusplus.NewDelete)
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
void CHyprWindowPort::forget_window_reference([[maybe_unused]] WindowId window) {
    // Keep live weak identities stable if a later host event reports the same object.
    std::erase_if(m_windows, [](const auto &item) { return item.object.expired(); });
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
