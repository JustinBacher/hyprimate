#include <cmath>
#include <memory>
#include <new>
#include <utility>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "HyprAnimationCanvas.hpp"
#include "WindowAnimationFacts.hpp"
#include "animations/paper/PaperMaterial.hpp"
#include "animations/paper/PaperRenderer.hpp"
#include "animations/paper/PaperShaders.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"

struct CPaperRenderer::Impl {
    hyprimate::gl::CompositorContextOwner context;
    std::unique_ptr<CShader> shader;
    CPaperMaterial material;
    bool ready = false;
    bool initialize() try {
        if (ready) return true;
        auto candidate = std::make_unique<CShader>();
        if (!candidate->createProgram(
                paper_shader::VERTEX.data(), paper_shader::FRAGMENT.data(), true, true) ||
            candidate->program() == 0 || !material.initialize(candidate->program()))
            return false;
        shader = std::move(candidate);
        ready = true;
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
};

CPaperRenderer::CPaperRenderer() : m_impl(std::make_unique<Impl>()) {
}
CPaperRenderer::~CPaperRenderer() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_impl->context};
    if (!CONTEXT) {
        // CShader deletes its program in its destructor, which is unsafe on another context.
        [[maybe_unused]] const auto *shader = m_impl->shader.release();
        return;
    }
    const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
    // Material framebuffer names never leave the material. Every operation that binds them
    // runs inside this adapter's raw bracket, so no host binding guard can have saved one.
    // The bracket also restores ambient bindings around deletion and CShader destruction.
    m_impl->material.release();
    m_impl->shader.reset();
}

bool CPaperRenderer::warm_up() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !m_impl->context.ensure_current()) return false;
    if (m_impl->ready) return true;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
    return m_impl->initialize();
}

bool CPaperRenderer::draw(const hyprimate::PaperSurface &surface,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw) {
    if (!draw.canvas) return false;
    draw.canvas->layer().reset();
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !draw.image || !*draw.image ||
        !std::isfinite(draw.scale) || !(draw.scale > 0) || !hyprimate::is_usable(draw.source) ||
        !hyprimate::is_usable(draw.canvas_box) || !std::isfinite(draw.canvas_box.w) ||
        !std::isfinite(draw.canvas_box.h) || draw.canvas_box.w < 1 || draw.canvas_box.h < 1 ||
        draw.canvas_box.w > 32768 || draw.canvas_box.h > 32768)
        return false;
    const auto IMAGE = (*draw.image)->getTexture();
    if (!IMAGE || IMAGE->m_texID == 0 ||
        (IMAGE->m_type != Render::TEXTURE_RGBA && IMAGE->m_type != Render::TEXTURE_RGBX))
        return false;
    if (!m_impl->context.ensure_current()) return false;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    auto &ctx = draw.canvas->context();
    const auto STATE = ctx.saveDrawState();
    const auto LAYER =
        hyprimate::gl::get_work_buffer(ctx, Vector2D{draw.canvas_box.w, draw.canvas_box.h});
    if (!LAYER || !LAYER->getTexture() || LAYER->getTexture()->m_texID == IMAGE->m_texID)
        return false;
    // Work buffers belong to Hyprland's GL backend.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    const auto FRAMEBUFFER = static_cast<Render::GL::CGLFramebuffer *>(LAYER.get())->getFBID();
    const hyprimate::gl::CRawGlBracket<2> BRACKET{
        {.viewport_width = static_cast<GLsizei>(draw.canvas_box.w),
            .viewport_height = static_cast<GLsizei>(draw.canvas_box.h),
            .framebuffer = FRAMEBUFFER}};
    if (!m_impl->initialize()) return false;
    const SPaperTarget TARGET{.canvas = {draw.canvas_box.w, draw.canvas_box.h},
        .origin = {draw.canvas_box.x, draw.canvas_box.y},
        .source = draw.source,
        .texture_origin = draw.image_origin,
        .texture_size = draw.image_size,
        .alpha = 1,
        .output_top_first = true};
    if (!m_impl->material.draw(surface, TARGET, IMAGE->m_texID, FRAMEBUFFER)) return false;
    draw.canvas->layer() = LAYER;
    return true;
}
