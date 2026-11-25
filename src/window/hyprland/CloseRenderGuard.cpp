#include <ranges>

#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>

#include "AnimationCloseRendererPrivate.hpp"
#include "CloseRenderGuard.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "window/WindowAnimations.hpp"

using namespace hyprimate::animation;
using namespace hyprimate::animation::close_detail;

void CloseRenderGuard::physical_failure(Render::CRenderContext &ctx) const {
    if (public_close_only && handle && renderer.valid() && model.valid() && physical_monitor(ctx, monitor)) {
        animations->interrupt_public_close(*handle);
    }
}

void CloseRenderGuard::physical_presented(Render::CRenderContext &ctx, bool submitted) const {
    if (public_close_only && handle && renderer.valid() && model.valid() && physical_monitor(ctx, monitor)) {
        animations->confirm_public_close_presented(*handle, Clock::now(), submitted);
    }
}

bool CloseRenderGuard::valid(Render::CRenderContext &ctx, bool require_native) const {
    if (!renderer.valid() || !model.valid()) { return false; }
    const auto deny = [&] {
        if (!require_native) { physical_failure(ctx); }
        return false;
    };
    if (public_close_only && (!port->session_unlocked() || !monitor || ctx.m_data.pMonitor.lock() != monitor)) {
        return deny();
    }
    const bool CAPTURE = capture_or_replay(ctx);
    const auto ALLOWED = [&](bool private_content) {
        return ctx.shouldRenderContent(private_content) && (!(public_close_only || CAPTURE) || !private_content);
    };
    if (!ALLOWED(no_screen_share)) { return deny(); }
    const auto ID = native ? port->fadeout_identity(native) : fadeout;
    if (native) {
        const auto SOURCE = native->source();
        const auto WORKSPACE = native->workspace().lock();
        if (!ALLOWED(SOURCE.noScreenShare) || native->monitor().lock() != monitor ||
            (WORKSPACE && WORKSPACE != workspace) ||
            Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, native->plane(), SOURCE.pinned) != plane) {
            return deny();
        }
        if (public_close_only && (WORKSPACE != workspace || !supported_close_effects(native->effects()))) {
            return deny();
        }
    }
    const bool NATIVE_LIVE = native && !native->done() &&
        std::ranges::any_of(Desktop::fadingOutState()->fadeouts(),
            [&](const auto &value) { return value.get() == native.get(); });
    if (require_native && !NATIVE_LIVE) { return false; }
    const bool ALLOWED_ASSOCIATION = close_association_valid(
        *animations, *port, handle, fadeout, ID, NATIVE_LIVE,
        [&](const auto &view) {
            return (view.ownership == CloseStart::Owned || NATIVE_LIVE) &&
                view.monitor == port->monitor_identity(monitor) && ALLOWED(view.no_screen_share) &&
                (!public_close_only || (view.public_close_only && monitor->m_scale == view.scale &&
                    same_extent(monitor->m_transformedSize, view.image_size) &&
                    view.workspace == port->workspace_identity(workspace)));
        });
    return ALLOWED_ASSOCIATION || deny();
}
