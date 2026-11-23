#include "window/construction/ConstructionRenderer.hpp"

#include <exception>
#include <memory>
#include <thread>
#include <utility>

#include <drm_fourcc.h>

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/construction/ConstructionBatch.hpp"
#include "window/construction/ConstructionCloseSite.hpp"
#include "window/construction/ConstructionMaterial.hpp"
#include "window/construction/ConstructionProgram.hpp"
#include "window/construction/ConstructionShaders.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/FramebufferAllocationState.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"

using namespace hyprimate;
using Draw = animation::Draw<CHyprAnimationCanvas>;

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

bool supports_texture(const Render::ITexture &image) {
    return image.m_texID && image.m_transform == HYPRUTILS_TRANSFORM_NORMAL &&
        (image.m_type == Render::TEXTURE_RGBA || image.m_type == Render::TEXTURE_RGBX) &&
        supports_scene_color(image);
}

bool supports_close_snapshot(Render::IFramebuffer &snapshot, const Render::ITexture &image) {
    // These are the two formats allocated by Hyprland's monitor snapshot producer.
    return (snapshot.m_drmFormat == DRM_FORMAT_ABGR8888 ||
            snapshot.m_drmFormat == DRM_FORMAT_ABGR16161616F) &&
        snapshot.isAllocated() && !snapshot.getMirrorTexture() &&
        image.m_type == Render::TEXTURE_RGBA && supports_texture(image);
}

SP<Render::IFramebuffer> close_work_buffer(Render::CRenderContext &ctx, const Vector2D &size,
    const Render::IFramebuffer &snapshot, const Render::ITexture &image) {
    // The native snapshot has alpha even on XRGB outputs. Use the same host sized
    // work pool as mapped drawing, but request the snapshot's actual format.
    const auto resources = ctx.m_data.pMonitor->resources();
    if (!resources) return nullptr;
    const gl::FramebufferAllocationState ALLOCATION;
    auto buffer = resources->getUnusedWorkBuffer(snapshot.m_drmFormat, image.m_imageDescription, size);
    return ctx.sceneResources() ? ctx.sceneResources()->prepareWorkBuffer(std::move(buffer)) : buffer;
}

}  // namespace

struct CConstructionRenderer::Impl {
    struct CloseScratch {
        construction::studies::RegionalClose::Frame mesh;
        ConstructionBatch effects;
    };

    hyprimate::gl::CompositorContextOwner owner;
    GLuint program = 0;
    hyprimate::CConstructionMaterial material;
    std::unique_ptr<CloseScratch> close_scratch;
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

    bool submit(const ConstructionBatch *batch, Box body, Box capture,
        const construction::CloseSite *close, const Draw &draw);
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

bool CConstructionRenderer::supports_snapshot(Render::IFramebuffer &snapshot) {
    const auto image = snapshot.getTexture();
    return image && supports_close_snapshot(snapshot, *image);
}

bool CConstructionRenderer::Impl::submit(const ConstructionBatch *batch, Box body, Box capture,
    const construction::CloseSite *close, const Draw &draw) try {
    if (!draw.canvas || render_thread != std::this_thread::get_id() || drawing)
        return false;
    drawing = true;
    struct DrawGuard {
        bool &drawing;
        ~DrawGuard() { drawing = false; }
    } guard{drawing};

    draw.canvas->layer().reset();
    if ((batch && (batch->count == 0 || batch->overflow || batch->count > batch->quads.size())) ||
        !g_pHyprRenderer || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL ||
        !draw.image || !*draw.image)
        return false;
    // The framework authorizes this borrow independently for every physical, capture and matte
    // replay. We never infer authorization from pass kind or retain the image or output lease.
    auto &input = **draw.image;
    const auto IMAGE = input.getTexture();
    if (!IMAGE || IMAGE->m_size.x != draw.image_size.x || IMAGE->m_size.y != draw.image_size.y ||
        !(close ? supports_close_snapshot(input, *IMAGE) : supports_texture(*IMAGE)))
        return false;

    const auto VIEW = CConstructionMaterial::prepare_view({.canvas = draw.canvas_box,
        .source = draw.source, .body = body, .capture = capture,
        .image_origin = draw.image_origin, .image_size = draw.image_size, .scale = draw.scale,
        .image_rgbx = IMAGE->m_type == Render::TEXTURE_RGBX, .allow_empty_crop = close != nullptr});
    if (!VIEW) return false;
    if (close) {
        if (draw.image_origin.x != 0 || draw.image_origin.y != 0) return false;
        // A fully clipped draw, or a valid publication without a face or artwork, is
        // transparent. The host composites nothing and does not resume the native fade.
        if (!VIEW->authorized_local_crop) return true;
        if (!close_scratch) close_scratch = std::make_unique_for_overwrite<CloseScratch>();
        if (!close->emit(*VIEW->authorized_local_crop, close_scratch->mesh, close_scratch->effects) ||
            close_scratch->effects.overflow)
            return false;
        if (close_scratch->mesh.source_faces().empty() && close_scratch->effects.count == 0)
            return true;
    }
    if (!owner.ensure_current() || !material.supports_view(*VIEW)) return false;

    auto &ctx = draw.canvas->context();
    if (!ctx.m_data.pMonitor) return false;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    // The pinned host can refuse a lease, but a failed GL storage allocation can
    // SIGABRT in CGLFramebuffer::internalAlloc before a lease can be returned.
    const Vector2D SIZE{VIEW->width, VIEW->height};
    const auto LAYER = close ? close_work_buffer(ctx, SIZE, input, *IMAGE) :
        gl::get_work_buffer(ctx, SIZE);
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
    if (!initialize()) return false;
    const bool SUBMITTED = close ?
        material.draw_close(close_scratch->mesh, close_scratch->effects, *VIEW, IMAGE->m_texID, TEXTURE->m_texID) :
        material.draw(*batch, *VIEW, IMAGE->m_texID, TEXTURE->m_texID);
    if (!SUBMITTED) return false;

    draw.canvas->layer() = LAYER;
    return true;
} catch (const std::exception &) {
    // Unwind recoverable allocation exceptions. No layer is published unless submission
    // finishes successfully.
    return false;
}

bool CConstructionRenderer::draw(const ConstructionBatch &batch, Box body, Box capture,
    const Draw &draw) {
    return m_impl->submit(&batch, body, capture, nullptr, draw);
}

bool CConstructionRenderer::draw(const construction::CloseSite &site, const Draw &draw) {
    return m_impl->submit(nullptr, site.body(), site.capture(), &site, draw);
}
