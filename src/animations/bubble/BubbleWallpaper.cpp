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
            const auto SOURCE = ctx.m_data.currentFB;
            if (!SOURCE || !SOURCE->getTexture() || (SOURCE->getTexture()->m_texID == 0u) ||
                m_source.empty()) {
                return {};
            }

            // Native projection accounts for framebuffer orientation and the current render target.
            CBox box = m_source;
            ctx.m_data.renderModif.applyToBox(box);
            const auto MATRIX = g_pHyprRenderer->projectBoxToTarget(ctx, box).getMatrix();
            const auto PROJECT = [&](double u, double v) {
                return Vector2D{(0.5 + (0.5 * ((MATRIX[0] * u) + (MATRIX[1] * v) + MATRIX[2]))) *
                                    SOURCE->m_size.x,
                    (0.5 + (0.5 * ((MATRIX[3] * u) + (MATRIX[4] * v) + MATRIX[5]))) *
                        SOURCE->m_size.y};
            };
            const auto ORIGIN = PROJECT(0, 0);
            const auto AXIS_X = PROJECT(1, 0);
            const auto AXIS_Y = PROJECT(0, 1);
            CBox bounds = m_bounds;
            ctx.m_data.renderModif.applyToBox(bounds);
            const auto BOUNDS_MATRIX = g_pHyprRenderer->projectBoxToTarget(ctx, bounds).getMatrix();
            const auto PROJECT_BOUNDS = [&](double u, double v) {
                return Vector2D{(0.5 + (0.5 * ((BOUNDS_MATRIX[0] * u) + (BOUNDS_MATRIX[1] * v) +
                                                  BOUNDS_MATRIX[2]))) *
                                    SOURCE->m_size.x,
                    (0.5 + (0.5 * ((BOUNDS_MATRIX[3] * u) + (BOUNDS_MATRIX[4] * v) +
                                      BOUNDS_MATRIX[5]))) *
                        SOURCE->m_size.y};
            };
            const auto BOUNDS_ORIGIN = PROJECT_BOUNDS(0, 0);
            const auto BOUNDS_X = PROJECT_BOUNDS(1, 0);
            const auto BOUNDS_Y = PROJECT_BOUNDS(0, 1);
            const auto BXY = PROJECT_BOUNDS(1, 1);
            const double LEFT = std::max(
                0.0, std::floor(std::min({BOUNDS_ORIGIN.x, BOUNDS_X.x, BOUNDS_Y.x, BXY.x})) - 1.0);
            const double CLIP_TOP = std::max(
                0.0, std::floor(std::min({BOUNDS_ORIGIN.y, BOUNDS_X.y, BOUNDS_Y.y, BXY.y})) - 1.0);
            const double RIGHT = std::min(SOURCE->m_size.x,
                std::ceil(std::max({BOUNDS_ORIGIN.x, BOUNDS_X.x, BOUNDS_Y.x, BXY.x})) + 1.0);
            const double TOP = std::min(SOURCE->m_size.y,
                std::ceil(std::max({BOUNDS_ORIGIN.y, BOUNDS_X.y, BOUNDS_Y.y, BXY.y})) + 1.0);
            const Vector2D EXTENT{RIGHT - LEFT, TOP - CLIP_TOP};
            if (!(EXTENT.x > 0 && EXTENT.y > 0) || !std::isfinite(EXTENT.x) ||
                !std::isfinite(EXTENT.y)) {
                return {};
            }

            auto &renderer = *Render::GL::g_pHyprOpenGL;
            const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
            const auto STATE = ctx.saveDrawState();
            GLint active = 0;
            GLint texture = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            renderer.setActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            const auto DESTINATION = g_pHyprRenderer->getWorkBuffer(ctx, EXTENT);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
            renderer.setActiveTexture(static_cast<GLenum>(active));
            if (!DESTINATION || !DESTINATION->getTexture() ||
                DESTINATION->getTexture()->m_texID == SOURCE->getTexture()->m_texID) {
                return {};
            }

            const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != 0u;
            renderer.setCapStatus(GL_SCISSOR_TEST, false);
            renderer.bindFramebuffer(GL_READ_FRAMEBUFFER,
                static_cast<Render::GL::CGLFramebuffer *>(SOURCE.get())->getFBID());
            renderer.bindFramebuffer(GL_DRAW_FRAMEBUFFER,
                static_cast<Render::GL::CGLFramebuffer *>(DESTINATION.get())->getFBID());
            glBlitFramebuffer(static_cast<GLint>(LEFT),
                static_cast<GLint>(CLIP_TOP),
                static_cast<GLint>(RIGHT),
                static_cast<GLint>(TOP),
                0,
                0,
                static_cast<GLint>(EXTENT.x),
                static_cast<GLint>(EXTENT.y),
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            renderer.setCapStatus(GL_SCISSOR_TEST, SCISSOR);
            m_wallpaper->origin = (ORIGIN - Vector2D{LEFT, CLIP_TOP}) / EXTENT;
            m_wallpaper->axisX = (AXIS_X - ORIGIN) / EXTENT;
            m_wallpaper->axisY = (AXIS_Y - ORIGIN) / EXTENT;
            m_wallpaper->framebuffer = DESTINATION;
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
