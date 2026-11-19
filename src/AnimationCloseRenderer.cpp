#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
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
#include "HyprWindowPort.hpp"
#include "NativeFadeoutPass.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimations.hpp"
#include "WindowAnimationPass.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace {

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
}  // namespace


CAnimationCloseRenderer *CAnimationCloseRenderer::s_active = nullptr;

CAnimationCloseRenderer::CAnimationCloseRenderer(
    HANDLE handle, HyprWindowAnimations &animations, CHyprWindowPort &port)
    : m_handle(handle), m_animations(animations), m_port(port) {
}

CAnimationCloseRenderer::~CAnimationCloseRenderer() {
    unhook();
}

bool CAnimationCloseRenderer::install(std::string &error) {
    unhook();

    const void *create_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, CREATE_NAME), CREATE_SIGNATURE);
    const void *render_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, RENDER_NAME), RENDER_SIGNATURE);
    if ((create_address == nullptr) || (render_address == nullptr)) {
        error = "close symbols not found by exact signature";
        return false;
    }

    m_batch.reserve(BATCH_CAPACITY);
    m_records.reserve(BATCH_CAPACITY);
    s_active = this;
    m_create_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void* destination address.
        m_handle, create_address, reinterpret_cast<const void *>(&hook_create));
    m_render_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void* destination address.
        m_handle, render_address, reinterpret_cast<const void *>(&hook_render));
    if ((m_create_hook == nullptr) || (m_render_hook == nullptr) || !m_create_hook->hook()) {
        unhook();
        error = "failed to hook CWindowFadeout::create";
        return false;
    }
    if (!m_render_hook->hook()) {
        unhook();
        error = "failed to hook IHyprRenderer::renderFadeouts";
        return false;
    }

    return true;
}

void CAnimationCloseRenderer::unhook() {
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
    m_batch.clear();
}

void CAnimationCloseRenderer::prune_records() {
    const auto VIEWS = m_animations.dying_views();
    std::erase_if(m_records, [&](const Record &record) {
        const bool ACTIVE = std::ranges::find(VIEWS, record.fadeout, &hyprimate::animation::DyingView::fadeout) != VIEWS.end();
        const auto NATIVE = record.native.lock();
        if (!NATIVE || NATIVE->done()) {
            m_animations.native_finished(record.fadeout);
            return !ACTIVE;
        }
        return !ACTIVE && !m_animations.suppresses(record.fadeout);
    });
}

void CAnimationCloseRenderer::on_fadeout_created(
    const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created) {
    using namespace hyprimate::animation;
    if (!window) {
        return;
    }
    const auto WINDOW_ID = m_port.identity(PHLWINDOWREF{window});
    const auto PENDING = m_animations.pending_for(WINDOW_ID);
    if (!PENDING) {
        return;
    }
    m_records.reserve(m_records.size() + 1);
    prune_records();
    const auto FADEOUT = created ? m_port.register_fadeout(created) : 0;
    auto facts = FADEOUT != 0 ? m_port.fadeout_facts(FADEOUT).value_or(FadeoutFacts{}) : FadeoutFacts{};
    facts.now = Clock::now();
    const auto IMAGE = created ? created->framebuffer(Render::eSceneMode::MONITOR) : nullptr;
    m_animations.bind_fadeout(WINDOW_ID, facts, IMAGE);
    if (!m_animations.active(*PENDING) || !created) {
        return;
    }
    const SP<Desktop::IFadeout> BASE = created;
    const auto WORKSPACE = created->workspace();
    m_records.push_back(Record{.fadeout = FADEOUT,
        .monitor = created->monitor(),
        .native = WP<Desktop::IFadeout>{BASE},
        .workspace = WORKSPACE,
        .workspace_bound = static_cast<bool>(WORKSPACE.lock()),
        .plane = Render::sceneFadeoutPlane(
            Render::eSceneMode::MONITOR, created->plane(), created->source().pinned),
        .z_index = created->zIndex(),
        .no_screen_share = created->source().noScreenShare});
}

bool CAnimationCloseRenderer::has_record_for(
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) const {
    const auto VIEWS = m_animations.dying_views();
    return std::ranges::any_of(m_records, [&](const Record &record) {
        return record.monitor.lock() == monitor && record.plane == plane &&
            (!record.workspace_bound || (record.workspace.lock() && record.workspace.lock() == workspace)) &&
            (std::ranges::find(VIEWS, record.fadeout, &hyprimate::animation::DyingView::fadeout) != VIEWS.end() ||
                m_animations.suppresses(record.fadeout));
    });
}

bool CAnimationCloseRenderer::render_batch(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) {
    using namespace hyprimate::animation;
    if (!monitor) {
        return true;
    }
    const bool CAPTURE = WorkspaceCaptureScope::for_context(ctx) != nullptr;
    if (!CAPTURE) {
        prune_records();
    }
    // Nested capture must not overwrite a live batch or the module's view scratch.
    std::vector<BatchItem> capture_batch;
    if (CAPTURE) {
        capture_batch.reserve(BATCH_CAPACITY);
    }
    auto &batch = CAPTURE ? capture_batch : m_batch;
    batch.clear();
    std::array<DyingView, BATCH_CAPACITY> copied_views{};
    const auto VIEWS = m_animations.dying_views();
    if (VIEWS.size() > copied_views.size()) {
        return false;
    }
    std::ranges::copy(VIEWS, copied_views.begin());
    const std::span<const DyingView> SNAPSHOT{copied_views.data(), VIEWS.size()};
    if (!collect_batch(ctx, monitor, plane, workspace, batch, SNAPSHOT)) {
        batch.clear();
        return false;
    }
    draw_batch(ctx, monitor, batch, SNAPSHOT);
    batch.clear();
    return true;
}

bool CAnimationCloseRenderer::collect_batch(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
    Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace, std::vector<BatchItem> &batch,
    std::span<const hyprimate::animation::DyingView> views) {
    using namespace hyprimate::animation;
    const auto MATCHES = [&](const Record &record) {
        const auto WORKSPACE = record.workspace.lock();
        return record.monitor.lock() == monitor && record.plane == plane &&
            (!record.workspace_bound || (WORKSPACE && WORKSPACE == workspace));
    };
    for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (!fadeout || fadeout->monitor().lock() != monitor) {
            continue;
        }
        const auto SOURCE = fadeout->source();
        if (!ctx.shouldRenderContent(SOURCE.noScreenShare) ||
            Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, fadeout->plane(), SOURCE.pinned) != plane) {
            continue;
        }
        const auto NATIVE_WORKSPACE = fadeout->workspace().lock();
        if (NATIVE_WORKSPACE && NATIVE_WORKSPACE != workspace) {
            continue;
        }
        const auto FADEOUT_ID = m_port.fadeout_identity(fadeout);
        const auto FOUND = std::ranges::find(m_records, FADEOUT_ID, &Record::fadeout);
        const Record *record = FADEOUT_ID != 0 && FOUND != m_records.end() && MATCHES(*FOUND) ? &*FOUND : nullptr;
        if (record != nullptr && fadeout->done() && m_animations.suppresses(FADEOUT_ID)) {
            continue;
        }
        if (batch.size() == batch.capacity()) {
            return false;
        }
        batch.push_back(BatchItem{.fadeout = fadeout, .record = record, .z_index = fadeout->zIndex()});
    }
    for (const auto &record : m_records) {
        const auto FOUND = std::ranges::find(views, record.fadeout, &DyingView::fadeout);
        if (FOUND == views.end() || FOUND->ownership != CloseStart::Owned || !MATCHES(record) ||
            !ctx.shouldRenderContent(record.no_screen_share)) {
            continue;
        }
        const auto NATIVE = record.native.lock();
        if (NATIVE && !NATIVE->done()) {
            continue;
        }
        if (batch.size() == batch.capacity()) {
            return false;
        }
        batch.push_back(BatchItem{.fadeout = nullptr, .record = &record, .z_index = record.z_index});
    }
    std::ranges::sort(batch, {}, &BatchItem::z_index);
    return true;
}

void CAnimationCloseRenderer::draw_batch(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
    const std::vector<BatchItem> &batch, std::span<const hyprimate::animation::DyingView> views) {
    using namespace hyprimate::animation;
    CNativeFadeoutPass native{ctx, monitor};
    for (const auto &item : batch) {
        const auto FOUND = item.record != nullptr
            ? std::ranges::find(views, item.record->fadeout, &DyingView::fadeout) : views.end();
        const bool SUPPRESSED = item.record != nullptr && m_animations.suppresses(item.record->fadeout);
        if (FOUND != views.end()) {
            const auto IMAGE = m_port.fadeout_image(FOUND->handle);
            if (IMAGE && IMAGE->getTexture()) {
                if (item.fadeout) {
                    native.draw_backdrop(item.fadeout->effects());
                }
                const auto &bounds = FOUND->bounds_device;
                CWindowAnimationPass::SData data{
                    .resources = &m_port.canvas_resources(),
                    .animations = &m_animations,
                    .handle = FOUND->handle,
                    .image = IMAGE,
                    .source = FOUND->render_box,
                    .output = bounds,
                    .canvas_box = {.x = std::floor(bounds.x), .y = std::floor(bounds.y),
                        .w = std::ceil(bounds.x + bounds.w) - std::floor(bounds.x),
                        .h = std::ceil(bounds.y + bounds.h) - std::floor(bounds.y)},
                    .image_origin = {},
                    .image_size = FOUND->image_size,
                    .scale = FOUND->scale,
                    .alpha = FOUND->alpha,
                    .monitor = FOUND->monitor,
                    .damage = native.damage()};
                Render::IHyprRenderer::addPassElement(ctx, makeUnique<CWindowAnimationPass>(std::move(data)));
                continue;
            }
        }
        if (SUPPRESSED || !item.fadeout) {
            continue;
        }
        const auto FRAMEBUFFER = item.fadeout->framebuffer(Render::eSceneMode::MONITOR);
        if (!FRAMEBUFFER || !FRAMEBUFFER->getTexture()) {
            continue;
        }
        const auto EFFECTS = item.fadeout->effects();
        native.draw_backdrop(EFFECTS);
        native.draw_texture(*item.fadeout, FRAMEBUFFER->getTexture(), EFFECTS);
    }
}
