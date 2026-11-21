#include <algorithm>
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
        auto *self = active_renderer();
        if (self == nullptr) { return nullptr; }
        const auto CALLBACK = self->callback_ticket();
        const auto PENDING = self->pending_for(window);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the original function as void*.
        auto *original = reinterpret_cast<CreateFn>(self->create_original());
        if (original == nullptr) {
            return nullptr;
        }

        // Never move from hooked by-value parameters: disassembly of the pinned host shows callers
        // keep the argument's control-block pointer in a register and release it after the call,
        // ignoring a moved-from slot.
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host releases both by-value snapshot slots after the call.
        auto created = original(window, snapshot, source_alpha, workspace_snapshot);
        if (CALLBACK.valid() && PENDING && self == active_renderer()) {
            try {
                self->on_fadeout_created(window, created, *PENDING);
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
        const auto ORIGINAL =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the original function as void*.
            reinterpret_cast<RenderFn>(active ? active->render_original() : nullptr);
        if ((active != nullptr) && mode == Render::eSceneMode::MONITOR &&
            active->has_record_for(monitor, plane, workspace)) {
            try {
                if (active->render_batch(ctx, monitor, plane, workspace)) {
                    return;
                }
            } catch (...) {
                // Drawing may already have submitted passes. Never replay the native plane.
                return;
            }
        }

        if (ORIGINAL != nullptr) {
            ORIGINAL(self, ctx, monitor, plane, workspace, mode);
        }
    }
}  // namespace


bool CloseRenderGuard::valid(Render::CRenderContext &ctx, bool require_native) const {
    if (!renderer.valid() || !model.valid()) { return false; }
    const bool CAPTURE = ctx.readOnlyEffects() || ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
        WorkspaceCaptureScope::for_context(ctx) != nullptr ||
        WorkspaceCaptureScope::is_destination_live(ctx);
    const auto ALLOWED = [&](bool private_content) {
        return ctx.shouldRenderContent(private_content) && (!CAPTURE || !private_content);
    };
    if (!ALLOWED(no_screen_share)) { return false; }
    const auto ID = native ? port->fadeout_identity(native) : fadeout;
    if (native) {
        const auto SOURCE = native->source();
        const auto WORKSPACE = native->workspace().lock();
        if (!ALLOWED(SOURCE.noScreenShare) || native->monitor().lock() != monitor ||
            (WORKSPACE && WORKSPACE != workspace) ||
            Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, native->plane(), SOURCE.pinned) != plane) {
            return false;
        }
    }
    const bool NATIVE_LIVE = native && !native->done() &&
        std::ranges::any_of(Desktop::fadingOutState()->fadeouts(),
            [&](const auto &value) { return value.get() == native.get(); });
    if (require_native && !NATIVE_LIVE) { return false; }
    if (handle) {
        const auto VIEW = animations->dying_view_for(fadeout);
        return ID == fadeout && VIEW && VIEW->handle == *handle &&
            (VIEW->ownership == hyprimate::animation::CloseStart::Owned || NATIVE_LIVE) &&
            VIEW->monitor == port->monitor_identity(monitor) && ALLOWED(VIEW->no_screen_share);
    }
    return NATIVE_LIVE && !animations->suppresses(ID);
}
CAnimationCloseRenderer *CAnimationCloseRenderer::s_active = nullptr;

CAnimationCloseRenderer::CAnimationCloseRenderer(
    HANDLE handle, HyprWindowAnimations &animations, CHyprWindowPort &port)
    : m_handle(handle), m_animations(animations), m_port(port) {
}

CAnimationCloseRenderer::~CAnimationCloseRenderer() {
    unhook();
}

bool CAnimationCloseRenderer::install(std::string &error) {
    if (s_active != nullptr && s_active != this) {
        error = "another close renderer owns the hooks";
        return false;
    }
    unhook();

    const void *create_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, CREATE_NAME), CREATE_SIGNATURE);
    const void *render_address =
        exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, RENDER_NAME), RENDER_SIGNATURE);
    if ((create_address == nullptr) || (render_address == nullptr)) {
        error = "close symbols not found by exact signature";
        return false;
    }

    m_records.reserve(16);
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
    m_callbacks.invalidate();
    if (s_active == this) { s_active = nullptr; }
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
}

void CAnimationCloseRenderer::prune_records() {
    const auto CALLBACK = callback_ticket();
    const auto MODEL = m_animations.callback_ticket();
    // Copy before native_finished: it can end an animation and synchronously reenter us.
    // Newly appended records belong to a later prune, not this invocation.
    std::optional<std::uint64_t> before;
    for (;;) {
        std::optional<Record> selected;
        for (const auto &record : m_records) {
            if ((!before || record.fadeout < *before) &&
                (!selected || record.fadeout > selected->fadeout)) { selected = record; }
        }
        if (!selected) { return; }
        const auto RECORD = *selected;
        before = RECORD.fadeout; // Port IDs are monotonic; growth cannot enter this prune.
        const auto NATIVE = RECORD.native.lock();
        if (RECORD.binding_renderer.valid() && RECORD.binding_model.valid()) { continue; }
        if (!NATIVE || NATIVE->done()) {
            m_animations.native_finished(RECORD.fadeout);
            if (!CALLBACK.valid() || !MODEL.valid()) { return; }
        }
        if (!m_animations.dying_view_for(RECORD.fadeout) &&
            !m_animations.suppresses(RECORD.fadeout)) {
            std::erase_if(m_records, [&](const Record &value) { return value.fadeout == RECORD.fadeout; });
        }
    }
}

std::optional<hyprimate::animation::Handle> CAnimationCloseRenderer::pending_for(
    const PHLWINDOW &window) const {
    return window ? m_animations.pending_for(m_port.identity(PHLWINDOWREF{window})) : std::nullopt;
}

void CAnimationCloseRenderer::on_fadeout_created(
    const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created,
    hyprimate::animation::Handle pending) {
    using namespace hyprimate::animation;
    if (!window || pending_for(window) != pending) { return; }
    const auto CALLBACK = callback_ticket();
    const auto MODEL = m_animations.callback_ticket();
    const auto WINDOW_ID = m_port.identity(PHLWINDOWREF{window});
    prune_records();
    if (!CALLBACK.valid() || !MODEL.valid() || pending_for(window) != pending) { return; }
    const auto FADEOUT = created ? m_port.register_fadeout(created) : 0;
    auto facts = FADEOUT != 0 ? m_port.fadeout_facts(FADEOUT).value_or(FadeoutFacts{}) : FadeoutFacts{};
    facts.now = Clock::now();
    const auto IMAGE = created ? created->framebuffer(Render::eSceneMode::MONITOR) : nullptr;
    // Publish before binding can suppress native drawing or call back into this renderer.
    // No vector reference or iterator survives bind_fadeout; nested creation may grow it.
    const bool INSERTED = created && FADEOUT != 0 &&
        std::ranges::none_of(m_records, [&](const Record &record) { return record.fadeout == FADEOUT; });
    if (INSERTED) {
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
            .no_screen_share = created->source().noScreenShare,
            .binding_renderer = CALLBACK, .binding_model = MODEL});
    }
    const auto FINISH_BINDING = [&] {
        if (!INSERTED || !CALLBACK.valid() || !MODEL.valid()) { return; }
        const auto FOUND = std::ranges::find(m_records, FADEOUT, &Record::fadeout);
        if (FOUND == m_records.end()) { return; }
        FOUND->binding_renderer = {};
        FOUND->binding_model = {};
        // A failed association must not make this plane eligible or leak its provisional record.
        if (!m_animations.dying_view_for(FADEOUT) && !m_animations.suppresses(FADEOUT)) {
            m_records.erase(FOUND);
        }
    };
    try {
        m_animations.bind_fadeout(WINDOW_ID, facts, IMAGE);
    } catch (...) {
        FINISH_BINDING();
        throw;
    }
    FINISH_BINDING();
}

bool CAnimationCloseRenderer::has_record_for(
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) const {
    // Lookup returns a value without touching shared view scratch or allocating.
    return std::ranges::any_of(m_records, [&](const Record &record) {
        const auto VIEW = m_animations.dying_view_for(record.fadeout);
        if (!VIEW && !m_animations.suppresses(record.fadeout)) { return false; }
        // Native weak workspaces that expire are global in the host's current scene scope.
        const auto NATIVE = record.native.lock();
        if (NATIVE && !NATIVE->done()) {
            const auto WORKSPACE = NATIVE->workspace().lock();
            return NATIVE->monitor().lock() == monitor &&
                Render::sceneFadeoutPlane(
                    Render::eSceneMode::MONITOR, NATIVE->plane(), NATIVE->source().pinned) == plane &&
                (!WORKSPACE || WORKSPACE == workspace);
        }
        // Retained pixels keep their original workspace authorization; expiration is not global.
        const auto WORKSPACE = record.workspace.lock();
        return VIEW && VIEW->ownership == hyprimate::animation::CloseStart::Owned &&
            record.monitor.lock() == monitor && record.plane == plane &&
            (!record.workspace_bound || (WORKSPACE && WORKSPACE == workspace));
    });
}

bool CAnimationCloseRenderer::render_batch(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) {
    using namespace hyprimate::animation;
    if (!monitor) {
        return true;
    }
    if (WorkspaceCaptureScope::for_context(ctx) == nullptr) {
        const auto CALLBACK = callback_ticket();
        prune_records();
        if (!CALLBACK.valid()) { return true; }
    }
    const auto CALLBACK = callback_ticket();
    const auto MODEL = m_animations.callback_ticket();
    draw_close_batch<BatchItem>(
        [&](auto &&emit) {
            if (CALLBACK.valid() && MODEL.valid()) { collect_batch(ctx, monitor, plane, workspace, emit); }
        },
        [&](const BatchItem &item) {
            if (!CALLBACK.valid() || !MODEL.valid()) { return; }
            try {
                draw_item(ctx, monitor, item);
            } catch (const std::bad_alloc &) {
                // A pass allocation can fail after its backdrop was submitted. Do not replay it,
                // but keep unrelated items visible instead of discarding the rest of the plane.
            }
        });
    return true;
}

template<class Emit>
void CAnimationCloseRenderer::collect_batch(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
    Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace, Emit &&emit) {
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
        const bool ASSOCIATED = FADEOUT_ID != 0 && FOUND != m_records.end() && MATCHES(*FOUND);
        const bool SUPPRESSED = ASSOCIATED && m_animations.suppresses(FADEOUT_ID);
        if (fadeout->done()) {
            continue;
        }
        emit(BatchItem{.fadeout = fadeout, .fadeout_id = ASSOCIATED ? FADEOUT_ID : 0,
            .view = ASSOCIATED ? m_animations.dying_view_for(FADEOUT_ID) : std::nullopt,
            .suppressed = SUPPRESSED, .workspace = workspace, .plane = plane,
            .key = {.z_index = fadeout->zIndex(), .retained = false,
                .identity = reinterpret_cast<std::uintptr_t>(fadeout.get())}});
    }
    for (const auto &record : m_records) {
        const auto VIEW = m_animations.dying_view_for(record.fadeout);
        if (!VIEW || VIEW->ownership != CloseStart::Owned || !MATCHES(record) ||
            !ctx.shouldRenderContent(record.no_screen_share)) {
            continue;
        }
        const auto NATIVE = record.native.lock();
        if (NATIVE && !NATIVE->done()) {
            continue;
        }
        emit(BatchItem{.fadeout = nullptr, .fadeout_id = record.fadeout, .view = VIEW,
            .suppressed = m_animations.suppresses(record.fadeout), .workspace = workspace, .plane = plane,
            .key = {.z_index = record.z_index, .retained = true, .identity = record.fadeout}});
    }
}

void CAnimationCloseRenderer::draw_item(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
    const BatchItem &item) {
    using namespace hyprimate::animation;
    CNativeFadeoutPass native{ctx, monitor};
    // Re-query by identity: a preceding sibling may have removed this association.
    const auto FOUND = m_animations.dying_view_for(item.fadeout_id);
    CloseRenderGuard guard{.renderer = callback_ticket(), .model = m_animations.callback_ticket(),
        .animations = &m_animations, .port = &m_port, .native = item.fadeout,
        .monitor = monitor, .workspace = item.workspace, .plane = item.plane,
        .fadeout = item.fadeout_id, .handle = FOUND ? std::optional{FOUND->handle} : std::nullopt,
        .no_screen_share = FOUND && FOUND->no_screen_share};
    // Do not turn a stale owned item into native content after association removal.
    if (item.view && (!FOUND || FOUND->handle != item.view->handle)) { return; }
    if (FOUND) {
        const auto IMAGE = m_port.fadeout_image(FOUND->handle);
        if (IMAGE && IMAGE->getTexture()) {
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
                .damage = native.damage(),
                .close_guard = guard};
            // Everything borrowed from the renderer is captured before the first submit.
            const auto EFFECTS = item.fadeout ? item.fadeout->effects() : Desktop::SFadeoutRenderEffects{};
            submit_close([&] { return guard.valid(ctx); },
                [&] { native.draw_backdrop(EFFECTS, guard); },
                [&] { Render::IHyprRenderer::addPassElement(ctx,
                    makeUnique<CWindowAnimationPass>(std::move(data))); });
            return;
        }
    }
    if (item.suppressed || m_animations.suppresses(item.fadeout_id) || !item.fadeout) {
        return;
    }
    const auto FRAMEBUFFER = item.fadeout->framebuffer(Render::eSceneMode::MONITOR);
    if (!FRAMEBUFFER || !FRAMEBUFFER->getTexture()) {
        return;
    }
    const auto EFFECTS = item.fadeout->effects();
    guard.handle.reset();
    submit_close([&] { return guard.valid(ctx); },
        [&] { native.draw_backdrop(EFFECTS, guard); },
        [&] { native.draw_texture(*item.fadeout, FRAMEBUFFER->getTexture(), EFFECTS, guard); });
}
