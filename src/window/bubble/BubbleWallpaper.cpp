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
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "BubbleWallpaper.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/NativeResource.hpp"
#include "gl/RawGlBracket.hpp"
#include "perf/GpuTimer.hpp"

namespace {
    class CBubbleWallpaperPass final : public IPassElement {
      public:
        CBubbleWallpaperPass(SP<SBubbleWallpaper> wallpaper,
            CBox source,
            CBox bounds,
            bool live,
            bool workspace_source)
            : m_wallpaper(std::move(wallpaper)),
              m_source(source),
              m_bounds(bounds),
              m_live(live),
              m_workspace_source(workspace_source) {
        }

        CBubbleWallpaperPass(const CBubbleWallpaperPass &) = delete;
        CBubbleWallpaperPass &operator=(const CBubbleWallpaperPass &) = delete;
        CBubbleWallpaperPass(CBubbleWallpaperPass &&) = delete;
        CBubbleWallpaperPass &operator=(CBubbleWallpaperPass &&) = delete;

        ~CBubbleWallpaperPass() override {
            // A live capture must be refreshed; a discarded capture is not completed.
            if (m_live || !m_wallpaper->framebuffer) {
                m_wallpaper->queued = false;
            }
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            constexpr hyprimate::perf::Key KEY{
                hyprimate::perf::Domain::Window, "bubble", "shared"};
            const hyprimate::perf::CpuScope CPU_PROFILE{KEY, hyprimate::perf::Phase::Draw};
            if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || ctx.m_renderingCapture ||
                ctx.m_renderingSnapshot ||
                g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
                return {};
            }
            if (!m_wallpaper->owner.ensure_current()) return {};
            const auto SOURCE = ctx.m_data.currentFB;
            if (!SOURCE || !SOURCE->getTexture() || (SOURCE->getTexture()->m_texID == 0U) ||
                m_source.empty()) {
                return {};
            }

            // Native projection accounts for framebuffer orientation and the current render target.
            CBox box = m_source;
            ctx.m_data.renderModif.applyToBox(box);
            const auto MATRIX = g_pHyprRenderer->projectBoxToTarget(ctx, box).getMatrix();
            const auto PROJECT = [&](double coord_x, double coord_y) {
                return Vector2D{
                    (0.5 + (0.5 * ((std::get<0>(MATRIX) * coord_x) +
                                      (std::get<1>(MATRIX) * coord_y) + std::get<2>(MATRIX)))) *
                        SOURCE->m_size.x,
                    (0.5 + (0.5 * ((std::get<3>(MATRIX) * coord_x) +
                                      (std::get<4>(MATRIX) * coord_y) + std::get<5>(MATRIX)))) *
                        SOURCE->m_size.y};
            };
            const auto ORIGIN = PROJECT(0, 0);
            const auto AXIS_X = PROJECT(1, 0);
            const auto AXIS_Y = PROJECT(0, 1);
            CBox bounds = m_bounds;
            ctx.m_data.renderModif.applyToBox(bounds);
            const auto BOUNDS_MATRIX = g_pHyprRenderer->projectBoxToTarget(ctx, bounds).getMatrix();
            const auto PROJECT_BOUNDS = [&](double coord_x, double coord_y) {
                return Vector2D{(0.5 + (0.5 * ((std::get<0>(BOUNDS_MATRIX) * coord_x) +
                                                  (std::get<1>(BOUNDS_MATRIX) * coord_y) +
                                                  std::get<2>(BOUNDS_MATRIX)))) *
                                    SOURCE->m_size.x,
                    (0.5 + (0.5 * ((std::get<3>(BOUNDS_MATRIX) * coord_x) +
                                      (std::get<4>(BOUNDS_MATRIX) * coord_y) +
                                      std::get<5>(BOUNDS_MATRIX)))) *
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
            if (!(EXTENT.x > 0) || !(EXTENT.y > 0) || !std::isfinite(EXTENT.x) ||
                !std::isfinite(EXTENT.y)) {
                return {};
            }

            const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
            const auto STATE = ctx.saveDrawState();
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects the GL
            // framebuffer backend.
            const auto SOURCE_ID =
                static_cast<Render::GL::CGLFramebuffer *>(SOURCE.get())->getFBID();
            const hyprimate::gl::CRawGlBracket<1> BRACKET{
                {.viewport_width = static_cast<GLsizei>(SOURCE->m_size.x),
                    .viewport_height = static_cast<GLsizei>(SOURCE->m_size.y),
                    .framebuffer = SOURCE_ID}};
            SP<Render::IFramebuffer> destination;
            if (m_workspace_source) {
                destination = g_pHyprRenderer->createFB("hyprimate workspace bubble underlay");
                if (destination && !hyprimate::gl::allocate_framebuffer(*destination,
                                       static_cast<int>(EXTENT.x),
                                       static_cast<int>(EXTENT.y),
                                       SOURCE->m_drmFormat)) {
                    destination.reset();
                }
                if (destination) {
                    destination->setImageDescription(SOURCE->imageDescription());
                }
            } else {
                destination = hyprimate::gl::get_work_buffer(ctx, EXTENT);
            }
            const auto DESTINATION = destination;
            if (!DESTINATION || !DESTINATION->getTexture() ||
                DESTINATION->getTexture()->m_texID == SOURCE->getTexture()->m_texID) {
                return {};
            }
            // Finish anchoring a plugin-created capture before replacing any old capture
            // or publishing coordinates. Borrowed live workbuffers must not be pinned.
            if (m_workspace_source) {
                destination = hyprimate::gl::retain_native(std::move(destination));
                if (!destination) return {};
            }

            const hyprimate::perf::GpuScope GPU_PROFILE{KEY};
            glBindFramebuffer(GL_READ_FRAMEBUFFER, SOURCE_ID);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects
                // the GL framebuffer backend.
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
            m_wallpaper->origin = (ORIGIN - Vector2D{LEFT, CLIP_TOP}) / EXTENT;
            m_wallpaper->axis_x = (AXIS_X - ORIGIN) / EXTENT;
            m_wallpaper->axis_y = (AXIS_Y - ORIGIN) / EXTENT;
            hyprimate::gl::release_native(m_wallpaper->framebuffer, m_wallpaper->owner);
            m_wallpaper->framebuffer = std::move(destination);
            return {};
        }

        bool needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) override {
            return false;
        }
        bool needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) override {
            return false;
        }
        const char *passName() override {
            return "hyprimate-bubble-wallpaper";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }
        // Reconstruct the entire lower scene, including pixels hidden by opaque windows.
        bool disableSimplification([[maybe_unused]] Render::CRenderContext &ctx) override {
            return true;
        }
        bool requiresFullDamage([[maybe_unused]] Render::CRenderContext &ctx) override {
            return true;
        }

      private:
        SP<SBubbleWallpaper> m_wallpaper;
        CBox m_source;
        CBox m_bounds;
        bool m_live;
        bool m_workspace_source;
    };
}  // namespace

void queue_bubble_wallpaper(Render::CRenderContext &ctx,
    const SP<SBubbleWallpaper> &wallpaper,
    const CBox &source,
    bool live,
    const CBox &bounds,
    hyprimate::animation::PassKind pass) {
    const bool WORKSPACE_CAPTURE = pass == hyprimate::animation::PassKind::WorkspaceSource;
    if (!wallpaper || !g_pHyprRenderer || pass == hyprimate::animation::PassKind::Inert ||
        ctx.m_renderingCapture || ctx.m_renderingSnapshot) {
        return;
    }
    if (!wallpaper->owner.ensure_current()) return;
    // A completed opening snapshot keeps queued set; a live request replaces it. A live pass
    // waiting in this frame has no framebuffer yet and must not be queued a second time.
    if (live && wallpaper->queued && wallpaper->framebuffer) {
        wallpaper->queued = false;
    }
    if (wallpaper->queued) {
        return;
    }
    if (live) {
        hyprimate::gl::release_native(wallpaper->framebuffer, wallpaper->owner);
    }
    if (!ctx.m_data.pMonitor ||
        bounds.copy().intersection(CBox{{}, ctx.m_data.pMonitor->m_transformedSize}).empty()) {
        return;
    }
    wallpaper->queued = true;
    Render::IHyprRenderer::addPassElement(
        ctx, makeUnique<CBubbleWallpaperPass>(wallpaper, source, bounds, live, WORKSPACE_CAPTURE));
}
