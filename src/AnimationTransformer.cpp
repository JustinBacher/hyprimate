#include "AnimationTransformer.hpp"

#include <cmath>
#include <utility>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/protocols/core/Subcompositor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "HyprAnimationCanvas.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimations.hpp"
#include "gl/NativeFramebuffer.hpp"

using namespace Render;
using namespace hyprimate::animation;

namespace {
bool construction_source_fits(const Desktop::View::CWindow &window) {
    const auto SURFACE = window.wlSurface();
    const auto ROOT = SURFACE ? SURFACE->resource() : nullptr;
    if (!ROOT) {
        return false;
    }

    // Stock 0.57 flattens the main pass into getFullWindowBoundingBox(), which
    // does not account for protruding wl_subsurfaces. After flattening, even
    // returning the input cannot recover pixels the host clipped away.
    const auto &children = ROOT->m_subsurfaces;
    if (!children.empty()) {
        // Prove the common one-leaf case without enumerating or caching a client tree.
        if (children.size() != 1) {
            return false;
        }
        const auto CHILD = children.front().lock();
        const auto LEAF = CHILD ? CHILD->m_surface.lock() : nullptr;
        // The native window surface pass clamps the far edge to the body
        // (squishOversized), but does not clamp a negative subsurface offset.
        if (LEAF && (!LEAF->m_subsurfaces.empty() ||
            CHILD->m_position.x < 0 || CHILD->m_position.y < 0)) {
            return false;
        }
    }

    // A blur-eligible window is replayed through the same transformer with a
    // white-on-black mask and identical public context. Decline before the host
    // disables native surface blur rather than mistaking a mask for client art.
    // The monitor presentation is conservative: isolated workspace draws only
    // reset workspace alpha to unity, which cannot make shouldBlur() stricter.
    return !window.shouldBlur();
}
}  // namespace

CAnimationTransformer::CAnimationTransformer(
    PHLWINDOWREF window, HyprWindowAnimations &animations, CHyprWindowPort &port, Handle handle)
    : m_window(std::move(window)), m_animations(animations), m_port(port), m_handle(handle) {}

bool CAnimationTransformer::active() const {
    if (m_detached) {
        return false;
    }
    const auto WINDOW = m_window.lock();
    return WINDOW && m_animations.active(m_handle) &&
        (m_handle.style != hyprimate::config::Style::Construction || construction_source_fits(*WINDOW));
}

int CAnimationTransformer::priority() const { return 10; }
bool CAnimationTransformer::blocksDirectScanout() const { return active(); }

CBox CAnimationTransformer::transformedExtents(const CBox &current_box) const {
    return active() ? to_cbox(m_animations.extents(m_handle, to_box(current_box))) : current_box;
}

CBox CAnimationTransformer::sourceBoxForOutput(const CBox &output_box, const CBox &input_box) const {
    return active() ? input_box : output_box.intersection(input_box);
}

SWindowTransformBuffer CAnimationTransformer::transform(CRenderContext &ctx,
    const SWindowTransformBuffer &input, const SWindowTransformContext &context) {
    if (!active() || context.standalone || context.renderingSnapshot || !context.monitor ||
        !input.framebuffer || !input.framebuffer->getTexture()) {
        return input;
    }
    const double SCALE = context.monitor->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0 || context.currentBox.empty()) {
        return input;
    }
    const CBox CANVAS = pixelBoxForLogical(context.outputBox.copy().expand(1.0 / SCALE), SCALE);
    const CBox SOURCE = context.currentBox.copy().scale(SCALE);
    const CBox OUTPUT = transformedExtents(context.currentBox).scale(SCALE);
    const auto SIZE = input.framebuffer->getTexture()->m_size;
    if (m_handle.style == hyprimate::config::Style::Construction &&
        (context.inputBox != context.currentBox || input.box.size() != SIZE)) {
        // Only sample the declared paint domain of this exact raster lease,
        // not rounded canvas padding or an incompatible preceding stage.
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    if (CANVAS.empty() || SOURCE.empty() || SIZE.x <= 0 || SIZE.y <= 0) {
        return input;
    }
    CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources()};
    const Draw<CHyprAnimationCanvas> DRAW{.canvas = &canvas, .image = &input.framebuffer,
        .source = to_box(SOURCE), .output = to_box(OUTPUT), .canvas_box = to_box(CANVAS),
        .image_origin = {.x = input.box.x, .y = input.box.y},
        .image_size = {.x = SIZE.x, .y = SIZE.y}, .scale = SCALE, .real_pass = true,
        .monitor = m_port.monitor_identity(context.monitor.lock())};
    if (!m_animations.draw(m_handle, DRAW)) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    if (!canvas.layer()) {
        canvas.layer() = hyprimate::gl::get_work_buffer(ctx, CANVAS.size());
        if (!canvas.layer()) {
            return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const auto STATE = ctx.saveDrawState();
        g_pHyprRenderer->bindFB(ctx, canvas.layer());
        ctx.m_data.damage = CRegion{0.0, 0.0, CANVAS.w, CANVAS.h};
        ctx.m_data.fbSize = CANVAS.size();
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    }
    return {.framebuffer = canvas.layer(), .box = CANVAS};
}
