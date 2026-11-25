#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "BurnGroundDebris.hpp"
#include "BurnGroundDraw.hpp"
#include "BurnGroundShader.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/RawGlBracket.hpp"
#include "gl/Storage.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/shared/SheetRenderer.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"
#include "perf/GpuTimer.hpp"

namespace {
    constexpr std::size_t FIELD_BYTES = CBurnGround::WIDTH * CBurnGround::HEIGHT * 4;
    std::uint8_t byte(float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.F, 1.F) * 255.F));
    }
    bool live_ground(const Render::CRenderContext &ctx) {
        return g_pSessionLockManager && !g_pSessionLockManager->isSessionLocked() &&
               !ctx.readOnlyEffects() && !ctx.m_renderingCapture && !ctx.m_renderingSnapshot &&
               WorkspaceCaptureScope::for_context(ctx) == nullptr &&
               !WorkspaceCaptureScope::is_destination_live(ctx);
    }

    // Tile-align crops so a one-pixel drag can reuse the same work-buffer size.
    struct Extent {
        double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
        void include(double l, double t, double r, double b) {
            left = std::min(left, l);
            top = std::min(top, t);
            right = std::max(right, r);
            bottom = std::max(bottom, b);
        }
        [[nodiscard]] CBox pixels(const hyprimate::Box &screen, double scale) const {
            constexpr double TILE = 64;
            const double x = std::max(0., std::floor((left - screen.x) * scale / TILE) * TILE);
            const double y = std::max(0., std::floor((top - screen.y) * scale / TILE) * TILE);
            const double r = std::min(
                std::round(screen.w * scale), std::ceil((right - screen.x) * scale / TILE) * TILE);
            const double b = std::min(
                std::round(screen.h * scale), std::ceil((bottom - screen.y) * scale / TILE) * TILE);
            if (r <= x || b <= y) return {};
            return {x, y, r - x, b - y};
        }
    };
}  // namespace

struct CBurnGroundDraw::Impl {
    hyprimate::gl::CompositorContextOwner owner;
    std::unique_ptr<CShader> shader;
    GLuint field = 0, vao = 0;
    CBurnGroundDebris debris;
    GLint wallpaper_uniform = -1, field_uniform = -1, origin_uniform = -1, axis_x_uniform = -1,
          axis_y_uniform = -1;
    GLint opening_count_uniform = -1, opening_rect_uniform = -1, opening_state_uniform = -1;
    GLint logical_screen_uniform = -1, view_uv_uniform = -1;
    bool ready = false;
    ~Impl() {
        const hyprimate::gl::ScopedCompositorContext CONTEXT{owner};
        if (!CONTEXT) {
            // CShader cannot detach its name. Let context teardown reclaim the
            // objects rather than destroy the host wrapper in a foreign context.
            [[maybe_unused]] const auto *abandoned = shader.release();
            return;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
        debris.release();
        if (field != 0) glDeleteTextures(1, &field);
        if (vao != 0) glDeleteVertexArrays(1, &vao);
        shader.reset();
    }
    bool initialize() {
        if (!owner.ensure_current()) return false;
        if (ready) return true;
        const auto DISCARD = [&] {
            glDeleteTextures(1, &field);
            glDeleteVertexArrays(1, &vao);
            field = vao = 0;
            shader.reset();
        };
        try {
            shader = std::make_unique<CShader>();
            if (!shader->createProgram(
                    burn_ground_shader::VERTEX, burn_ground_shader::fragment(), true, true) ||
                shader->program() == 0) {
                DISCARD();
                return false;
            }
        } catch (const std::exception &) {
            DISCARD();
            return false;
        }
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
        view_uv_uniform = glGetUniformLocation(PROGRAM, "viewUV");
        glGenVertexArrays(1, &vao);
        glGenTextures(1, &field);
        if (!field || !vao) {
            DISCARD();
            return false;
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, field);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!hyprimate::gl::allocate_texture_2d(
                GL_RGBA8, CBurnGround::WIDTH, CBurnGround::HEIGHT)) {
            DISCARD();
            return false;
        }
        ready = true;
        return ready;
    }
    struct Pass final : IPassElement {
        std::shared_ptr<Impl> renderer;
        CHyprAnimationCanvas::Backdrop wallpaper;
        hyprimate::animation::ScreenFacts screen;
        CBox viewport;
        std::array<std::uint8_t, FIELD_BYTES> pixels{};
        std::array<CBurnGroundDebris::Instance, CBurnGroundDebris::MAX_INSTANCES> particles;
        std::size_t particle_count = 0;
        std::array<float, 32 * 4> opening_rect{}, opening_state{};
        std::size_t opening_count = 0;
        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            constexpr hyprimate::perf::Key KEY{
                hyprimate::perf::Domain::Window, "burn", "shared"};
            const hyprimate::perf::CpuScope CPU_PROFILE{KEY, hyprimate::perf::Phase::Draw};
            if (!wallpaper || !wallpaper->framebuffer || !g_pHyprRenderer ||
                !Render::GL::g_pHyprOpenGL || !live_ground(ctx) ||
                g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
                return {};
            if (!renderer->owner.ensure_current() || !wallpaper->owner.ensure_current()) return {};
            const auto IMAGE = wallpaper->framebuffer->getTexture();
            if (!IMAGE || IMAGE->m_texID == 0) return {};
            const hyprimate::perf::GpuScope GPU_PROFILE{KEY};
            const Vector2D SIZE{viewport.w, viewport.h};
            const hyprimate::Box VIEW{screen.screen.x + viewport.x / screen.scale,
                screen.screen.y + viewport.y / screen.scale,
                viewport.w / screen.scale,
                viewport.h / screen.scale};
            SP<Render::IFramebuffer> layer;
            {
                const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
                const auto STATE = ctx.saveDrawState();
                layer = hyprimate::gl::get_work_buffer(ctx, SIZE);
                if (!layer || !layer->getTexture() || !ctx.m_data.currentFB ||
                    !ctx.m_data.currentFB->getTexture() ||
                    layer->getTexture()->m_texID == IMAGE->m_texID ||
                    layer->getTexture()->m_texID == ctx.m_data.currentFB->getTexture()->m_texID)
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
                {
                    const hyprimate::gl::ClientPixelUnpack UNPACK;
                    glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        CBurnGround::WIDTH,
                        CBurnGround::HEIGHT,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        pixels.data());
                }
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
                glUniform4f(renderer->view_uv_uniform,
                    static_cast<float>((VIEW.x - screen.screen.x) / screen.screen.w),
                    static_cast<float>((VIEW.y - screen.screen.y) / screen.screen.h),
                    static_cast<float>(VIEW.w / screen.screen.w),
                    static_cast<float>(VIEW.h / screen.screen.h));
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
                if (particle_count && renderer->debris.initialize()) {
                    renderer->debris.draw({particles.data(), particle_count},
                        VIEW,
                        screen.screen,
                        {wallpaper->origin.x, wallpaper->origin.y},
                        {wallpaper->axis_x.x, wallpaper->axis_x.y},
                        {wallpaper->axis_y.x, wallpaper->axis_y.y});
                }
            }
            Render::GL::CHyprOpenGLImpl::STextureRenderData data{};
            data.damage = &ctx.m_data.damage;
            data.allowCustomUV = true;
            {
                const hyprimate::gl::NativeMeshSamplers SAMPLERS;
                Render::GL::g_pHyprOpenGL->renderTextureMesh(
                    ctx, layer->getTexture(), viewport, sheet_canvas_quad(), data);
            }
            hyprimate::perf::touch(KEY, screen.monitor);
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
bool CBurnGroundDraw::warm_up() {
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT || !m_impl->owner.ensure_current()) return false;
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::CRawGlBracket<2> BRACKET{{}};
    return m_impl->initialize() && m_impl->debris.initialize();
}
void CBurnGroundDraw::queue(CHyprAnimationCanvas &canvas,
    const CBurnGround &ground,
    const hyprimate::animation::ScreenFacts &screen) {
    auto &ctx = canvas.context();
    if (canvas.pass() != hyprimate::animation::PassKind::Live || !live_ground(ctx) ||
        !ground.active(screen.monitor) || !hyprimate::is_usable(screen.screen) ||
        !(screen.scale > 0) || !ctx.m_data.pMonitor || !g_pHyprRenderer ||
        g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
        return;
    auto pass = makeUnique<Impl::Pass>();
    pass->renderer = m_impl;
    pass->screen = screen;
    Extent draw_extent, source_extent;
    std::size_t min_col = CBurnGround::WIDTH, min_row = CBurnGround::HEIGHT, max_col = 0,
                max_row = 0;
    for (const auto &field : ground.areas()) {
        const auto *area = &field;
        if (area->monitor != screen.monitor || !area->active() ||
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
            const auto &box = opening.front.capture;
            draw_extent.include(box.x, box.y, box.x + box.w, box.y + box.h);
        }
        for (std::size_t index = 0; index < area->cells.size(); ++index) {
            const auto &cell = area->cells[index];
            const float MOVEMENT = cell.movement_damage();
            const float DAMAGE = cell.damage();
            if (DAMAGE <= 0 && MOVEMENT <= 0) continue;
            if (DAMAGE > 0) {
                pass->pixels[index * 4] = std::max(pass->pixels[index * 4], byte(DAMAGE));
                pass->pixels[index * 4 + 1] =
                    std::max(pass->pixels[index * 4 + 1], byte(cell.heat()));
            }
            if (MOVEMENT > 0) {
                pass->pixels[index * 4 + 2] = std::max(pass->pixels[index * 4 + 2], byte(MOVEMENT));
                pass->pixels[index * 4 + 3] =
                    std::max(pass->pixels[index * 4 + 3], byte(cell.movement_heat()));
            }
            const auto COL = index % CBurnGround::WIDTH, ROW = index / CBurnGround::WIDTH;
            min_col = std::min(min_col, COL);
            max_col = std::max(max_col, COL);
            min_row = std::min(min_row, ROW);
            max_row = std::max(max_row, ROW);
        }
        for (std::size_t index = 0;
            index < area->particle_count && pass->particle_count < pass->particles.size();
            ++index) {
            const auto &p = area->particles[index];
            const double RADIUS = p.size * 1.1 + 2;
            if (p.position.x + RADIUS < screen.screen.x ||
                p.position.y + RADIUS < screen.screen.y ||
                p.position.x - RADIUS > screen.screen.x + screen.screen.w ||
                p.position.y - RADIUS > screen.screen.y + screen.screen.h)
                continue;
            pass->particles[pass->particle_count++] = CBurnGroundDebris::pack(p, screen.screen);
            draw_extent.include(p.position.x - RADIUS,
                p.position.y - RADIUS,
                p.position.x + RADIUS,
                p.position.y + RADIUS);
            if (p.kind == CBurnGround::Particle::Kind::Paper)
                source_extent.include(p.source.x - RADIUS,
                    p.source.y - RADIUS,
                    p.source.x + RADIUS,
                    p.source.y + RADIUS);
        }
    }
    if (min_col <= max_col) {
        // A linearly sampled texel extends half a cell past its nominal boundary.
        const double CW = screen.screen.w / CBurnGround::WIDTH,
                     CH = screen.screen.h / CBurnGround::HEIGHT;
        draw_extent.include(screen.screen.x + (static_cast<double>(min_col) - .5) * CW - 16,
            screen.screen.y + (static_cast<double>(min_row) - .5) * CH - 16,
            screen.screen.x + (static_cast<double>(max_col) + 1.5) * CW + 16,
            screen.screen.y + (static_cast<double>(max_row) + 1.5) * CH + 16);
    }
    pass->viewport = draw_extent.pixels(screen.screen, screen.scale);
    if (pass->viewport.empty()) return;
    source_extent.include(
        draw_extent.left - 8, draw_extent.top - 8, draw_extent.right + 8, draw_extent.bottom + 8);
    if (!m_impl->owner.ensure_current()) return;
    const CBox BOX{0, 0, screen.screen.w * screen.scale, screen.screen.h * screen.scale};
    pass->wallpaper =
        canvas.capture_wallpaper(BOX, source_extent.pixels(screen.screen, screen.scale));
    if (!pass->wallpaper) return;
    Render::IHyprRenderer::addPassElement(ctx, std::move(pass));
}
