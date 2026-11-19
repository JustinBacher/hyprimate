#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "AnimationCloseRenderer.hpp"
#include "AnimationTransformer.hpp"
#include "CloseStyles.hpp"
#include "LiveStyle.hpp"
#include "NativeFadeoutPass.hpp"
#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace {

    using hyprimate::STALL_SECONDS;
    using hyprimate::Vec2;

    // Symbols are selected by these exact demangled signatures, verified against the installed
    // headers.
    constexpr const char *CREATE_SIGNATURE =
        "Desktop::CWindowFadeout::create(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>,"
        " "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>)";
    constexpr const char *RENDER_SIGNATURE =
        "Render::IHyprRenderer::renderFadeouts(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Desktop::eFadeoutPlane, Hyprutils::Memory::CSharedPointer<Workspace::CHLWorkspace>, "
        "Render::eSceneMode)";

    // findFunctionsByName substring-matches MANGLED symbol names; the exact demangled signature
    // then selects one.
    constexpr const char *CREATE_NAME = "CWindowFadeout6create";
    constexpr const char *RENDER_NAME = "renderFadeouts";
    constexpr double PENDING_TIMEOUT_SECONDS = 1.0;
    constexpr size_t BATCH_CAPACITY = 16;

    using CreateFn = SP<Desktop::CWindowFadeout> (*)(
        PHLWINDOW, SP<Render::IFramebuffer>, float, SP<Render::IFramebuffer>);
    using RenderFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLMONITOR,
        Desktop::eFadeoutPlane,
        PHLWORKSPACE,
        Render::eSceneMode);

    // Returns the address of the single symbol whose demangled signature matches exactly, or
    // nullptr otherwise.
    void *exact_symbol(const std::vector<SFunctionMatch> &matches, const char *signature) {
        void *found = nullptr;
        int count = 0;
        for (const auto &match : matches) {
            if (match.demangled == signature) {
                found = match.address;
                ++count;
            }
        }
        return count == 1 ? found : nullptr;
    }

    // Owned placement in CBox terms; the arithmetic lives in hyprimate::owned_render_box.
    CBox owned_render_box(
        const CBox &source_device, const CBox &native_box, const Vector2D &snapshot_size) {
        return to_cbox(hyprimate::owned_render_box(to_box(source_device),
            to_box(native_box),
            Vec2{.x = snapshot_size.x, .y = snapshot_size.y}));
    }

    // Exact box equality: a box that did not move is not damaged again.
    bool same_box(const CBox &lhs, const CBox &rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
    }

    // Adds `box` to `damage` unless it is empty or already listed.
    void append_damage(std::vector<CBox> &damage, const CBox &box) {
        if (box.empty() || std::ranges::any_of(damage,
                               [&box](const CBox &other) { return same_box(other, box); })) {
            return;
        }
        damage.push_back(box);
    }

    // Outward rounding keeps every pixel in the style's footprint inside the damaged box.
    CBox drawn_box_of(const CAnimationCloseRenderer::SClosing &record, const PHLMONITOR &monitor) {
        const CBox DEVICE = std::visit(
            [&](const auto &style) { return close_style::footprint(style, record); }, record.style);

        const double SCALE = record.scale;
        const double LEFT = std::floor((DEVICE.x / SCALE) + monitor->m_position.x);
        const double TOP = std::floor((DEVICE.y / SCALE) + monitor->m_position.y);
        const double RIGHT = std::ceil(((DEVICE.x + DEVICE.w) / SCALE) + monitor->m_position.x);
        const double BOTTOM = std::ceil(((DEVICE.y + DEVICE.h) / SCALE) + monitor->m_position.y);
        return CBox{LEFT, TOP, RIGHT - LEFT, BOTTOM - TOP};
    }

    // An owned record stops drawing: its last drawn box is damaged once so those pixels clear.
    void release_drawn(CAnimationCloseRenderer::SClosing &record, std::vector<CBox> &damage) {
        if (record.owned) {
            append_damage(damage, record.drawn_box);
        }
        record.drawn_box = CBox{};
    }

    // A bound record matches only its captured workspace (an expired one matches nothing); an
    // unbound record matches any workspace, as the native fadeout does.
    bool record_on_workspace(
        const CAnimationCloseRenderer::SClosing &record, const PHLWORKSPACE &workspace) {
        if (!record.workspace_bound) {
            return true;
        }
        const auto CAPTURED = record.workspace.lock();
        return CAPTURED && CAPTURED == workspace;
    }

    bool same_monitor(const PHLMONITORREF &ref, const PHLMONITOR &monitor) {
        return ref.lock().get() == monitor.get();
    }

    CAnimationCloseRenderer *active_renderer() {
        return CAnimationCloseRenderer::s_active;
    }

    // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes window by value.
    SP<Desktop::CWindowFadeout> hook_create(PHLWINDOW window,
        SP<Render::IFramebuffer> snapshot,
        float source_alpha,
        SP<Render::IFramebuffer> workspace_snapshot) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the original function as void*.
        auto *original = reinterpret_cast<CreateFn>(
            (active_renderer() != nullptr) ? active_renderer()->create_original() : nullptr);
        if (original == nullptr) {
            return nullptr;
        }

        // Never move from hooked by-value parameters: disassembly of the pinned host shows callers
        // keep the argument's control-block pointer in a register and release it after the call,
        // ignoring a moved-from slot.
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host releases both by-value snapshot slots after the call.
        auto created = original(window, snapshot, source_alpha, workspace_snapshot);
        if (auto *self = active_renderer()) {
            try {
                self->on_fadeout_created(window, created);
            } catch (...) {
                // Association is best effort; return the already-created native fadeout intact.
                return created;
            }
        }
        return created;
    }

    void hook_render(Render::IHyprRenderer *self,
        Render::CRenderContext &ctx,
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes monitor by value.
        PHLMONITOR monitor,
        Desktop::eFadeoutPlane plane,
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes workspace by value.
        PHLWORKSPACE workspace,
        Render::eSceneMode mode) {
        auto *active = active_renderer();
        if ((active != nullptr) && mode == Render::eSceneMode::MONITOR &&
            active->has_record_for(monitor, plane, workspace)) {
            bool submitted = false;
            try {
                submitted = active->render_batch(ctx, monitor, plane, workspace);
            } catch (...) {
                // The batch may be partially submitted, so the native pass must not run on top of
                // it.
                return;
            }
            if (submitted) {
                return;
            }
            // Not handled (batch larger than the preallocated scratch): nothing was submitted, draw
            // natively.
        }

        auto *original =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the original function as void*.
            reinterpret_cast<RenderFn>((active != nullptr) ? active->render_original() : nullptr);
        if (original != nullptr) {
            original(self, ctx, monitor, plane, workspace, mode);
        }
    }

    bool running(const SStyleState &style) {
        return std::visit([](const auto &value) { return close_style::running(value); }, style);
    }
    bool finished(const SStyleState &style) {
        return std::visit([](const auto &value) { return close_style::finished(value); }, style);
    }

}  // namespace

bool close_animation_enabled(const hyprimate::config::SConfig &config,
    const SStyleResources<StyleModules> &resources) {
    switch (config.style) {
#define HYPRIMATE_VALID_STYLE(kind, tag, module) case hyprimate::config::Style::kind: break;
        HYPRIMATE_STYLE_LIST(HYPRIMATE_VALID_STYLE, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_VALID_STYLE
        default: return false;
    }
    return with_live_style(config.style, [&](auto live) {
        using Live = decltype(live);
        return close_style::close_enabled(typename Live::CloseData{}, resources.get<typename Live::Module>());
    });
}

CAnimationCloseRenderer *CAnimationCloseRenderer::s_active = nullptr;

CAnimationCloseRenderer::CAnimationCloseRenderer(
    HANDLE handle, SStyleResources<StyleModules> &resources)
    : m_handle(handle), m_resources(resources) {
}

CAnimationCloseRenderer::~CAnimationCloseRenderer() {
    shutdown();
}

bool CAnimationCloseRenderer::install(std::string &error) {
    shutdown();

    const void *create_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, CREATE_NAME), CREATE_SIGNATURE);
    const void *render_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, RENDER_NAME), RENDER_SIGNATURE);
    if ((create_address == nullptr) || (render_address == nullptr)) {
        error = "close symbols not found by exact signature";
        return false;
    }

    m_batch.reserve(BATCH_CAPACITY);
    s_active = this;
    m_create_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void* destination address.
        m_handle, create_address, reinterpret_cast<const void *>(&hook_create));
    m_render_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void* destination address.
        m_handle, render_address, reinterpret_cast<const void *>(&hook_render));
    if ((m_create_hook == nullptr) || (m_render_hook == nullptr) || !m_create_hook->hook()) {
        shutdown();
        error = "failed to hook CWindowFadeout::create";
        return false;
    }
    if (!m_render_hook->hook()) {
        shutdown();
        error = "failed to hook IHyprRenderer::renderFadeouts";
        return false;
    }

    return true;
}

void CAnimationCloseRenderer::shutdown() {
    // Hooks come down first so no new trampoline call can reach this object.
    // removeFunctionHook unhooks (if active) and releases the host-owned hook.
    if (m_render_hook != nullptr) {
        HyprlandAPI::removeFunctionHook(m_handle, m_render_hook);
    }
    if (m_create_hook != nullptr) {
        HyprlandAPI::removeFunctionHook(m_handle, m_create_hook);
    }
    m_render_hook = nullptr;
    m_create_hook = nullptr;
    s_active = nullptr;
    clear();
}

void CAnimationCloseRenderer::clear() {
    // Owned closes may still have a last frame on screen. Damage their last drawn boxes so those
    // pixels clear before the records go. Called from teardown where no tick damage loop runs, so
    // this damages directly. Native fadeouts continue on their own.
    if (g_pHyprRenderer) {
        for (const auto &record : m_records) {
            if (record->owned && !record->drawn_box.empty()) {
                g_pHyprRenderer->damageBox(record->drawn_box);
            }
        }
    }
    m_pending.clear();
    m_records.clear();
}

void CAnimationCloseRenderer::stage(const PHLWINDOW &window,
    SStyleState &state,
    bool state_active,
    const std::function<hyprimate::EdgeInput()> &edge_input,
    const std::function<std::span<const hyprimate::Box>()> &obstacles,
    const std::function<std::uint64_t()> &next_seed) {
    auto pending = pending_for(window);
    const bool CAN_STAGE = pending.has_value();
    if (!pending) {
        pending.emplace();
    }
    const bool STAGED = std::visit(
        [&](auto &style) {
            using Live = decltype(live_style_for(style));
            return close_style::stage(style,
                *pending,
                window,
                state_active,
                m_resources.get<typename Live::Module>(),
                edge_input,
                obstacles,
                next_seed);
        },
        state);
    if (STAGED && CAN_STAGE) {
        push(std::move(*pending));
    }
}

std::optional<CAnimationCloseRenderer::SPending> CAnimationCloseRenderer::pending_for(
    const PHLWINDOW &window) {
    if (!window) {
        return std::nullopt;
    }

    const auto MONITOR = window->m_monitor.lock();
    if (!MONITOR) {
        return std::nullopt;
    }

    const double SCALE = MONITOR->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0) {
        return std::nullopt;
    }

    const CBox CAPTURE = window->getFullWindowBoundingBox();
    if (CAPTURE.empty()) {
        return std::nullopt;
    }

    // Window position inside the snapshot: the snapshot's presentation offsets, then monitor-local
    // device px.
    const auto PRESENTATION = Render::resolveWindowSnapshotPresentation(
        window->presentation().renderPresentation(Render::eSceneMode::MONITOR));
    const CBox SOURCE = CAPTURE.copy()
                            .translate(PRESENTATION.workspaceOffset + PRESENTATION.floatingOffset)
                            .translate(-MONITOR->m_position)
                            .scale(SCALE);
    if (SOURCE.empty()) {
        return std::nullopt;
    }

    SPending pending;
    pending.window = PHLWINDOWREF{window};
    pending.source_device = SOURCE;
    pending.scale = SCALE;
    pending.capture_logical = Vec2{.x = CAPTURE.w, .y = CAPTURE.h};
    pending.staged = Clock::now();
    return pending;
}

void CAnimationCloseRenderer::push(SPending pending) {
    std::erase_if(m_pending,
        [&pending](const SPending &item) { return item.window.get() == pending.window.get(); });
    m_pending.push_back(std::move(pending));
}

void CAnimationCloseRenderer::on_fadeout_created(
    const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created) {
    const auto PENDING_MATCH = std::ranges::find_if(
        m_pending, [&window](const SPending &item) { return item.window.get() == window.get(); });
    if (PENDING_MATCH == m_pending.end()) {
        return;
    }

    SPending pending = std::move(*PENDING_MATCH);
    m_pending.erase(PENDING_MATCH);

    if (!created || window.get() != pending.window.get()) {
        return;
    }
    if (std::chrono::duration<double>(Clock::now() - pending.staged).count() >
        PENDING_TIMEOUT_SECONDS) {
        return;
    }

    const auto MONITOR = created->monitor().lock();
    const auto SNAPSHOT = created->framebuffer(Render::eSceneMode::MONITOR);
    if (!MONITOR || !SNAPSHOT || !SNAPSHOT->getTexture() || MONITOR->m_scale != pending.scale) {
        return;
    }
    // Retained records and their snapshots are bounded by the batch scratch they must fit in;
    // beyond that the native fadeout runs unmodified.
    if (m_records.size() >= BATCH_CAPACITY) {
        return;
    }

    if (!std::visit(
            [&](const auto &style) {
                return close_style::valid(style, pending, created->effects().textureBlur.enabled);
            },
            pending.style)) {
        return;
    }

    auto record = std::make_unique<SClosing>();
    const SP<Desktop::IFadeout> BASE = created;
    record->fadeout = WP<Desktop::IFadeout>{BASE};
    record->snapshot = SNAPSHOT;
    record->monitor = PHLMONITORREF{MONITOR};
    record->workspace = created->workspace();
    record->workspace_bound = static_cast<bool>(record->workspace.lock());
    record->plane = Render::sceneFadeoutPlane(
        Render::eSceneMode::MONITOR, created->plane(), created->source().pinned);
    record->z_index = created->zIndex();
    record->no_screen_share = created->source().noScreenShare;
    record->style = std::move(pending.style);
    record->source_alpha = created->alpha();
    record->source_device = pending.source_device;
    record->scale = pending.scale;
    record->capture_logical = pending.capture_logical;
    record->snapshot_size = SNAPSHOT->getTexture()->m_size;
    record->last_advance = Clock::now();

    const bool TEXTURE_BLUR = created->effects().textureBlur.enabled;
    std::visit(
        [&](auto &style) { close_style::begin(style, pending, TEXTURE_BLUR); }, record->style);
    record->close_data = std::move(pending.close_data);
    record->owned = running(record->style) && record->source_alpha > 0.F;
    if (!record->owned &&
        std::visit(
            [](const auto &style) { return close_style::needs_owned(style); }, record->style)) {
        return;
    }
    if (record->owned) {
        // Material-state closes stay at the captured position, independent of native slide/pop.
        record->render_box =
            std::visit([](const auto &style) {
                using Module = typename SStyleModuleFor<std::decay_t<decltype(style)>>::Module;
                return Module::FIXED_CLOSE_PLACEMENT;
            }, record->style)
                ? CBox{0.0, 0.0, record->snapshot_size.x, record->snapshot_size.y}
                : owned_render_box(
                      record->source_device, created->renderBox(), record->snapshot_size);
    }

    fill_vertices(*record);
    m_records.push_back(std::move(record));
}

void CAnimationCloseRenderer::fill_vertices(SClosing &record) {
    std::visit([&](auto &style) { close_style::fill(style, record); }, record.style);
}

void CAnimationCloseRenderer::tick(
    Clock::time_point now, const PHLMONITOR &rendering, std::vector<CBox> &damage) {
    damage.clear();

    std::erase_if(m_pending, [now](const SPending &item) {
        return std::chrono::duration<double>(now - item.staged).count() > PENDING_TIMEOUT_SECONDS;
    });

    std::erase_if(m_records, [&](const std::unique_ptr<SClosing> &record) {
        const auto FADEOUT = record->fadeout.lock();
        const bool NATIVE_ALIVE = FADEOUT && !FADEOUT->done();
        const auto MONITOR = record->monitor.lock();

        if (!MONITOR || MONITOR->m_scale != record->scale) {
            release_drawn(*record, damage);
            return true;
        }

        if (record->owned) {
            // A suppressed close keeps its record (so the native fade stays suppressed) until the
            // native fadeout ends.
            if (finished(record->style)) {
                release_drawn(*record, damage);
                return !NATIVE_ALIVE;
            }

            if (NATIVE_ALIVE && !std::visit([](const auto &style) {
                    using Module = typename SStyleModuleFor<std::decay_t<decltype(style)>>::Module;
                    return Module::FIXED_CLOSE_PLACEMENT;
                }, record->style)) {
                record->render_box = owned_render_box(
                    record->source_device, FADEOUT->renderBox(), record->snapshot_size);
            }

            bool advanced = false;
            if (MONITOR == rendering) {
                const double ELAPSED =
                    std::clamp(std::chrono::duration<double>(now - record->last_advance).count(),
                        0.0,
                        STALL_SECONDS);
                std::visit(
                    [&](auto &style) { close_style::advance(style, *record, ELAPSED, true); },
                    record->style);
                fill_vertices(*record);
                record->last_advance = now;
                advanced = true;
            }

            // An advancing record damages its box every frame, which also schedules its next frame.
            // A record that did not advance only damages when the native fadeout moved its box.
            const CBox CURRENT = drawn_box_of(*record, MONITOR);
            if (advanced || !same_box(CURRENT, record->drawn_box)) {
                append_damage(damage, record->drawn_box);
                append_damage(damage, CURRENT);
            }
            record->drawn_box = CURRENT;
            return false;
        }

        // Unowned closes contribute no damage: the native fadeout damages its own box.
        if (!NATIVE_ALIVE) {
            return true;
        }

        // A non-owned close that has settled needs no more simulation or vertex refills; the native
        // fadeout keeps scheduling its own alpha frames and the settled mesh is still substituted.
        if (std::visit(
                [](const auto &style) { return close_style::settled(style); }, record->style) ||
            MONITOR != rendering) {
            return false;
        }

        const double ELAPSED = std::clamp(
            std::chrono::duration<double>(now - record->last_advance).count(), 0.0, STALL_SECONDS);
        std::visit([&](auto &style) { close_style::advance(style, *record, ELAPSED, false); },
            record->style);
        record->last_advance = now;
        return false;
    });
}

bool CAnimationCloseRenderer::needs_frame(const PHLMONITOR &monitor) const {
    if (!monitor) {
        return false;
    }
    return std::ranges::any_of(m_records, [&](const auto &record) {
        return record->owned && same_monitor(record->monitor, monitor) &&
               (!finished(record->style) || !record->drawn_box.empty());
    });
}

CAnimationCloseRenderer::SClosing *CAnimationCloseRenderer::record_for(
    const SP<Desktop::IFadeout> &fadeout) const {
    for (const auto &record : m_records) {
        if (record->fadeout.lock().get() == fadeout.get()) {
            return record.get();
        }
    }
    return nullptr;
}

bool CAnimationCloseRenderer::has_record_for(
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) const {
    for (const auto &record : m_records) {
        if (!same_monitor(record->monitor, monitor) || record->plane != plane) {
            continue;
        }
        if (!record_on_workspace(*record, workspace)) {
            continue;
        }
        return true;
    }
    return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Batch hot path preserves collection, fallback and draw ordering without extra calls.
bool CAnimationCloseRenderer::render_batch(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor,
    Desktop::eFadeoutPlane plane,
    const PHLWORKSPACE &workspace) {
    if (!monitor) {
        return true;
    }

    // Mirrors IHyprRenderer::renderFadeouts (MONITOR mode). m_batch keeps the capacity reserved at
    // install and is never grown here: a larger batch returns false so the caller delegates it to
    // the native renderer.
    std::vector<SBatchItem> capture_batch;
    if (WorkspaceCaptureScope::forContext(ctx)) {
        capture_batch.reserve(BATCH_CAPACITY);
    }
    auto &batch = WorkspaceCaptureScope::forContext(ctx) ? capture_batch : m_batch;
    batch.clear();

    // Native pass: live fadeouts in the native selection. An owned close whose native fadeout has
    // finished is left to the orphan pass below, so each owned close is drawn exactly once.
    for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (!fadeout || fadeout->monitor().lock() != monitor) {
            continue;
        }

        const auto SOURCE = fadeout->source();
        if (!ctx.shouldRenderContent(SOURCE.noScreenShare)) {
            continue;
        }
        if (Render::sceneFadeoutPlane(
                Render::eSceneMode::MONITOR, fadeout->plane(), SOURCE.pinned) != plane) {
            continue;
        }
        const auto FADEOUT_WORKSPACE = fadeout->workspace().lock();
        if (FADEOUT_WORKSPACE && FADEOUT_WORKSPACE != workspace) {
            continue;
        }

        SClosing *record = record_for(fadeout);
        if ((record != nullptr) && !record_on_workspace(*record, workspace)) {
            record = nullptr;
        }
        if ((record != nullptr) && record->owned && fadeout->done()) {
            continue;
        }

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(
            SBatchItem{.fadeout = fadeout, .record = record, .z_index = fadeout->zIndex()});
    }

    // Orphan pass: owned closes whose native fadeout finished or vanished keep drawing from their
    // transition.
    for (const auto &record : m_records) {
        if (!record->owned || finished(record->style) || !same_monitor(record->monitor, monitor)) {
            continue;
        }
        if (record->plane != plane || !record_on_workspace(*record, workspace) ||
            !ctx.shouldRenderContent(record->no_screen_share)) {
            continue;
        }
        const auto FADEOUT = record->fadeout.lock();
        if (FADEOUT && !FADEOUT->done()) {
            continue;
        }

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(
            SBatchItem{.fadeout = nullptr, .record = record.get(), .z_index = record->z_index});
    }

    std::ranges::sort(batch, {}, &SBatchItem::z_index);

    CNativeFadeoutPass native{ctx, monitor};
    for (const auto &item : batch) {
        SClosing *record = item.record;

        if ((record != nullptr) && record->owned) {
            // A closed owned close draws nothing and suppresses its native fade, dim and blur
            // included.
            if (finished(record->style)) {
                continue;
            }
            const float ALPHA = record->source_alpha;
            if (ALPHA <= 0.F) {
                continue;
            }
            const auto TEX = record->snapshot ? record->snapshot->getTexture() : nullptr;
            if (!TEX) {
                continue;
            }

            // Native backdrop effects apply only while the native fadeout is still live.
            if (item.fadeout) {
                native.draw_backdrop(item.fadeout->effects());
            }

            std::visit(
                [&](auto &style) {
                    using Module = typename SStyleModuleFor<std::decay_t<decltype(style)>>::Module;
                    close_style::render(style, *record, ctx, monitor, TEX, m_resources.get<Module>());
                },
                record->style);
            continue;
        }

        // Non-owned: a native fadeout (the orphan pass only yields owned records, so item.fadeout
        // is non-null here).
        const auto &fadeout = item.fadeout;
        if (!fadeout) {
            continue;
        }
        const auto FRAMEBUFFER = fadeout->framebuffer(Render::eSceneMode::MONITOR);
        if (!FRAMEBUFFER || !FRAMEBUFFER->getTexture()) {
            continue;
        }

        const auto EFFECTS = fadeout->effects();
        native.draw_backdrop(EFFECTS);

        const auto TEX = FRAMEBUFFER->getTexture();
        // Texture blur rides on a stock element; the mesh path only replaces the plain texture
        // draw.
        if (record != nullptr && same_monitor(record->monitor, monitor) &&
            !EFFECTS.textureBlur.enabled && TEX->m_size.x == record->snapshot_size.x &&
            TEX->m_size.y == record->snapshot_size.y &&
            std::visit(
                [&](auto &style) {
                    return close_style::render_native(
                        style, *record, ctx, fadeout, TEX, native.damage());
                },
                record->style)) {
            continue;
        }

        native.draw_texture(*fadeout, TEX, EFFECTS);
    }
    batch.clear();
    return true;
}
