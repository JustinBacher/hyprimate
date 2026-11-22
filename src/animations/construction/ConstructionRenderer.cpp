#include "animations/construction/ConstructionRenderer.hpp"

#include <exception>
#include <memory>
#include <thread>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "HyprAnimationCanvas.hpp"
#include "animations/construction/ConstructionMaterial.hpp"
#include "animations/construction/ConstructionProgram.hpp"
#include "animations/construction/ConstructionShaders.hpp"
#include "animations/construction/ConstructionSite.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"

namespace {

bool supports_scene_color(const Render::ITexture &image) {
    // The PNG and solid colors are authored for an SDR sRGB palette. Do not apply a second
    // conversion to the already scene-encoded client. If we cannot also composite the art in
    // that encoding, let Hyprland present this draw natively (including HDR/ICC scenes).
    using namespace NColorManagement;
    // The compositor may uniquely own its description: inspect the render-thread
    // borrow synchronously; promoting that weak view to a shared owner aborts.
    const auto &description = image.m_imageDescription;
    if (description.expired()) return false;
    const auto &value = description->value();
    const bool SRGB = value.primariesNameSet ? value.primariesNamed == CM_PRIMARIES_SRGB :
        value.primaries == getPrimaries(CM_PRIMARIES_SRGB);
    return SRGB && !value.icc.present &&
        (value.transferFunction == CM_TRANSFER_FUNCTION_SRGB ||
            value.transferFunction == CM_TRANSFER_FUNCTION_GAMMA22);
}

}  // namespace

struct CConstructionRenderer::Impl {
    hyprimate::gl::CompositorContextOwner owner;
    GLuint program = 0;
    hyprimate::CConstructionMaterial material;
    const std::thread::id render_thread = std::this_thread::get_id();
    bool drawing = false;

    bool initialize() noexcept {
        if (program == 0) {
            using namespace hyprimate::construction_shader;
            // The material owns its VAO and uniform bindings. CShader cannot adopt this
            // program and its dynamic compiler leaks GL names on compile/link failures.
            program = build_program(VERTEX.data(), FRAGMENT.data());
        }
        // Retain a linked program through transient material allocation failure; the material
        // releases every partially acquired object before returning false, and retries lazily.
        return material.initialize(program);
    }
};

CConstructionRenderer::CConstructionRenderer() : m_impl(std::make_unique<Impl>()) {}

CConstructionRenderer::~CConstructionRenderer() {
    if (m_impl->program == 0) {
        return;  // No material objects are created without a linked program.
    }
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_impl->owner};
    if (!CONTEXT) {
        // Leave names to EGL teardown; never delete them from an unrelated context.
        return;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
    m_impl->material.release();
    glDeleteProgram(m_impl->program);
}

bool CConstructionRenderer::draw(const hyprimate::ConstructionSite &site,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw) try {
    using namespace hyprimate;
    if (!draw.canvas || m_impl->render_thread != std::this_thread::get_id() || m_impl->drawing)
        return false;
    m_impl->drawing = true;
    struct DrawGuard {
        bool &drawing;
        ~DrawGuard() { drawing = false; }
    } guard{m_impl->drawing};

    draw.canvas->layer().reset();
    const auto &batch = site.batch();
    if (!site.active() || batch.overflow || batch.count > batch.quads.size() || !g_pHyprRenderer ||
        g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !draw.image || !*draw.image)
        return false;
    // The framework authorizes this borrow independently for every physical, capture and matte
    // replay. We never infer authorization from pass kind or retain the image or output lease.
    const auto IMAGE = (*draw.image)->getTexture();
    if (!IMAGE || !IMAGE->m_texID ||
        (IMAGE->m_type != Render::TEXTURE_RGBA && IMAGE->m_type != Render::TEXTURE_RGBX) ||
        IMAGE->m_transform != HYPRUTILS_TRANSFORM_NORMAL || IMAGE->m_size.x != draw.image_size.x ||
        IMAGE->m_size.y != draw.image_size.y || !supports_scene_color(*IMAGE))
        return false;

    const auto VIEW = CConstructionMaterial::prepare_view({.canvas = draw.canvas_box,
        .source = draw.source, .body = site.body(), .capture = site.capture(),
        .image_origin = draw.image_origin, .image_size = draw.image_size, .scale = draw.scale,
        .image_rgbx = IMAGE->m_type == Render::TEXTURE_RGBX});
    if (!VIEW || !m_impl->owner.ensure_current() || !m_impl->material.supports_view(*VIEW))
        return false;

    auto &ctx = draw.canvas->context();
    if (!ctx.m_data.pMonitor) return false;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    // The pinned host can refuse a lease, but a failed GL storage allocation can
    // SIGABRT in CGLFramebuffer::internalAlloc before a lease can be returned.
    const auto LAYER = gl::get_work_buffer(ctx, Vector2D{VIEW->width, VIEW->height});
    if (!LAYER || LAYER == *draw.image || LAYER == ctx.m_data.currentFB || LAYER->getMirrorTexture() ||
        !LAYER->isAllocated() || LAYER->m_drmFormat != (*draw.image)->m_drmFormat)
        return false;
    const auto TEXTURE = LAYER->getTexture();
    const auto CURRENT = ctx.m_data.currentFB ? ctx.m_data.currentFB->getTexture() : nullptr;
    if (!TEXTURE || !TEXTURE->m_texID || TEXTURE->m_texID == IMAGE->m_texID ||
        (CURRENT && TEXTURE->m_texID == CURRENT->m_texID) || TEXTURE->m_type != Render::TEXTURE_RGBA ||
        TEXTURE->m_size.x != VIEW->width || TEXTURE->m_size.y != VIEW->height ||
        TEXTURE->m_imageDescription != IMAGE->m_imageDescription)
        return false;

    // RT_GL and get_work_buffer() select the native GL framebuffer backend.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    const GLuint FRAMEBUFFER = static_cast<Render::GL::CGLFramebuffer *>(LAYER.get())->getFBID();
    if (!FRAMEBUFFER) return false;
    const gl::CRawGlBracket<2> BRACKET{{.viewport_width = VIEW->width,
        .viewport_height = VIEW->height, .framebuffer = FRAMEBUFFER}};
    if (!m_impl->initialize() ||
        !m_impl->material.draw(batch, *VIEW, IMAGE->m_texID, TEXTURE->m_texID))
        return false;

    draw.canvas->layer() = LAYER;
    return true;
} catch (const std::exception &) {
    // Unwind recoverable allocation exceptions. No layer is published unless submission
    // finishes successfully.
    return false;
}
