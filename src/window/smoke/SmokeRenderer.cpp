#include <array>
#include <new>
#include <utility>

#include <GLES3/gl32.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"
#include "smoke_shaders_generated.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/smoke/SmokeRenderer.hpp"
#include "window/smoke/SmokeStyle.hpp"

CSmokeFieldHandle::~CSmokeFieldHandle() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_owner};
    if (!CONTEXT) return;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<7> BRACKET{{}};
    if (field.framebuffer() != 0)
        Render::GL::g_pHyprOpenGL->onFramebufferDeleted(field.framebuffer());
    field.release();
}
CSmokeRenderer::~CSmokeRenderer() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{m_owner};
    if (!CONTEXT) {
        // CShader cannot detach its name. Context teardown reclaims the GL
        // objects; do not run the host wrapper destructor in a foreign context.
        for (auto &shader : m_shaders) {
            [[maybe_unused]] const auto *released = shader.release();
        }
        return;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<7> BRACKET{{}};
    m_material.release();
    for (auto &shader : m_shaders) shader.reset();
}
// Both callers have already checked m_owner in the current EGL binding.
bool CSmokeRenderer::initialize() {
    if (m_ready) return true;
    const std::array<const char *, SSmokePrograms::ProgramCount> SOURCES{
        smoke_shader::VELOCITY.data(),
        smoke_shader::DIVERGENCE.data(),
        smoke_shader::PRESSURE.data(),
        smoke_shader::PROJECT.data(),
        smoke_shader::ADVECT.data(),
        smoke_shader::DYE.data(),
        smoke_shader::COMPOSITE.data()};
    // Keep failed shader construction local so neither retries nor rendering can observe
    // a partially compiled program set.
    try {
        decltype(m_shaders) shaders;
        SSmokePrograms programs;
        for (std::size_t i = 0; i < SOURCES.size(); ++i) {
            shaders[i] = std::make_unique<CShader>();
            if (!shaders[i]->createProgram(smoke_shader::VERTEX.data(), SOURCES[i], true, true))
                return false;
            programs.ids[i] = shaders[i]->program();
        }
        if (!m_material.initialize(programs)) return false;
        m_shaders = std::move(shaders);
        m_programs = programs;
        m_ready = true;
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
bool CSmokeRenderer::warm_up() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !m_owner.ensure_current()) return false;
    if (m_warmed) return true;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<7> BRACKET{{}};
    m_warmed = initialize();
    return m_warmed;
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
    const hyprimate::gl::CurrentCompositorContext CURRENT_CONTEXT{};
    if (!m_owner.ensure_current(CURRENT_CONTEXT) ||
        !draw.field->m_owner.ensure_current(CURRENT_CONTEXT))
        return nullptr;
    if (!draw.simulate && draw.field->field.framebuffer() == 0) return nullptr;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto state = ctx.saveDrawState();
    const auto layer = hyprimate::gl::get_work_buffer(ctx, draw.canvas, CURRENT_CONTEXT);
    if (!layer || !layer->getTexture() || layer->getTexture()->m_texID == draw.window->m_texID)
        return nullptr;
    auto *gl_layer = dynamic_cast<Render::GL::CGLFramebuffer *>(layer.get());
    if (gl_layer == nullptr) return nullptr;
    g_pHyprRenderer->bindFB(ctx, layer);
    const hyprimate::gl::SShadowedState KNOWN{.viewport_width = static_cast<GLsizei>(draw.canvas.x),
        .viewport_height = static_cast<GLsizei>(draw.canvas.y),
        .framebuffer = gl_layer->getFBID()};
    const hyprimate::gl::CRawGlBracket<7> BRACKET{KNOWN};
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
        // The simulation is keyed by model time, not the number of draw calls.
        if (draw.simulate && !m_material.simulate(draw.field->field, draw.frame, target, images))
            return nullptr;
        glBindFramebuffer(GL_FRAMEBUFFER, KNOWN.framebuffer);
        glViewport(0, 0, KNOWN.viewport_width, KNOWN.viewport_height);
        m_material.draw(draw.field->field, draw.frame, target, images);
    }
    return ready ? layer : nullptr;
}

bool draw_smoke(SSmokeStyle &state,
    CHyprAnimationCanvas &canvas,
    const SP<Render::IFramebuffer> &image,
    const hyprimate::Box &source,
    const hyprimate::Box &canvas_box,
    hyprimate::Vec2 image_origin,
    hyprimate::Vec2 image_size,
    double scale,
    const SSmokeResources &resources) {
    using hyprimate::animation::PassKind;
    const auto PASS = canvas.pass();
    auto &field = PASS == PassKind::WorkspaceSource ? state.capture_field : state.field;
    if (!field && PASS != PassKind::Inert) {
        try {
            field = std::make_shared<CSmokeFieldHandle>();
        } catch (const std::bad_alloc &) {
            return false;
        }
    }
    if (!field || !image || !(scale > 0)) {
        return false;
    }
    canvas.layer() = resources.renderer->render(canvas.context(),
        SSmokeDraw{
            .window = image->getTexture(),
            .field = field,
            .frame = state.smoke.frame({.x = source.w / scale, .y = source.h / scale}),
            .canvas = {canvas_box.w, canvas_box.h},
            .source = {source.x, source.y, source.w, source.h},
            .texture_origin = image_origin,
            .texture_size = image_size,
            .output_origin = {.x = canvas_box.x, .y = canvas_box.y},
            .simulate = PASS != PassKind::Inert,
        });
    return static_cast<bool>(canvas.layer());
}
