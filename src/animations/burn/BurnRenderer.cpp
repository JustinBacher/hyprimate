#include <algorithm>
#include <exception>
#include <memory>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "HyprAnimationCanvas.hpp"
#include "animations/burn/BurnMaterial.hpp"
#include "animations/burn/BurnRenderer.hpp"
#include "animations/burn/BurnShaders.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"

struct CBurnRenderer::Impl {
    hyprimate::gl::CompositorContextOwner owner;
    std::unique_ptr<CShader> shader;
    CBurnMaterial material;
    bool ready = false;
    GLint maximum_edge = 0;
    bool supports(hyprimate::Vec2 size) {
        if (maximum_edge == 0) {
            GLint texture_limit = 0, renderbuffer_limit = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer_limit);
            maximum_edge =
                std::min({texture_limit, renderbuffer_limit, CBurnMaterial::MAX_TARGET_EDGE});
        }
        return size.x >= 1 && size.y >= 1 && size.x <= maximum_edge && size.y <= maximum_edge &&
               size.x * size.y <= CBurnMaterial::MAX_TARGET_PIXELS;
    }
    bool initialize() {
        if (!owner.ensure_current()) return false;
        if (ready) return true;
        try {
            shader = std::make_unique<CShader>();
            const auto VERTEX = burn_shader::vertex();
            const auto FRAGMENT = burn_shader::fragment();
            if (shader->createProgram(VERTEX, FRAGMENT, true, true) && shader->program() != 0)
                ready = material.initialize(shader->program());
        } catch (const std::exception &) {
            ready = false;
        }
        if (!ready) {
            material.release();
            shader.reset();
        }
        return ready;
    }
};

CBurnRenderer::CBurnRenderer() : m_impl(std::make_unique<Impl>()) {
}
CBurnRenderer::~CBurnRenderer() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_impl->owner};
    if (!CONTEXT) {
        // CShader cannot detach its GL name. Context teardown reclaims the objects;
        // abandon the wrapper rather than run its destructor in a foreign context.
        [[maybe_unused]] const auto *abandoned = m_impl->shader.release();
        return;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<1> BRACKET{{}};
    m_impl->material.release();
    m_impl->shader.reset();
}
bool CBurnRenderer::warm_up() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !m_impl->owner.ensure_current()) return false;
    if (m_impl->ready) return true;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<1> BRACKET{{}};
    return m_impl->initialize();
}

bool CBurnRenderer::draw(const hyprimate::BurnSimulation &simulation,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw) {
    if (!draw.canvas) return false;
    if (!m_impl->owner.ensure_current()) return false;
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
    if (!m_impl->supports({draw.canvas_box.w, draw.canvas_box.h})) return false;
    const auto LAYER =
        hyprimate::gl::get_work_buffer(ctx, Vector2D{draw.canvas_box.w, draw.canvas_box.h});
    if (!LAYER || !LAYER->getTexture() || LAYER->getTexture()->m_texID == IMAGE->m_texID)
        return false;
    // Work buffers are allocated by Hyprland's GL backend.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    const auto FRAMEBUFFER = static_cast<Render::GL::CGLFramebuffer *>(LAYER.get())->getFBID();
    const hyprimate::gl::CRawGlBracket<1> BRACKET{
        {.viewport_width = static_cast<GLsizei>(draw.canvas_box.w),
            .viewport_height = static_cast<GLsizei>(draw.canvas_box.h),
            .framebuffer = FRAMEBUFFER}};
    if (!m_impl->initialize()) return false;
    const SBurnTarget TARGET{.canvas = {draw.canvas_box.w, draw.canvas_box.h},
        .origin = {draw.canvas_box.x, draw.canvas_box.y},
        .source = draw.source,
        .texture_origin = draw.image_origin,
        .texture_size = draw.image_size,
        .alpha = 1,
        .output_top_first = true};
    if (!m_impl->material.draw(simulation, TARGET, IMAGE->m_texID, FRAMEBUFFER)) return false;
    draw.canvas->layer() = LAYER;
    return true;
}
