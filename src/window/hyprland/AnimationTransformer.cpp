#include <cmath>
#include <format>
#include <utility>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/protocols/core/Subcompositor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>

#include "AnimationTransformer.hpp"
#include "HyprAnimationCanvas.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "window/WindowAnimations.hpp"
#include "perf/GpuTimer.hpp"
#include "perf/Diagnostics.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

using namespace Render;
using namespace hyprimate::animation;

namespace {
bool construction_source_fits(const Desktop::View::CWindow &window) {
    // Output capture masks the native footprint. Recheck before rendering, not just at
    // acquisition: protection can change between animation ticks.
    if (!window.m_ruleApplicator || window.m_ruleApplicator->noScreenShare().valueOrDefault()) {
        return false;
    }
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

    return true;
}
}  // namespace

// Only the actual nested window pass executes this marker. Hyprland's second
// transformer invocation renders a blur rectangle into a different framebuffer.
// Framebuffer identity distinguishes the two without reading client pixels or
// depending on call order, and remains valid through preceding transformers.
class CAnimationTransformer::CContentSourcePass final : public IPassElement {
  public:
    CContentSourcePass(ContentSource &source, CRenderPass &pass, CBox blur_box,
        double rounding, double rounding_power, CContentSourcePass *parent = nullptr,
        const std::string *outer_name = nullptr)
        : m_source(source), m_pass(pass), m_blur_box(blur_box), m_rounding(rounding),
          m_rounding_power(rounding_power), m_next(source.passes), m_peer(parent),
          m_outer_name(outer_name) {
        if (parent) { parent->m_peer = this; }
        if (m_next) { m_next->m_previous = this; }
        source.passes = this;
    }
    ~CContentSourcePass() override {
        if (m_peer) { m_peer->m_peer = nullptr; }
        if (m_source.underlay_pass == this) { m_source.underlay_pass = nullptr; }
        if (m_source.receipt == this) {
            m_source.receipt = nullptr;
            m_source.context = nullptr;
            m_source.framebuffer.reset();
            m_source.shader_matte.reset();
        }
        if (m_previous) { m_previous->m_next = m_next; }
        else { m_source.passes = m_next; }
        if (m_next) { m_next->m_previous = m_previous; }
    }
    CContentSourcePass(const CContentSourcePass &) = delete;
    CContentSourcePass &operator=(const CContentSourcePass &) = delete;
    CContentSourcePass(CContentSourcePass &&) = delete;
    CContentSourcePass &operator=(CContentSourcePass &&) = delete;
    std::vector<UP<IPassElement>> draw(CRenderContext &ctx) override {
        if (m_outer_name) {
            m_target = ctx.m_data.currentFB;
            m_armed_context = &ctx;
            m_source.underlay_pass = m_peer ? this : nullptr;
            return {};
        }
        m_source.includes_underlay = false;
        if (m_peer && m_peer->m_armed_context == &ctx && m_source.underlay_pass == m_peer) {
            m_source.underlay_pass = nullptr;
            if (const auto image = std::exchange(m_peer->m_underlay, {})) {
                // This is the first draw into the window source. Use the host's
                // source translation and clip, before any decorations or client pixels.
                GL::CHyprOpenGLImpl::STextureRenderData data{};
                data.allowDim = false;
                GL::g_pHyprOpenGL->renderTexture(ctx, image->getTexture(), m_peer->m_blur_box, data);
                m_source.includes_underlay = true;
            }
        }
        m_source.framebuffer = ctx.m_data.currentFB;
        m_source.context = &ctx;
        m_source.receipt = this;
        m_source.shader_matte.reset();
        m_source.blur_box = m_blur_box;
        m_source.rounding = m_rounding;
        m_source.rounding_power = m_rounding_power;
        return {};
    }
    bool needsLiveBlur(CRenderContext &) override { return false; }
    bool needsPrecomputeBlur(CRenderContext &) override { return false; }
    bool undiscardable(CRenderContext &) override { return true; }
    bool disableSimplification(CRenderContext &) override { return !m_outer_name; }
    ePassElementType type() override { return EK_CUSTOM; }
    const char *passName() override {
        return m_outer_name ? m_outer_name->c_str() : nested_name().c_str();
    }
    void remove() { m_pass.removeAllOfType(m_outer_name ? *m_outer_name : nested_name()); }

    std::optional<UnderlayTarget> underlay_target(CRenderContext &ctx, const SP<IFramebuffer> &target) {
        if (!m_peer || m_armed_context != &ctx || !target ||
            m_target.lock() != target || ctx.m_data.currentFB != target) {
            return std::nullopt;
        }
        if (!m_underlay) {
            m_underlay = hyprimate::gl::get_work_buffer(ctx, m_blur_box.size());
            if (!m_underlay) { return std::nullopt; }
            const GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
            const auto state = ctx.saveDrawState();
            g_pHyprRenderer->bindFB(ctx, m_underlay);
            ctx.m_data.fbSize = m_blur_box.size();
            ctx.m_data.damage = CRegion{0, 0, m_blur_box.w, m_blur_box.h};
            g_pHyprRenderer->setProjectionType(ctx, RPT_EXPORT);
            g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
        }
        return UnderlayTarget{m_underlay, m_blur_box};
    }

  private:
    static const std::string &nested_name() {
        static const std::string name = "hyprimate-window-source";
        return name;
    }
    ContentSource &m_source;
    CRenderPass &m_pass;
    const CBox m_blur_box;
    const double m_rounding, m_rounding_power;
    CContentSourcePass *m_next = nullptr, *m_previous = nullptr;
    CContentSourcePass *m_peer;
    const std::string *m_outer_name;
    CRenderContext *m_armed_context = nullptr;
    WP<IFramebuffer> m_target;
    SP<IFramebuffer> m_underlay;
};

CAnimationTransformer::CAnimationTransformer(
    PHLWINDOWREF window, HyprWindowAnimations &animations, CHyprWindowPort &port, Handle handle)
    : m_window(std::move(window)), m_animations(animations), m_port(port), m_handle(handle) {
}

CAnimationTransformer::~CAnimationTransformer() {
    // Nested passes are host-owned and may outlive detachment. Remove their
    // plugin vtables before the transformer (or the plugin library) is destroyed.
    while (m_content_source.passes) { m_content_source.passes->remove(); }
}

void CAnimationTransformer::prepare_window_underlay(const PHLWINDOW &window, CRenderContext &ctx) {
    if (ctx.m_mode != RENDER_MODE_NORMAL || &IHyprRenderer::currentPass(ctx) != &ctx.m_pass ||
        ctx.m_renderingSnapshot || ctx.m_renderingCapture || ctx.readOnlyEffects() ||
        WorkspaceCaptureScope::for_context(ctx) || WorkspaceCaptureScope::is_destination_live(ctx)) {
        return;
    }
    // Effect packages may sample client pixels, not paint derived from the desktop.
    if (auto *transformer = window->effects().transformers()->get<CAnimationTransformer>();
        transformer && transformer->m_handle.style != hyprimate::config::Style::Shader) {
        transformer->m_underlay_context = &ctx;
    }
}

std::optional<CAnimationTransformer::UnderlayTarget> CAnimationTransformer::window_underlay_target(
    CRenderContext &ctx, const SP<IFramebuffer> &target) {
    auto *pass = m_content_source.underlay_pass;
    const CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources()};
    if (!pass || !active() || !m_port.session_unlocked() ||
        !m_animations.allows_draw(canvas) || canvas.captures_pixels() ||
        CHyprAnimationCanvas::exports_output(ctx) ||
        (ctx.m_data.renderModif.enabled && !ctx.m_data.renderModif.modifs.empty())) {
        return std::nullopt;
    }
    return pass->underlay_target(ctx, target);
}

void CAnimationTransformer::preWindowRender(CRenderContext &ctx, CSurfacePassElement::SRenderData *data) {
    const bool underlay = std::exchange(m_underlay_context, nullptr) == &ctx;
    if (!m_animations.allows_draw(CHyprAnimationCanvas{ctx, m_port.canvas_resources()})) {
        return;
    }
    const bool needs_receipt = m_handle.style == hyprimate::config::Style::Construction ||
        m_handle.style == hyprimate::config::Style::Shader;
    auto &pass = IHyprRenderer::currentPass(ctx);
    UP<CContentSourcePass> parent;
    // The outer marker arms one presentation, not the window for the whole frame.
    // The nested marker consumes only its own peer, even if a draw was culled.
    if (underlay && ctx.m_mode == RENDER_MODE_NORMAL &&
        !CHyprAnimationCanvas{ctx, m_port.canvas_resources()}.captures_pixels() &&
        &pass != &ctx.m_pass && data && data->decorate && data->pWindow && data->pMonitor) {
        const auto &monitor = data->pMonitor;
        const auto &presentation = data->workspacePresentation;
        auto box = data->pWindow->getFullWindowBoundingBox();
        box.translate(presentation.workspaceOffset + presentation.floatingOffset - monitor->m_position);
        const double scale = monitor->m_scale;
        const auto raster = std::isfinite(scale) && scale > 0 ? pixelBoxForLogical(box, scale) : CBox{};
        if (!raster.empty()) {
            if (m_underlay_pass_name.empty()) {
                m_underlay_pass_name = std::format("hyprimate-window-underlay-{}", static_cast<const void *>(this));
            }
            parent = makeUnique<CContentSourcePass>(
                m_content_source, ctx.m_pass, raster, 0, 2, nullptr, &m_underlay_pass_name);
        }
    }
    if (!needs_receipt && !parent) { return; }
    CBox blur_box;
    if (m_handle.style == hyprimate::config::Style::Shader && data && data->pWindow &&
        data->pMonitor && data->pWindow->shouldBlur(data->workspacePresentation)) {
        const auto position = data->pos - data->pMonitor->m_position;
        blur_box = CBox{position.x, position.y, data->w, data->h}.scale(data->pMonitor->m_scale).round();
    }
    IHyprRenderer::addPassElement(ctx, makeUnique<CContentSourcePass>(m_content_source, pass,
        blur_box, !data || data->dontRound ? 0 : std::max(data->rounding - 1, 0),
        data ? data->roundingPower : 2, parent.get()));
    if (parent) { ctx.m_pass.add(std::move(parent)); }
}

bool CAnimationTransformer::active() const {
    if (m_detached) {
        return false;
    }
    const auto WINDOW = m_window.lock();
    const auto frame = m_animations.view(m_handle);
    return WINDOW && frame && frame->running && !frame->native_presentation &&
        (m_handle.style != hyprimate::config::Style::Construction || construction_source_fits(*WINDOW));
}

int CAnimationTransformer::priority() const {
    return 10;
}
bool CAnimationTransformer::blocksDirectScanout() const {
    return active();
}

CBox CAnimationTransformer::transformedExtents(const CBox &current_box) const {
    return active() ? to_cbox(m_animations.planned_extents(m_handle, to_box(current_box))) : current_box;
}

CBox CAnimationTransformer::sourceBoxForOutput(
    const CBox &output_box, const CBox &input_box) const {
    return active() ? input_box : output_box.intersection(input_box);
}

SWindowTransformBuffer CAnimationTransformer::transform(CRenderContext &ctx,
    const SWindowTransformBuffer &input,
    const SWindowTransformContext &context) {
    if (!active() || context.standalone || context.renderingSnapshot || !context.monitor ||
        !input.framebuffer || !input.framebuffer->getTexture()) {
        return input;
    }
    if (!m_animations.allows_draw(CHyprAnimationCanvas{ctx, m_port.canvas_resources()})) {
        return input;
    }
    if (m_handle.style == hyprimate::config::Style::Shader) {
        const auto window = m_window.lock();
        if (!m_port.session_unlocked() || !window || !window->mapped() ||
            !window->m_ruleApplicator ||
            window->m_ruleApplicator->noScreenShare().valueOrDefault()) {
            return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
        }
    }
    if (m_handle.style == hyprimate::config::Style::Shader &&
        input.framebuffer != ctx.m_data.currentFB) {
        // Only expose the host's isolated window raster, never an unverified
        // output supplied by a preceding transformer.
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    const double SCALE = context.monitor->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0 || context.currentBox.empty()) {
        return input;
    }
    const CBox CANVAS = pixelBoxForLogical(context.outputBox.copy().expand(1.0 / SCALE), SCALE);
    const CBox SOURCE = context.currentBox.copy().scale(SCALE);
    const CBox OUTPUT = to_cbox(m_animations.extents(m_handle, to_box(context.currentBox))).scale(SCALE);
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
    const bool CONTENT_RECEIPT = m_handle.style == hyprimate::config::Style::Construction ||
                                 m_handle.style == hyprimate::config::Style::Shader;
    const bool BLUR_MASK = CONTENT_RECEIPT &&
        (m_content_source.context != &ctx ||
            m_content_source.framebuffer.lock() != ctx.m_data.currentFB);
    const bool SHADER = m_handle.style == hyprimate::config::Style::Shader;
    if (SHADER && BLUR_MASK) {
        auto matte = std::exchange(m_content_source.shader_matte, {});
        if (matte && m_content_source.context == &ctx && m_content_source.matte_result_box == CANVAS) {
            return {.framebuffer = std::move(matte), .box = CANVAS};
        }
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources(), BLUR_MASK};
    const hyprimate::perf::GpuScope GPU_PROFILE{profile_key(m_handle.style)};
    HYPRIMATE_GPU_STOPWATCH("style.outer");
    const Draw<CHyprAnimationCanvas> DRAW{.canvas = &canvas,
        .image = &input.framebuffer,
        .source = to_box(SOURCE),
        .output = to_box(OUTPUT),
        .canvas_box = to_box(CANVAS),
        .image_origin = {.x = input.box.x, .y = input.box.y},
        .image_size = {.x = SIZE.x, .y = SIZE.y},
        .scale = SCALE,
        .real_pass = !BLUR_MASK,
        .monitor = m_port.monitor_identity(context.monitor.lock()),
        .includes_desktop_pixels = m_content_source.includes_underlay &&
            m_content_source.context == &ctx && m_content_source.framebuffer.lock() == ctx.m_data.currentFB};
    if (!m_animations.draw(m_handle, DRAW)) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    const bool TRANSFORMED = canvas.layer() != nullptr;
    if (!canvas.layer()) {
        canvas.layer() = hyprimate::gl::get_work_buffer(ctx, CANVAS.size());
        if (!canvas.layer()) {
            return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const auto STATE = ctx.saveDrawState();
        HYPRIMATE_GPU_STOPWATCH("transform.work-buffer-clear");
        g_pHyprRenderer->bindFB(ctx, canvas.layer());
        ctx.m_data.damage = CRegion{0.0, 0.0, CANVAS.w, CANVAS.h};
        ctx.m_data.fbSize = CANVAS.size();
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    }
    // Stock 0.57 publishes color before calling the transformer on its matte.
    // Stage our own matching native geometry now: any allocation or shader error
    // returns *both* original rasters instead of publishing a blur-less silhouette.
    if (SHADER && !m_content_source.blur_box.empty()) {
        CHyprAnimationCanvas matte{ctx, m_port.canvas_resources(), true,
            m_content_source.rounding, m_content_source.rounding_power};
        matte.set_blur_box(m_content_source.blur_box);
        auto mask_draw = DRAW;
        mask_draw.canvas = &matte;
        mask_draw.real_pass = false;
        if (!m_animations.draw(m_handle, mask_draw) || !matte.layer()) {
            return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
        }
        m_content_source.shader_matte = std::move(matte.layer());
        m_content_source.matte_result_box = CANVAS;
    }
    if (TRANSFORMED && !BLUR_MASK && !canvas.captures_pixels()) {
        m_animations.profile_transformed(m_handle, DRAW.monitor);
    }
    return {.framebuffer = canvas.layer(), .box = CANVAS};
}
