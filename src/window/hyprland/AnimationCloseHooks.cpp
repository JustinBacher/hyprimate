#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>

#include "AnimationCloseRenderer.hpp"

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

    // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes window by
    // value.
    SP<Desktop::CWindowFadeout> hook_create(PHLWINDOW window,
        SP<Render::IFramebuffer> snapshot,
        float source_alpha,
        SP<Render::IFramebuffer> workspace_snapshot) {
        auto *self = active_renderer();
        if (self == nullptr) {
            return nullptr;
        }
        const auto CALLBACK = self->callback_ticket();
        const auto PENDING = self->pending_for(window);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the original
        // function as void*.
        auto *original = reinterpret_cast<CreateFn>(self->create_original());
        if (original == nullptr) {
            return nullptr;
        }

        // Never move from hooked by-value parameters: disassembly of the pinned host shows callers
        // keep the argument's control-block pointer in a register and release it after the call,
        // ignoring a moved-from slot.
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host releases both by-value
        // snapshot slots after the call.
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
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes monitor
        // by value.
        PHLMONITOR monitor,
        Desktop::eFadeoutPlane plane,
        // NOLINTNEXTLINE(performance-unnecessary-value-param): Pinned host hook ABI passes
        // workspace by value.
        PHLWORKSPACE workspace,
        Render::eSceneMode mode) {
        auto *active = active_renderer();
        const auto ORIGINAL =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK stores the
            // original function as void*.
            reinterpret_cast<RenderFn>(active ? active->render_original() : nullptr);
        if ((active != nullptr) && mode == Render::eSceneMode::MONITOR &&
            active->has_record_for(monitor, plane, workspace)) {
            try {
                active->render_batch(ctx, monitor, plane, workspace);
            } catch (...) {
                // The plane contains plugin-owned content. Never delegate it wholesale.
            }
            return;
        }

        if (ORIGINAL != nullptr) {
            ORIGINAL(self, ctx, monitor, plane, workspace, mode);
        }
    }
}  // namespace

CAnimationCloseRenderer *CAnimationCloseRenderer::s_active = nullptr;

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
    m_snapshots.reserve(16);
    s_active = this;
    m_create_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void*
        // destination address.
        m_handle,
        create_address,
        reinterpret_cast<const void *>(&hook_create));
    m_render_hook = HyprlandAPI::createFunctionHook(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hook SDK requires a void*
        // destination address.
        m_handle,
        render_address,
        reinterpret_cast<const void *>(&hook_render));
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
    m_snapshots.clear();
    if (s_active == this) {
        s_active = nullptr;
    }
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
