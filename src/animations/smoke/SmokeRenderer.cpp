#include <array>
#include <utility>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/shared/SheetRenderer.hpp"
#include "animations/smoke/SmokeRenderer.hpp"
#include "gl/RawGlBracket.hpp"
#include "smoke_shaders_generated.hpp"

namespace {
    template <class F>
    void with_context(F &&work) {
        const EGLDisplay display = eglGetCurrentDisplay();
        const EGLContext context = eglGetCurrentContext();
        const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW),
                         read = eglGetCurrentSurface(EGL_READ);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        std::forward<F>(work)();
        if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
            eglMakeCurrent(display, draw, read, context);
        else if (context == EGL_NO_CONTEXT)
            eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
    }
}  // namespace
CSmokeFieldHandle::~CSmokeFieldHandle() {
    if (Render::GL::g_pHyprOpenGL && g_pHyprRenderer) with_context([&] {
        auto &renderer = *Render::GL::g_pHyprOpenGL;
        if (field.framebuffer() != 0) renderer.onFramebufferDeleted(field.framebuffer());
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<7> bracket{{}};
        field.release();
    });
}
CSmokeRenderer::~CSmokeRenderer() {
    if (!Render::GL::g_pHyprOpenGL || !g_pHyprRenderer) {
        // Context teardown reclaims GL resources when the renderer is already gone.
        for (auto &shader : m_shaders) (void)shader.release();
        return;
    }
    with_context([&] {
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<7> bracket{{}};
        m_material.release();
        for (auto &shader : m_shaders) shader.reset();
    });
}
bool CSmokeRenderer::initialize() {
    if (m_tried) return m_programs.ids[SSmokePrograms::Composite] != 0;
    m_tried = true;
    const std::array<const char *, SSmokePrograms::ProgramCount> sources{smoke_shader::VELOCITY.data(),
        smoke_shader::DIVERGENCE.data(), smoke_shader::PRESSURE.data(), smoke_shader::PROJECT.data(),
        smoke_shader::ADVECT.data(), smoke_shader::DYE.data(), smoke_shader::COMPOSITE.data()};
    for (std::size_t i = 0; i < sources.size(); ++i) {
        m_shaders[i] = std::make_unique<CShader>();
        if (!m_shaders[i]->createProgram(smoke_shader::VERTEX.data(), sources[i], true, true)) return false;
        m_programs.ids[i] = m_shaders[i]->program();
    }
    return m_material.initialize(m_programs);
}
void CSmokeRenderer::warm_up() {
    if (m_warmed || !Render::GL::g_pHyprOpenGL || !g_pHyprRenderer) return;
    m_warmed = true;
    with_context([&] {
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<7> bracket{{}};
        initialize();
    });
}
SP<Render::IFramebuffer> CSmokeRenderer::render(
    Render::CRenderContext &ctx, const SSmokeDraw &draw) {
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !draw.field || !draw.window ||
        !draw.window->m_texID ||
        (draw.window->m_type != Render::TEXTURE_RGBA &&
            draw.window->m_type != Render::TEXTURE_RGBX) ||
        !(draw.canvas.x > 0 && draw.canvas.y > 0 && draw.source.w > 0 && draw.source.h > 0 &&
            draw.texture_size.x > 0 && draw.texture_size.y > 0))
        return nullptr;
    const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    const auto state = ctx.saveDrawState();
    const auto layer = g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas);
    if (!layer || !layer->getTexture() || layer->getTexture()->m_texID == draw.window->m_texID)
        return nullptr;
    auto *gl_layer = dynamic_cast<Render::GL::CGLFramebuffer *>(layer.get());
    if (!gl_layer) return nullptr;
    g_pHyprRenderer->bindFB(ctx, layer);
    const hyprimate::gl::SShadowedState known{.viewport_width = static_cast<GLsizei>(draw.canvas.x),
        .viewport_height = static_cast<GLsizei>(draw.canvas.y), .framebuffer = gl_layer->getFBID()};
    const hyprimate::gl::CRawGlBracket<7> bracket{known};
    const bool ready = initialize();
    if (ready) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const SSmokeTarget target{.canvas = {draw.canvas.x, draw.canvas.y},
            .origin = draw.output_origin,
            .source_pos = {draw.source.x, draw.source.y},
            .source_size = {draw.source.w, draw.source.h}};
        const SSmokeImages images{.window = draw.window->m_texID,
            .texture_origin = draw.texture_origin,
            .texture_size = draw.texture_size};
        // Steps once per frame; further monitors reuse the advanced field.
        m_material.simulate(draw.field->field, draw.frame, target, images);
        glBindFramebuffer(GL_FRAMEBUFFER, known.framebuffer);
        glViewport(0, 0, known.viewport_width, known.viewport_height);
        m_material.draw(draw.field->field, draw.frame, target, images);
    }
    return ready ? layer : nullptr;
}
CSmokePassElement::CSmokePassElement(SData data) : m_data(std::move(data)) {
}
std::vector<UP<IPassElement>> CSmokePassElement::draw(Render::CRenderContext &ctx) {
    const auto layer =
        m_data.renderer ? m_data.renderer->render(ctx, m_data.draw) : SP<Render::IFramebuffer>{};
    Render::GL::CHyprOpenGLImpl::STextureRenderData data{};
    data.damage = &m_data.damage;
    data.a = m_data.alpha;
    data.allowCustomUV = true;
    data.allowDim = false;
    if (layer)
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, layer->getTexture(), m_data.box, sheet_canvas_quad(), data);
    else {
        const auto &d = m_data.draw;
        if (!d.window || !(d.canvas.x > 0 && d.canvas.y > 0 && d.source.w > 0 && d.source.h > 0 &&
                             d.texture_size.x > 0 && d.texture_size.y > 0))
            return {};
        const float u0 = (d.source.x - d.texture_origin.x) / d.texture_size.x,
                    v0 = (d.source.y - d.texture_origin.y) / d.texture_size.y;
        const float u1 = (d.source.x + d.source.w - d.texture_origin.x) / d.texture_size.x,
                    v1 = (d.source.y + d.source.h - d.texture_origin.y) / d.texture_size.y;
        const CBox box{m_data.box.x + (d.source.x - d.output_origin.x) * m_data.box.w / d.canvas.x,
            m_data.box.y + (d.source.y - d.output_origin.y) * m_data.box.h / d.canvas.y,
            d.source.w * m_data.box.w / d.canvas.x,
            d.source.h * m_data.box.h / d.canvas.y};
        const std::vector<SMeshRenderVertex> quad{{.x = 0, .y = 0, .u = u0, .v = v0},
            {.x = 0, .y = 1, .u = u0, .v = v1},
            {.x = 1, .y = 0, .u = u1, .v = v0},
            {.x = 1, .y = 0, .u = u1, .v = v0},
            {.x = 0, .y = 1, .u = u0, .v = v1},
            {.x = 1, .y = 1, .u = u1, .v = v1}};
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, d.window, box, quad, data);
    }
    return {};
}
bool CSmokePassElement::needsLiveBlur(Render::CRenderContext &) {
    return false;
}
bool CSmokePassElement::needsPrecomputeBlur(Render::CRenderContext &) {
    return false;
}
const char *CSmokePassElement::passName() {
    return "hyprimate-smoke";
}
ePassElementType CSmokePassElement::type() {
    return EK_CUSTOM;
}
std::optional<CBox> CSmokePassElement::boundingBox(Render::CRenderContext &) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}
CRegion CSmokePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
