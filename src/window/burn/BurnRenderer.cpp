#include <exception>
#include <memory>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"
#include "window/burn/BurnMaterial.hpp"
#include "window/burn/BurnRenderer.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"

namespace {
    void report_shader_error(const char *label, const char *stage, const char *message) noexcept {
        try {
            LOG(Log::ERR, "[hyprimate] {} {}: {}", label, stage, message);
        } catch (const std::exception &) {
            // Diagnostic allocation must not prevent shader cleanup or retry.
        }
    }
}

struct CBurnRenderer::Impl {
    hyprimate::gl::CompositorContextOwner owner;
    CBurnResources resources;

    bool admit(const hyprimate::gl::CurrentCompositorContext &current) noexcept {
        const auto STATE = owner.classify_current(current);
        if (!resources.accept(STATE)) return false;
        if (STATE == hyprimate::gl::ContextOwnerState::replaced) owner.reset();
        return owner.ensure_current(current);
    }
};

CBurnRenderer::CBurnRenderer() : m_impl(std::make_unique<Impl>()) {
}
CBurnRenderer::~CBurnRenderer() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_impl->owner};
    if (!CONTEXT) {
        m_impl->resources.abandon();
        return;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<1> BRACKET{{}};
    m_impl->resources.release();
}
bool CBurnRenderer::warm_up() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT) return false;
    const hyprimate::gl::CurrentCompositorContext CURRENT_CONTEXT{};
    if (!m_impl->admit(CURRENT_CONTEXT)) return false;
    if (m_impl->resources.program()) return true;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<1> BRACKET{{}};
    return m_impl->resources.initialize(report_shader_error);
}

bool CBurnRenderer::draw(const hyprimate::BurnSimulation &simulation,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw) {
    if (!draw.canvas) return false;
    const hyprimate::gl::CurrentCompositorContext CURRENT_CONTEXT{};
    if (!m_impl->admit(CURRENT_CONTEXT)) return false;
    draw.canvas->layer().reset();
    if (!simulation.initialized() || !g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
        !draw.image || !*draw.image || !(draw.scale > 0) || !hyprimate::is_usable(draw.source) ||
        !hyprimate::is_usable(draw.canvas_box))
        return false;
    // The only image input is the framework's retained framebuffer. No window,
    // surface, or client is dereferenced, including after unmap/destruction.
    const auto IMAGE = (*draw.image)->getTexture();
    if (!IMAGE || IMAGE->m_texID == 0 ||
        (IMAGE->m_type != Render::TEXTURE_RGBA && IMAGE->m_type != Render::TEXTURE_RGBX))
        return false;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    auto &ctx = draw.canvas->context();
    const auto STATE = ctx.saveDrawState();
    // Reject oversized debris bounds before Hyprland allocates the work buffer
    // or any dimension is converted to the backend's integer representation.
    if (!m_impl->resources.supports({draw.canvas_box.w, draw.canvas_box.h})) return false;
    const auto LAYER = hyprimate::gl::get_work_buffer(
        ctx, Vector2D{draw.canvas_box.w, draw.canvas_box.h}, CURRENT_CONTEXT);
    if (!LAYER || !LAYER->getTexture() || LAYER->getTexture()->m_texID == IMAGE->m_texID)
        return false;
    // Work buffers are allocated by Hyprland's GL backend.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    const auto FRAMEBUFFER = static_cast<Render::GL::CGLFramebuffer *>(LAYER.get())->getFBID();
    const hyprimate::gl::CRawGlBracket<1> BRACKET{
        {.viewport_width = static_cast<GLsizei>(draw.canvas_box.w),
            .viewport_height = static_cast<GLsizei>(draw.canvas_box.h),
            .framebuffer = FRAMEBUFFER}};
    if (!m_impl->resources.initialize(report_shader_error)) return false;
    const SBurnTarget TARGET{.canvas = {draw.canvas_box.w, draw.canvas_box.h},
        .origin = {draw.canvas_box.x, draw.canvas_box.y},
        .source = draw.source,
        .texture_origin = draw.image_origin,
        .texture_size = draw.image_size,
        .alpha = 1,
        .output_top_first = true};
    if (!m_impl->resources.material().draw(simulation, TARGET, IMAGE->m_texID, FRAMEBUFFER)) return false;
    draw.canvas->layer() = LAYER;
    return true;
}
