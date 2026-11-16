#include <algorithm>
#include <cmath>
#include <utility>

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "BubbleWallpaper.hpp"

namespace {
    class CBubbleWallpaperPass final : public IPassElement {
      public:
        CBubbleWallpaperPass(SP<SBubbleWallpaper> wallpaper, CBox source)
            : m_wallpaper(std::move(wallpaper)), m_source(source) {
        }

        ~CBubbleWallpaperPass() override {
            // A discarded pass or an unavailable target is not a completed capture.
            if (!m_wallpaper->framebuffer) m_wallpaper->queued = false;
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || ctx.readOnlyEffects() ||
                ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
                g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL)
                return {};
            const auto source = ctx.m_data.currentFB;
            if (!source || !source->getTexture() || !source->getTexture()->m_texID ||
                m_source.empty())
                return {};

            // Native projection accounts for framebuffer orientation and the current render target.
            CBox box = m_source;
            ctx.m_data.renderModif.applyToBox(box);
            const auto matrix = g_pHyprRenderer->projectBoxToTarget(ctx, box).getMatrix();
            const auto project = [&](double u, double v) {
                return Vector2D{
                    (0.5 + 0.5 * (matrix[0] * u + matrix[1] * v + matrix[2])) * source->m_size.x,
                    (0.5 + 0.5 * (matrix[3] * u + matrix[4] * v + matrix[5])) * source->m_size.y};
            };
            const auto p0 = project(0, 0), px = project(1, 0), py = project(0, 1),
                       pxy = project(1, 1);
            const double x = std::max(0.0, std::floor(std::min({p0.x, px.x, py.x, pxy.x})) - 1.0);
            const double y = std::max(0.0, std::floor(std::min({p0.y, px.y, py.y, pxy.y})) - 1.0);
            const double right =
                std::min(source->m_size.x, std::ceil(std::max({p0.x, px.x, py.x, pxy.x})) + 1.0);
            const double top =
                std::min(source->m_size.y, std::ceil(std::max({p0.y, px.y, py.y, pxy.y})) + 1.0);
            const Vector2D extent{right - x, top - y};
            if (!(extent.x > 0 && extent.y > 0) || !std::isfinite(extent.x) ||
                !std::isfinite(extent.y))
                return {};

            auto &gl = *Render::GL::g_pHyprOpenGL;
            Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
            auto state = ctx.saveDrawState();
            GLint active, texture;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            gl.setActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            const auto destination = g_pHyprRenderer->getWorkBuffer(ctx, extent);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
            gl.setActiveTexture(static_cast<GLenum>(active));
            if (!destination || !destination->getTexture() ||
                destination->getTexture()->m_texID == source->getTexture()->m_texID)
                return {};

            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            gl.setCapStatus(GL_SCISSOR_TEST, false);
            gl.bindFramebuffer(GL_READ_FRAMEBUFFER,
                static_cast<Render::GL::CGLFramebuffer *>(source.get())->getFBID());
            gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER,
                static_cast<Render::GL::CGLFramebuffer *>(destination.get())->getFBID());
            glBlitFramebuffer(static_cast<GLint>(x),
                static_cast<GLint>(y),
                static_cast<GLint>(right),
                static_cast<GLint>(top),
                0,
                0,
                static_cast<GLint>(extent.x),
                static_cast<GLint>(extent.y),
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            gl.setCapStatus(GL_SCISSOR_TEST, scissor);
            m_wallpaper->origin = (p0 - Vector2D{x, y}) / extent;
            m_wallpaper->axisX = (px - p0) / extent;
            m_wallpaper->axisY = (py - p0) / extent;
            m_wallpaper->framebuffer = destination;
            return {};
        }

        bool needsLiveBlur(Render::CRenderContext &) override {
            return false;
        }
        bool needsPrecomputeBlur(Render::CRenderContext &) override {
            return false;
        }
        const char *passName() override {
            return "hyprimate-bubble-wallpaper";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }
        // Exactly one capture frame: redraw even wallpaper hidden by opaque windows, rather than
        // copying stale pixels.
        bool disableSimplification(Render::CRenderContext &) override {
            return true;
        }
        bool requiresFullDamage(Render::CRenderContext &) override {
            return true;
        }

      private:
        SP<SBubbleWallpaper> m_wallpaper;
        CBox m_source;
    };
}  // namespace

void queueBubbleWallpaper(
    Render::CRenderContext &ctx, const SP<SBubbleWallpaper> &wallpaper, const CBox &source) {
    if (!wallpaper || wallpaper->queued || ctx.readOnlyEffects() || ctx.m_renderingCapture ||
        ctx.m_renderingSnapshot || ctx.m_mode != Render::RENDER_MODE_NORMAL)
        return;
    if (!ctx.m_data.pMonitor ||
        source.copy().intersection(CBox{{}, ctx.m_data.pMonitor->m_transformedSize}).empty())
        return;
    wallpaper->queued = true;
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubbleWallpaperPass>(wallpaper, source));
}
