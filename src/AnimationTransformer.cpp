#include "AnimationTransformer.hpp"

#include <cmath>
#include <utility>

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "HyprAnimationCanvas.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimations.hpp"

using namespace Render;
using namespace hyprimate::animation;

CAnimationTransformer::CAnimationTransformer(
    PHLWINDOWREF window, HyprWindowAnimations &animations, CHyprWindowPort &port, Handle handle)
    : m_window(std::move(window)), m_animations(animations), m_port(port), m_handle(handle) {}

bool CAnimationTransformer::active() const {
    return !m_detached && m_window.lock() && m_animations.active(m_handle);
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
        canvas.layer() = g_pHyprRenderer->getWorkBuffer(ctx, CANVAS.size());
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
