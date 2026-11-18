#include <array>
#include <utility>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/shared/PaperRenderer.hpp"
#include "animations/smoke/SmokeRenderer.hpp"

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
    if (Render::GL::g_pHyprOpenGL) with_context([&] { field.release(); });
}
CSmokeRenderer::~CSmokeRenderer() {
    if (Render::GL::g_pHyprOpenGL) with_context([&] { m_material.release(); });
}
void CSmokeRenderer::warm_up() {
    if (m_warmed || !Render::GL::g_pHyprOpenGL) return;
    m_warmed = true;
    with_context([&] {
        GLint active = 0, noise = 0, empty = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &noise);
        glActiveTexture(GL_TEXTURE2);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &empty);
        m_material.initialize();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(noise));
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(empty));
        glActiveTexture(static_cast<GLenum>(active));
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
    auto &renderer = *Render::GL::g_pHyprOpenGL;
    const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    const auto state = ctx.saveDrawState();
    const auto layer = g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas);
    if (!layer || !layer->getTexture() || layer->getTexture()->m_texID == draw.window->m_texID)
        return nullptr;
    GLint program = 0, vao = 0, buffer = 0, active = 0, src = 0, dst = 0, srca = 0, dsta = 0;
    std::array<GLint, 4> viewport{};
    std::array<GLboolean, 4> mask{};
    // The fluid passes use texture units 0-6.
    std::array<std::array<GLint, 2>, 7> textures{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srca);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dsta);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE),
               blend = renderer.blendEnabled();
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i][0]);
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &textures[i][1]);
    }
    const bool ready = m_material.initialize();
    if (ready) {
        renderer.setCapStatus(GL_SCISSOR_TEST, false);
        renderer.setCapStatus(GL_STENCIL_TEST, false);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        renderer.blend(false);
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
        g_pHyprRenderer->bindFB(ctx, layer);
        renderer.setViewport(
            0, 0, static_cast<GLsizei>(draw.canvas.x), static_cast<GLsizei>(draw.canvas.y));
        m_material.draw(draw.field->field, draw.frame, target, images);
    }
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        renderer.setActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i][0]);
        glBindTexture(GL_TEXTURE_3D, textures[i][1]);
    }
    renderer.setActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    glUseProgram(static_cast<GLuint>(program));
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    renderer.setCapStatus(GL_SCISSOR_TEST, scissor);
    renderer.setCapStatus(GL_STENCIL_TEST, stencil);
    renderer.blend(blend);
    renderer.blendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
    if (srca != src || dsta != dst) glBlendFuncSeparate(src, dst, srca, dsta);
    renderer.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
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
            ctx, layer->getTexture(), m_data.box, paper_canvas_quad(), data);
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
