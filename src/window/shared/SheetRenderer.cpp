#include <cstdint>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>
#include <drm_fourcc.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/IndexedMesh.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/NativeResource.hpp"
#include "gl/Storage.hpp"
#include "perf/Diagnostics.hpp"
#include "perf/LiveProfiler.hpp"
#include "window/shared/SheetRenderer.hpp"

SP<Render::ITexture> CSheetShade::texture() {
    if (!g_pHyprRenderer) return nullptr;
    return m_cache.texture(
        [](std::uint8_t *pixels, int size) {
            return g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
                pixels,
                size * 4,
                Vector2D{size, 1},
                false,
                false);
        },
        [](SP<Render::ITexture> texture) {
            return hyprimate::gl::retain_native(std::move(texture));
        },
        [](const SP<Render::ITexture> &texture,
            const hyprimate::gl::CurrentCompositorContext &current) {
            return hyprimate::gl::retire_replaced_sheet_shade(texture, current);
        });
}

const std::vector<SMeshRenderVertex> &sheet_canvas_quad() {
    static const std::vector<SMeshRenderVertex> QUAD{
        {.x = 0.F, .y = 0.F, .u = 0.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 1.F, .y = 0.F, .u = 1.F, .v = 0.F},
        {.x = 0.F, .y = 1.F, .u = 0.F, .v = 1.F},
        {.x = 1.F, .y = 1.F, .u = 1.F, .v = 1.F},
    };
    return QUAD;
}

SP<Render::IFramebuffer> render_sheet(Render::CRenderContext &ctx, const SSheetDraw &draw) {
    if (!draw.window || (draw.window_vertices == nullptr) || draw.window_vertices->empty() ||
        (!(draw.canvas.x > 0.0) || !(draw.canvas.y > 0.0))) {
        return nullptr;
    }
    const hyprimate::gl::CurrentCompositorContext CONTEXT;
    if (!CONTEXT) return nullptr;

    // Preserve split alpha factors as well as the RGB factors shadowed by Hyprland.
    const bool ENTRY_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entry_src = 0;
    GLint entry_dst = 0;
    GLint entry_src_alpha = 0;
    GLint entry_dst_alpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entry_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &entry_dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &entry_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &entry_dst_alpha);

    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    auto &data = ctx.m_data;

    auto paper_layer = hyprimate::gl::get_work_buffer(ctx, draw.canvas, CONTEXT);
    if (!paper_layer) {
        return nullptr;
    }

    // Requested while paper_layer is held, so the pool cannot return the same buffer twice.
    const bool LIT =
        draw.shade && (draw.shade_vertices != nullptr) && !draw.shade_vertices->empty();
    const auto SHADE_LAYER =
        LIT ? hyprimate::gl::get_work_buffer(ctx, draw.canvas, CONTEXT) : SP<Render::IFramebuffer>{};

    // Binds the framebuffer as the target and clears it under the canvas damage.
    const auto BEGIN_TARGET = [&](const SP<Render::IFramebuffer> &framebuffer) {
        HYPRIMATE_GPU_STOPWATCH("sheet.work-buffer-clear");
        g_pHyprRenderer->bindFB(ctx, framebuffer);
        data.damage = CRegion{0.0, 0.0, draw.canvas.x, draw.canvas.y};
        data.fbSize = draw.canvas;
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    };

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &data.damage;
    texture_data.allowCustomUV = true;
    const hyprimate::gl::NativeMeshSamplers SAMPLERS;

    // Shade layer: no blending, so the frontmost triangle's shade wins each pixel and hidden
    // triangles never tint visible ones.
    if (SHADE_LAYER) {
        BEGIN_TARGET(SHADE_LAYER);
        const bool WAS_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
        Render::GL::g_pHyprOpenGL->blend(false);
        texture_data.a = 1.F;
        HYPRIMATE_GPU_STOPWATCH("sheet.shade-layer");
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, draw.shade, draw.mesh_box, *draw.shade_vertices, texture_data);
        Render::GL::g_pHyprOpenGL->blend(WAS_BLEND);
    }

    // Window layer: triangles are sorted back to front and drawn without blending, so each pixel
    // takes the frontmost triangle only. Paper is opaque: with blending, a translucent window let
    // the layers folded behind show through, and the crumple read as tissue. The window's own
    // translucency against what is behind it is kept.
    // Draws the window layer from the generator's expanded list, or from a gathered copy of its
    // unique vertices when only the indexed form exists (redirect unavailable, compare reference).
    const auto PREPARE_WINDOW = [&] {
        BEGIN_TARGET(paper_layer);
        Render::GL::g_pHyprOpenGL->blend(false);
        texture_data.a = draw.alpha;
    };
    const auto DRAW_WINDOW_EXPANDED = [&] {
        PREPARE_WINDOW();
        if (draw.window_indices == nullptr) {
            Render::GL::g_pHyprOpenGL->renderTextureMesh(
                ctx, draw.window, draw.mesh_box, *draw.window_vertices, texture_data);
            return;
        }
        static thread_local std::vector<SMeshRenderVertex> gathered;
        hyprimate::gl::expand_indexed_mesh(*draw.window_vertices, *draw.window_indices, gathered);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, draw.window, draw.mesh_box, gathered, texture_data);
    };
    const auto DRAW_WINDOW_INDEXED = [&] {
        PREPARE_WINDOW();
        return hyprimate::gl::render_indexed_mesh(
            ctx, draw.window, draw.mesh_box, *draw.window_vertices, *draw.window_indices,
            texture_data);
    };
    {
#ifdef HYPRIMATE_DIAGNOSTICS
        constexpr hyprimate::perf::Key KEY{
            hyprimate::perf::Domain::Window, "sheet", "window-layer"};
        HYPRIMATE_CPU_SCOPE(KEY);
#endif
        HYPRIMATE_GPU_STOPWATCH("window-layer");
        if (draw.window_indices == nullptr) {
            DRAW_WINDOW_EXPANDED();
        }
#ifdef HYPRIMATE_DIAGNOSTICS
        else if (hyprimate::gl::indexed_mesh_mode() ==
                   hyprimate::gl::IndexedMeshMode::Compare) {
            DRAW_WINDOW_EXPANDED();
            const auto EXPANDED_PIXELS = hyprimate::gl::capture_bound_framebuffer(
                static_cast<int>(draw.canvas.x), static_cast<int>(draw.canvas.y));
            if (DRAW_WINDOW_INDEXED()) {
                hyprimate::gl::record_compare(EXPANDED_PIXELS,
                    hyprimate::gl::capture_bound_framebuffer(
                        static_cast<int>(draw.canvas.x), static_cast<int>(draw.canvas.y)),
                    draw.window_vertices->size(),
                    draw.window_indices->size());
            } else {
                DRAW_WINDOW_EXPANDED();
            }
        }
#endif
        else if (!DRAW_WINDOW_INDEXED()) {
            DRAW_WINDOW_EXPANDED();
        }
    }
    Render::GL::g_pHyprOpenGL->blend(true);

    // Tint: DST_ALPHA weights the shade by window coverage, so transparent pixels stay transparent
    // and alpha is preserved.
    if (SHADE_LAYER) {
        Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        texture_data.a = 1.F;
        HYPRIMATE_GPU_STOPWATCH("sheet.tint");
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            SHADE_LAYER->getTexture(),
            CBox{0.0, 0.0, draw.canvas.x, draw.canvas.y},
            sheet_canvas_quad(),
            texture_data);
    }

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    Render::GL::g_pHyprOpenGL->blend(ENTRY_BLEND);
    Render::GL::g_pHyprOpenGL->blendFunc(
        static_cast<GLenum>(entry_src), static_cast<GLenum>(entry_dst));
    glBlendFuncSeparate(static_cast<GLenum>(entry_src),
        static_cast<GLenum>(entry_dst),
        static_cast<GLenum>(entry_src_alpha),
        static_cast<GLenum>(entry_dst_alpha));

    return paper_layer;
}
