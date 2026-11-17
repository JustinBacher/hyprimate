#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "BubbleWallpaper.hpp"

namespace {
    class CBubbleWallpaperPass final : public IPassElement {
      public:
        CBubbleWallpaperPass(SP<SBubbleWallpaper> wallpaper, CBox source, CBox bounds, bool live)
            : m_wallpaper(std::move(wallpaper)), m_source(source), m_bounds(bounds), m_live(live) {
        }

        ~CBubbleWallpaperPass() override {
            // A live capture must be refreshed; a discarded capture is not completed.
            if (m_live || !m_wallpaper->framebuffer) {
                m_wallpaper->queued = false;
            }
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || ctx.readOnlyEffects() ||
                ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
                g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
                return {};
            }
            const auto source = ctx.m_data.currentFB;
            if (!source || !source->getTexture() || (source->getTexture()->m_texID == 0u) ||
                m_source.empty()) {
                return {};
            }

            // Native projection accounts for framebuffer orientation and the current render target.
            CBox box = m_source;
            ctx.m_data.renderModif.applyToBox(box);
            const auto matrix = g_pHyprRenderer->projectBoxToTarget(ctx, box).getMatrix();
            const auto project = [&](double u, double v) {
                return Vector2D{(0.5 + (0.5 * ((matrix[0] * u) + (matrix[1] * v) + matrix[2]))) *
                                    source->m_size.x,
                    (0.5 + (0.5 * ((matrix[3] * u) + (matrix[4] * v) + matrix[5]))) *
                        source->m_size.y};
            };
            const auto p0 = project(0, 0);
            const auto px = project(1, 0);
            const auto py = project(0, 1);
            CBox bounds = m_bounds;
            ctx.m_data.renderModif.applyToBox(bounds);
            const auto boundsMatrix = g_pHyprRenderer->projectBoxToTarget(ctx, bounds).getMatrix();
            const auto projectBounds = [&](double u, double v) {
                return Vector2D{(0.5 + (0.5 * ((boundsMatrix[0] * u) + (boundsMatrix[1] * v) +
                                                  boundsMatrix[2]))) *
                                    source->m_size.x,
                    (0.5 +
                        (0.5 * ((boundsMatrix[3] * u) + (boundsMatrix[4] * v) + boundsMatrix[5]))) *
                        source->m_size.y};
            };
            const auto b0 = projectBounds(0, 0);
            const auto bx = projectBounds(1, 0);
            const auto by = projectBounds(0, 1);
            const auto bxy = projectBounds(1, 1);
            const double x = std::max(0.0, std::floor(std::min({b0.x, bx.x, by.x, bxy.x})) - 1.0);
            const double y = std::max(0.0, std::floor(std::min({b0.y, bx.y, by.y, bxy.y})) - 1.0);
            const double right =
                std::min(source->m_size.x, std::ceil(std::max({b0.x, bx.x, by.x, bxy.x})) + 1.0);
            const double top =
                std::min(source->m_size.y, std::ceil(std::max({b0.y, bx.y, by.y, bxy.y})) + 1.0);
            const Vector2D extent{right - x, top - y};
            if (!(extent.x > 0 && extent.y > 0) || !std::isfinite(extent.x) ||
                !std::isfinite(extent.y)) {
                return {};
            }

            auto &gl = *Render::GL::g_pHyprOpenGL;
            const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
            const auto state = ctx.saveDrawState();
            GLint active = 0;
            GLint texture = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            gl.setActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            const auto destination = g_pHyprRenderer->getWorkBuffer(ctx, extent);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
            gl.setActiveTexture(static_cast<GLenum>(active));
            if (!destination || !destination->getTexture() ||
                destination->getTexture()->m_texID == source->getTexture()->m_texID) {
                return {};
            }

            const bool scissor = glIsEnabled(GL_SCISSOR_TEST) != 0u;
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
        // Reconstruct the entire lower scene, including pixels hidden by opaque windows.
        bool disableSimplification(Render::CRenderContext &) override {
            return true;
        }
        bool requiresFullDamage(Render::CRenderContext &) override {
            return true;
        }

      private:
        SP<SBubbleWallpaper> m_wallpaper;
        CBox m_source;
        CBox m_bounds;
        bool m_live;
    };
}  // namespace

void queueBubbleWallpaper(Render::CRenderContext &ctx,
    const SP<SBubbleWallpaper> &wallpaper,
    const CBox &source,
    bool live,
    const CBox &bounds) {
    if (!wallpaper || !g_pHyprRenderer || ctx.readOnlyEffects() || ctx.m_renderingCapture ||
        ctx.m_renderingSnapshot || ctx.m_mode != Render::RENDER_MODE_NORMAL) {
        return;
    }
    // A completed opening snapshot keeps queued set; a live request replaces it. A live pass
    // waiting in this frame has no framebuffer yet and must not be queued a second time.
    if (live && wallpaper->queued && wallpaper->framebuffer) {
        wallpaper->queued = false;
    }
    if (wallpaper->queued) {
        return;
    }
    if (live) {
        wallpaper->framebuffer.reset();
    }
    if (!ctx.m_data.pMonitor ||
        bounds.copy().intersection(CBox{{}, ctx.m_data.pMonitor->m_transformedSize}).empty()) {
        return;
    }
    wallpaper->queued = true;
    Render::IHyprRenderer::addPassElement(
        ctx, makeUnique<CBubbleWallpaperPass>(wallpaper, source, bounds, live));
}
