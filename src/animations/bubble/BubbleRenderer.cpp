#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Region.hpp>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "bubble_shaders_generated.hpp"
#include "gl/RawGlBracket.hpp"
#include "animations/shared/SheetRenderer.hpp"

namespace {
    // Runs work with the compositor's EGL context current, then restores whatever was current
    // before.
    template <class F>
    void with_compositor_context(F &&work) {
        const EGLDisplay DISPLAY = eglGetCurrentDisplay();
        const EGLContext CONTEXT = eglGetCurrentContext();
        const EGLSurface DRAW = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface READ = eglGetCurrentSurface(EGL_READ);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        std::forward<F>(work)();
        if (DISPLAY != EGL_NO_DISPLAY && CONTEXT != EGL_NO_CONTEXT) {
            eglMakeCurrent(DISPLAY, DRAW, READ, CONTEXT);
        } else if (CONTEXT == EGL_NO_CONTEXT) {
            eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
        }
    }
}  // namespace

CBubbleRenderer::~CBubbleRenderer() {
    if (Render::GL::g_pHyprOpenGL) {
        with_compositor_context([&] {
            Render::GL::g_pHyprOpenGL->bindArrayBuffer(0);
            m_material.release();
            for (auto &shader : m_shaders) {
                shader.reset();
            }
        });
    }
}

bool CBubbleRenderer::initialize() {
    if (m_warmed) {
        return m_ready;
    }
    m_warmed = true;
    constexpr std::array SOURCES{
        std::pair{bubble_shader::VERTEX.data(), bubble_shader::FRAGMENT.data()},
        std::pair{bubble_shader::CURL_VERTEX.data(), bubble_shader::CURL_FRAGMENT.data()},
        std::pair{bubble_shader::PARTICLE_VERTEX.data(), bubble_shader::PARTICLE_FRAGMENT.data()},
        std::pair{bubble_shader::FILM_VERTEX.data(), bubble_shader::FILM_FRAGMENT.data()},
        std::pair{bubble_shader::MICROBUBBLE_VERTEX.data(), bubble_shader::MICROBUBBLE_FRAGMENT.data()},
    };
    for (std::size_t i = 0; i < m_shaders.size(); ++i) {
        m_shaders[i] = std::make_unique<CShader>();
        if (!m_shaders[i]->createProgram(SOURCES[i].first, SOURCES[i].second, true, true) ||
            m_shaders[i]->program() == 0U) {
            return false;
        }
    }
    m_ready = m_material.initialize({.membrane = m_shaders[0]->program(), .curl = m_shaders[1]->program(),
        .particles = m_shaders[2]->program(), .film = m_shaders[3]->program(), .micro = m_shaders[4]->program()});
    return m_ready;
}

void CBubbleRenderer::warm_up() {
    if (m_warmed || !g_pHyprRenderer || !Render::GL::g_pHyprOpenGL) {
        return;
    }
    with_compositor_context([&] {
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<4> BRACKET{{}};
        (void)initialize();
    });
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): GL capture, drawing and restoration
// must retain their order.
SP<Render::IFramebuffer> CBubbleRenderer::render(
    Render::CRenderContext &ctx, const SBubbleDraw &draw) {
    if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
        (!draw.on_glass && (!draw.window || (draw.window->m_texID == 0U) ||
                               (draw.window->m_type != Render::TEXTURE_RGBA &&
                                   draw.window->m_type != Render::TEXTURE_RGBX) ||
                               (!(draw.texture_size.x >= 1) || !(draw.texture_size.y >= 1)))) ||
        (!(draw.canvas.x > 0) || !(draw.canvas.y > 0) || !(draw.source.w > 0) ||
            !(draw.source.h > 0) || !(draw.frame.size.x > 0) || !(draw.frame.size.y > 0))) {
        return nullptr;
    }
    const auto WALLPAPER =
        draw.wallpaper && draw.wallpaper->framebuffer &&
                draw.pass != hyprimate::animation::PassKind::Inert
            ? draw.wallpaper->framebuffer->getTexture()
            : SP<Render::ITexture>{};
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    const auto LAYER = g_pHyprRenderer->getWorkBuffer(ctx, draw.canvas);
    const GLuint IMAGE = draw.window ? draw.window->m_texID : 0;
    if (!LAYER || !LAYER->getTexture() || LAYER->getTexture()->m_texID == IMAGE ||
        (WALLPAPER && LAYER->getTexture()->m_texID == WALLPAPER->m_texID)) {
        return nullptr;
    }
    // RT_GL is the backend that owns the work buffer used by this renderer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): work buffer is GL-backed.
    const auto FRAMEBUFFER = static_cast<Render::GL::CGLFramebuffer *>(LAYER.get())->getFBID();
    const hyprimate::gl::CRawGlBracket<4> BRACKET{{.viewport_width = static_cast<GLsizei>(draw.canvas.x),
        .viewport_height = static_cast<GLsizei>(draw.canvas.y), .framebuffer = FRAMEBUFFER}};
    const bool READY = initialize();
    if (READY) {
        g_pHyprRenderer->bindFB(ctx, LAYER);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const SBubbleTarget TARGET{
            .canvas = {.x = draw.canvas.x, .y = draw.canvas.y},
            .origin = draw.output_origin,
            .source_pos = {.x = draw.source.x, .y = draw.source.y},
            .source_size = {.x = draw.source.w, .y = draw.source.h},
        };
        SBubbleImages images;
        images.window = IMAGE;
        images.texture_origin = draw.texture_origin;
        images.texture_size = draw.on_glass ? hyprimate::Vec2{.x = 1, .y = 1} : draw.texture_size;
        if (WALLPAPER) {
            images.wallpaper = WALLPAPER->m_texID;
            images.wallpaper_origin = {
                .x = draw.wallpaper->origin.x,
                .y = draw.wallpaper->origin.y,
            };
            images.wallpaper_x = {.x = draw.wallpaper->axis_x.x, .y = draw.wallpaper->axis_x.y};
            images.wallpaper_y = {.x = draw.wallpaper->axis_y.x, .y = draw.wallpaper->axis_y.y};
            images.wallpaper_pixel = {
                .x = 1.0 / WALLPAPER->m_size.x,
                .y = 1.0 / WALLPAPER->m_size.y,
            };
        }
        m_material.draw_membranes(draw.frame, TARGET, images);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (draw.on_glass) {
            m_material.draw_particles(std::span{draw.glass_particles}.first(
                                          std::min(draw.glass_count, draw.glass_particles.size())),
                draw.frame.size,
                TARGET,
                true);
            if (draw.wake) {
                m_material.draw_wake(*draw.wake, TARGET);
            }
        } else {
            const auto SHELL_COUNT = hyprimate::bubble_particles(
                draw.frame.shell, std::span{m_particles}.first(hyprimate::K_BUBBLE_MAX_PARTICLES));
            const auto WINDOW_COUNT = hyprimate::bubble_particles(draw.frame.window,
                std::span{m_particles}.subspan(SHELL_COUNT).first(hyprimate::K_BUBBLE_MAX_PARTICLES));
            m_material.draw_particles(std::span{m_particles}.first(SHELL_COUNT + WINDOW_COUNT),
                draw.frame.size,
                TARGET,
                false);
            m_material.draw_microbubbles(std::span{draw.frame.micro}.first(std::min(
                                             draw.frame.micro_count, draw.frame.micro.size())),
                draw.frame.size,
                TARGET);
        }
    }
    return READY ? LAYER : nullptr;
}

CBubblePassElement::CBubblePassElement(SData data) : m_data(std::move(data)) {
}
std::vector<UP<IPassElement>> CBubblePassElement::draw(Render::CRenderContext &ctx) {
    const auto LAYER =
        m_data.renderer ? m_data.renderer->render(ctx, m_data.draw) : SP<Render::IFramebuffer>{};
    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &m_data.damage;
    texture_data.a = 1.F;
    texture_data.allowCustomUV = true;
    texture_data.allowDim = false;
    if (LAYER) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, LAYER->getTexture(), m_data.box, sheet_canvas_quad(), texture_data);
    }
    return {};
}
bool CBubblePassElement::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}
bool CBubblePassElement::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}
const char *CBubblePassElement::passName() {
    return "hyprimate-bubble";
}
ePassElementType CBubblePassElement::type() {
    return EK_CUSTOM;
}
std::optional<CBox> CBubblePassElement::boundingBox([[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}
CRegion CBubblePassElement::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
