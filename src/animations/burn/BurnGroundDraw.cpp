#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "BurnGroundDraw.hpp"
#include "BurnShaders.hpp"
#include "HyprAnimationCanvas.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "gl/RawGlBracket.hpp"

namespace {
    constexpr std::size_t FIELD_BYTES = CBurnGround::WIDTH * CBurnGround::HEIGHT * 4;
    std::uint8_t byte(float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.F, 1.F) * 255.F));
    }
    constexpr const char *VERTEX = R"GLSL(#version 300 es
precision highp float;
out vec2 uv;
void main() {
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    uv=p;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)GLSL";
    std::string fragment() {
        return std::string{R"GLSL(#version 300 es
precision highp float;
uniform sampler2D wallpaper;
uniform sampler2D field;
uniform vec2 origin;
uniform vec2 axisX;
uniform vec2 axisY;
uniform vec4 logicalScreen;
uniform int openingCount;
uniform vec4 openingRect[32];
uniform vec4 openingState[32];
in vec2 uv;
out vec4 color;
)GLSL"} + std::string{burn_shader::COMMON} +
               R"GLSL(
void main() {
    vec2 coarse=texture(field,uv).rg;
    if(coarse.r<=0.0 && openingCount==0) discard;
    vec2 paper=logicalScreen.xy+uv*logicalScreen.zw;
    float severity=0.0;
    float heat=0.0;
    if(coarse.r>0.0) {
        vec2 warp=vec2(burnGrain(paper*0.038),
            burnGrain(paper*0.038+vec2(29.4,13.8)))*2.0-1.0;
        vec2 state=texture(field,uv+warp*12.0/logicalScreen.zw).rg;
        float mottle=burnFbm(paper*0.019);
        severity=smoothstep(0.10+0.19*(1.0-mottle),0.90,state.r);
        heat=state.g*(0.25+1.25*mottle);
    }
    float opacity=severity;
    float frontDistance=0.0, frontWidth=0.055;
    vec2 frontUV=vec2(0.0);
    bool onFront=false;
    for(int index=0;index<openingCount;++index) {
        vec4 rect=openingRect[index];
        vec4 state=openingState[index];
        vec2 local=(uv-rect.xy)/rect.zw;
        if(any(lessThan(local,vec2(0.0))) || any(greaterThan(local,vec2(1.0)))) continue;
        float distance=burnArrival(local,state.y,state.w)-state.x;
        float width=max(state.z,0.0001);
        if(distance<0.0 || distance>width*2.6) continue;
        float rim=1.0-smoothstep(width*0.25,width*2.6,distance);
        if(rim>opacity) {
            opacity=rim;
            frontDistance=distance;
            frontWidth=width;
            frontUV=local;
            onFront=true;
        }
    }
    if(opacity<=0.0) discard;
    vec3 clean=texture(wallpaper,origin+axisX*uv.x+axisY*uv.y).rgb;
    vec3 burned;
    if(onFront) {
        burned=burnChar(clean,frontDistance,frontWidth,frontUV,1.0);
    } else {
        float luminance=dot(clean,vec3(0.2126,0.7152,0.0722));
        vec3 desaturated=mix(clean,vec3(luminance),severity*0.85);
        burned=burnChar(desaturated,(1.0-severity)*0.055*2.6,0.055,paper/800.0,heat);
    }
    color=vec4(burned*opacity,opacity);
}
)GLSL";
    }
}  // namespace

struct CBurnGroundDraw::Impl {
    std::unique_ptr<CShader> shader;
    GLuint field = 0, vao = 0;
    GLint wallpaper_uniform = -1, field_uniform = -1, origin_uniform = -1, axis_x_uniform = -1,
          axis_y_uniform = -1;
    GLint opening_count_uniform = -1, opening_rect_uniform = -1, opening_state_uniform = -1;
    GLint logical_screen_uniform = -1;
    bool attempted = false, ready = false;
    ~Impl() {
        if (!Render::GL::g_pHyprOpenGL) {
            // Compositor teardown has already destroyed the context and its objects.
            shader.reset();
            return;
        }
        const auto DISPLAY = eglGetCurrentDisplay();
        const auto CONTEXT = eglGetCurrentContext();
        const auto DRAW = eglGetCurrentSurface(EGL_DRAW);
        const auto READ = eglGetCurrentSurface(EGL_READ);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        {
            const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
            if (field != 0) glDeleteTextures(1, &field);
            if (vao != 0) glDeleteVertexArrays(1, &vao);
            shader.reset();
        }
        if (DISPLAY != EGL_NO_DISPLAY && CONTEXT != EGL_NO_CONTEXT) {
            eglMakeCurrent(DISPLAY, DRAW, READ, CONTEXT);
        } else {
            eglMakeCurrent(Render::GL::g_pHyprOpenGL->m_eglDisplay,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
        }
    }
    bool initialize() {
        if (attempted) return ready;
        attempted = true;
        shader = std::make_unique<CShader>();
        if (!shader->createProgram(VERTEX, fragment(), true, true) || shader->program() == 0)
            return false;
        const auto PROGRAM = shader->program();
        wallpaper_uniform = glGetUniformLocation(PROGRAM, "wallpaper");
        field_uniform = glGetUniformLocation(PROGRAM, "field");
        origin_uniform = glGetUniformLocation(PROGRAM, "origin");
        axis_x_uniform = glGetUniformLocation(PROGRAM, "axisX");
        axis_y_uniform = glGetUniformLocation(PROGRAM, "axisY");
        opening_count_uniform = glGetUniformLocation(PROGRAM, "openingCount");
        opening_rect_uniform = glGetUniformLocation(PROGRAM, "openingRect[0]");
        opening_state_uniform = glGetUniformLocation(PROGRAM, "openingState[0]");
        logical_screen_uniform = glGetUniformLocation(PROGRAM, "logicalScreen");
        glGenVertexArrays(1, &vao);
        glGenTextures(1, &field);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, field);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, CBurnGround::WIDTH, CBurnGround::HEIGHT);
        ready = field != 0 && vao != 0;
        return ready;
    }
    struct Pass final : IPassElement {
        std::shared_ptr<Impl> renderer;
        CHyprAnimationCanvas::Backdrop wallpaper;
        hyprimate::animation::ScreenFacts screen;
        std::array<std::uint8_t, FIELD_BYTES> pixels{};
        std::array<CBurnGround::Particle, CBurnGround::MAX_PARTICLES> particles{};
        std::size_t particle_count = 0;
        std::array<float, 32 * 4> opening_rect{}, opening_state{};
        std::size_t opening_count = 0;
        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            if (!wallpaper || !wallpaper->framebuffer || !g_pHyprRenderer ||
                !Render::GL::g_pHyprOpenGL || ctx.readOnlyEffects() || ctx.m_renderingCapture ||
                ctx.m_renderingSnapshot || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
                return {};
            const auto IMAGE = wallpaper->framebuffer->getTexture();
            if (!IMAGE || IMAGE->m_texID == 0) return {};
            const Vector2D SIZE{screen.screen.w * screen.scale, screen.screen.h * screen.scale};
            SP<Render::IFramebuffer> layer;
            {
                const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
                const auto STATE = ctx.saveDrawState();
                layer = g_pHyprRenderer->getWorkBuffer(ctx, SIZE);
                if (!layer || !layer->getTexture() ||
                    layer->getTexture()->m_texID == IMAGE->m_texID)
                    return {};
                // RT_GL and the work buffer contract select this framebuffer backend.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
                const auto FRAMEBUFFER =
                    static_cast<Render::GL::CGLFramebuffer *>(layer.get())->getFBID();
                const hyprimate::gl::CRawGlBracket<2> BRACKET{
                    {.viewport_width = static_cast<GLsizei>(SIZE.x),
                        .viewport_height = static_cast<GLsizei>(SIZE.y),
                        .framebuffer = FRAMEBUFFER}};
                if (!renderer->initialize()) return {};
                glBindFramebuffer(GL_FRAMEBUFFER, FRAMEBUFFER);
                glViewport(0, 0, static_cast<GLsizei>(SIZE.x), static_cast<GLsizei>(SIZE.y));
                glDisable(GL_BLEND);
                glDisable(GL_SCISSOR_TEST);
                glDisable(GL_STENCIL_TEST);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                constexpr std::array<float, 4> CLEAR{0, 0, 0, 0};
                glClearBufferfv(GL_COLOR, 0, CLEAR.data());
                glUseProgram(renderer->shader->program());
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, IMAGE->m_texID);
                glBindSampler(0, 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, renderer->field);
                glBindSampler(1, 0);
                GLint unpack_buffer = 0, unpack_alignment = 0;
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    CBurnGround::WIDTH,
                    CBurnGround::HEIGHT,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    pixels.data());
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer));
                glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
                glUniform1i(renderer->opening_count_uniform, static_cast<GLint>(opening_count));
                glUniform4fv(renderer->opening_rect_uniform,
                    static_cast<GLsizei>(opening_count),
                    opening_rect.data());
                glUniform4fv(renderer->opening_state_uniform,
                    static_cast<GLsizei>(opening_count),
                    opening_state.data());
                glUniform1i(renderer->wallpaper_uniform, 0);
                glUniform1i(renderer->field_uniform, 1);
                glUniform4f(renderer->logical_screen_uniform,
                    static_cast<float>(screen.screen.x),
                    static_cast<float>(screen.screen.y),
                    static_cast<float>(screen.screen.w),
                    static_cast<float>(screen.screen.h));
                glUniform2f(renderer->origin_uniform,
                    static_cast<float>(wallpaper->origin.x),
                    static_cast<float>(wallpaper->origin.y));
                glUniform2f(renderer->axis_x_uniform,
                    static_cast<float>(wallpaper->axis_x.x),
                    static_cast<float>(wallpaper->axis_x.y));
                glUniform2f(renderer->axis_y_uniform,
                    static_cast<float>(wallpaper->axis_y.x),
                    static_cast<float>(wallpaper->axis_y.y));
                glBindVertexArray(renderer->vao);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            Render::GL::CHyprOpenGLImpl::STextureRenderData data{};
            data.damage = &ctx.m_data.damage;
            data.allowCustomUV = true;
            Render::GL::g_pHyprOpenGL->renderTextureMesh(
                ctx, layer->getTexture(), CBox{0, 0, SIZE.x, SIZE.y}, sheet_canvas_quad(), data);
            for (std::size_t index = 0; index < particle_count; ++index) {
                const auto &particle = particles[index];
                const double PHASE =
                    particle.size * 13.7 + static_cast<double>(particle.owner % 257);
                const double FLICKER =
                    .72 + .28 * std::sin(particle.age * (19 + particle.size * 3) + PHASE);
                const double OPACITY =
                    (1.0 - particle.age / particle.lifetime) * (particle.ember ? FLICKER : .8);
                const double EXTENT =
                    particle.size * screen.scale * (particle.ember ? .85 + .15 * FLICKER : 1.0);
                const double ASPECT = particle.ember ? 1.0 : .45 + .25 * (1 + std::sin(PHASE));
                const CBox BOX{
                    (particle.position.x - screen.screen.x) * screen.scale - EXTENT * ASPECT * .5,
                    (particle.position.y - screen.screen.y) * screen.scale - EXTENT * .5,
                    EXTENT * ASPECT,
                    EXTENT};
                if (BOX.copy().intersection(CBox{0, 0, SIZE.x, SIZE.y}).empty()) continue;
                CRectPassElement::SRectData rect;
                rect.box = BOX;
                const float TONE = static_cast<float>(.5 + .5 * std::sin(PHASE));
                rect.color = particle.ember ? CHyprColor{1.F,
                                                  .12F + TONE * .27F,
                                                  .006F + TONE * .045F,
                                                  static_cast<float>(OPACITY)}
                                            : CHyprColor{.18F + TONE * .24F,
                                                  .17F + TONE * .24F,
                                                  .16F + TONE * .24F,
                                                  static_cast<float>(OPACITY)};
                rect.round = particle.ember ? 1 : 0;
                g_pHyprRenderer->draw(ctx, rect, ctx.m_data.damage);
            }
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext &) override {
            return false;
        }
        bool needsPrecomputeBlur(Render::CRenderContext &) override {
            return false;
        }
        bool disableSimplification(Render::CRenderContext &) override {
            return true;
        }
        bool requiresFullDamage(Render::CRenderContext &) override {
            return true;
        }
        const char *passName() override {
            return "hyprimate-burn-ground";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }
    };
};

CBurnGroundDraw::CBurnGroundDraw() : m_impl(std::make_shared<Impl>()) {
}
CBurnGroundDraw::~CBurnGroundDraw() = default;
void CBurnGroundDraw::queue(CHyprAnimationCanvas &canvas,
    const CBurnGround &ground,
    const hyprimate::animation::ScreenFacts &screen) {
    auto &ctx = canvas.context();
    if (canvas.pass() != hyprimate::animation::PassKind::Live || ctx.readOnlyEffects() ||
        ctx.m_renderingCapture || ctx.m_renderingSnapshot || !ground.active(screen.monitor) ||
        !hyprimate::is_usable(screen.screen) || !(screen.scale > 0) || !ctx.m_data.pMonitor ||
        !g_pHyprRenderer || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
        return;
    auto pass = makeUnique<Impl::Pass>();
    pass->renderer = m_impl;
    pass->screen = screen;
    bool visible = false;
    for (const auto &area : ground.areas()) {
        if (!area || area->monitor != screen.monitor || !area->active() ||
            (area->workspace != 0 && area->workspace != screen.workspace &&
                area->workspace != screen.special_workspace))
            continue;
        for (std::size_t index = 0; index < area->opening_count && pass->opening_count < 32;
            ++index) {
            const auto &opening = area->openings[index];
            const auto OFFSET = pass->opening_count++ * 4;
            pass->opening_rect[OFFSET] =
                static_cast<float>((opening.front.capture.x - screen.screen.x) / screen.screen.w);
            pass->opening_rect[OFFSET + 1] =
                static_cast<float>((opening.front.capture.y - screen.screen.y) / screen.screen.h);
            pass->opening_rect[OFFSET + 2] =
                static_cast<float>(opening.front.capture.w / screen.screen.w);
            pass->opening_rect[OFFSET + 3] =
                static_cast<float>(opening.front.capture.h / screen.screen.h);
            pass->opening_state[OFFSET] = static_cast<float>(opening.front.progress);
            pass->opening_state[OFFSET + 1] = hyprimate::burn_phase(opening.front.seed);
            pass->opening_state[OFFSET + 2] = static_cast<float>(opening.width);
            pass->opening_state[OFFSET + 3] = static_cast<float>(opening.variation);
            visible = true;
        }
        for (std::size_t index = 0; index < area->cells.size(); ++index) {
            const auto &cell = area->cells[index];
            if (cell.severity <= 0) continue;
            const float DAMAGE = cell.damage();
            if (DAMAGE <= 0) continue;
            pass->pixels[index * 4] = std::max(pass->pixels[index * 4], byte(DAMAGE));
            pass->pixels[index * 4 + 1] = std::max(pass->pixels[index * 4 + 1], byte(cell.heat()));
            visible = true;
        }
        for (std::size_t index = 0;
            index < area->particle_count && pass->particle_count < pass->particles.size();
            ++index) {
            pass->particles[pass->particle_count++] = area->particles[index];
            visible = true;
        }
    }
    if (!visible) return;
    const CBox BOX{0, 0, screen.screen.w * screen.scale, screen.screen.h * screen.scale};
    pass->wallpaper = canvas.capture_wallpaper(BOX, BOX);
    if (!pass->wallpaper) return;
    Render::IHyprRenderer::addPassElement(ctx, std::move(pass));
}
