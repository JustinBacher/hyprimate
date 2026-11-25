#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "HyprAnimationCanvas.hpp"
#include "gl/IndexedMesh.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/Storage.hpp"
#include "perf/Diagnostics.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

using namespace hyprimate::animation;

PassKind CHyprAnimationCanvas::pass() const {
    if (WorkspaceCaptureScope::for_context(context()) != nullptr) {
        return PassKind::WorkspaceSource;
    }
    if (WorkspaceCaptureScope::is_destination_live(context())) {
        return PassKind::Live;
    }
    if (context().readOnlyEffects() || context().m_renderingCapture ||
        context().m_renderingSnapshot) {
        return PassKind::Inert;
    }
    return PassKind::Live;
}
bool CHyprAnimationCanvas::captures_pixels() const {
    // Both workspace scopes target retained framebuffers. DestinationLive still
    // uses PassKind::Live for normal animation bookkeeping, not privacy authority.
    return context().m_renderingCapture || context().m_renderingSnapshot ||
           context().readOnlyEffects() ||
           WorkspaceCaptureScope::for_context(context()) != nullptr ||
           WorkspaceCaptureScope::is_destination_live(context());
}

bool CHyprAnimationCanvas::exports_output(const Render::CRenderContext &ctx) {
    // Monitor/region shares may copy already-rendered scanout pixels, bypassing
    // the read-only window/workspace draw path and masking only native footprints.
    const auto monitor = ctx.m_data.pMonitor.lock();
    return monitor && (monitor->resources()->shouldKeepMirrorFB() || monitor->needsACopyFB());
}

bool CHyprAnimationCanvas::workspace_switch() const {
    const auto &ctx = context();
    if (WorkspaceCaptureScope::for_context(ctx) ||
        WorkspaceCaptureScope::is_destination_live(ctx)) {
        return WorkspaceCaptureScope::is_workspace_switch(ctx);
    }
    const auto monitor = ctx.m_data.pMonitor.lock();
    for (const auto &state : m_resources->workspace_switches) {
        if (monitor && state.monitor.lock() == monitor) {
            return state.active;
        }
    }
    return false;
}
CHyprAnimationCanvas::Backdrop CHyprAnimationCanvas::capture_wallpaper(
    const CBox &source, const CBox &bounds) {
    if (pass() != PassKind::Live || context().readOnlyEffects() || context().m_renderingCapture ||
        context().m_renderingSnapshot) {
        return {};
    }
    auto backdrop = makeShared<SBubbleWallpaper>();
    queue_bubble_wallpaper(context(), backdrop, source, true, bounds, PassKind::Live);
    return backdrop;
}

hyprimate::Vec2 CHyprAnimationCanvas::image_size(const Image &image) {
    if (!image || !image->getTexture()) {
        return {};
    }
    const auto SIZE = image->getTexture()->m_size;
    return {.x = SIZE.x, .y = SIZE.y};
}

void CHyprAnimationCanvas::composite(const hyprimate::Box &box, float alpha) {
    if (!m_layer || !m_layer->getTexture()) {
        return;
    }
    Render::GL::CHyprOpenGLImpl::STextureRenderData data{};
    data.damage = &context().m_data.damage;
    data.a = alpha;
    data.allowCustomUV = true;
    const hyprimate::gl::NativeMeshSamplers SAMPLERS;
    HYPRIMATE_GPU_STOPWATCH("canvas.composite");
    Render::GL::g_pHyprOpenGL->renderTextureMesh(context(),
        m_layer->getTexture(),
        CBox{box.x, box.y, box.w, box.h},
        sheet_canvas_quad(),
        data);
}

bool CHyprAnimationCanvas::indexed_meshes() {
    return hyprimate::gl::indexed_meshes_enabled();
}

bool CHyprAnimationCanvas::submit_mesh(
    const Mesh<Vertex> &mesh, const Draw<CHyprAnimationCanvas> &draw) {
    if (!*draw.image || !(*draw.image)->getTexture() || mesh.window == nullptr ||
        mesh.window->empty() || draw.canvas_box.w <= 0 || draw.canvas_box.h <= 0) {
        return false;
    }
    const Vector2D SIZE{draw.canvas_box.w, draw.canvas_box.h};
    const CBox OUTPUT{draw.output.x - draw.canvas_box.x,
        draw.output.y - draw.canvas_box.y,
        draw.output.w,
        draw.output.h};
    m_layer = render_sheet(context(),
        SSheetDraw{.window = (*draw.image)->getTexture(),
            .shade = mesh.shade != nullptr ? m_resources->shade.texture() : SP<Render::ITexture>{},
            .canvas = SIZE,
            .mesh_box = OUTPUT,
            .window_vertices = mesh.window,
            .window_indices = mesh.window_indices,
            .shade_vertices = mesh.shade,
            .alpha = mesh.opacity});
    if (!m_layer) {
        return false;
    }
    SP<Render::ITexture> solid;
    if (mesh.under != nullptr && !mesh.under->empty()) {
        m_resources->solid.set_color(CHyprColor{
            mesh.under_color.r, mesh.under_color.g, mesh.under_color.b, mesh.under_color.a});
        solid = m_resources->solid.texture();
    }
    if (!solid && !mesh.clip) {
        return true;
    }
    const auto COMPOSED = hyprimate::gl::get_work_buffer(context(), SIZE);
    if (!COMPOSED) {
        return false;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = context().saveDrawState();
    auto &data = context().m_data;
    {
        HYPRIMATE_GPU_STOPWATCH("canvas.work-buffer-clear");
        g_pHyprRenderer->bindFB(context(), COMPOSED);
        data.damage = CRegion{0.0, 0.0, SIZE.x, SIZE.y};
        data.fbSize = SIZE;
        g_pHyprRenderer->setProjectionType(context(), Render::RPT_EXPORT);
        g_pHyprRenderer->draw(context(), CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    }
    if (mesh.clip) {
        const auto &clip = *mesh.clip;
        const CBox CLIP{clip.x - draw.canvas_box.x, clip.y - draw.canvas_box.y, clip.w, clip.h};
        data.damage = CRegion{CLIP.intersection(CBox{0, 0, SIZE.x, SIZE.y})};
    }
    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &data.damage;
    texture_data.allowCustomUV = true;
    const hyprimate::gl::NativeMeshSamplers SAMPLERS;
    if (solid) {
        texture_data.a = mesh.opacity;
        const CBox UNDER{mesh.under_output.x - draw.canvas_box.x,
            mesh.under_output.y - draw.canvas_box.y,
            mesh.under_output.w,
            mesh.under_output.h};
        HYPRIMATE_GPU_STOPWATCH("canvas.compose-under");
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            context(), solid, UNDER, *mesh.under, texture_data);
    }
    texture_data.a = 1.F;
    {
        HYPRIMATE_GPU_STOPWATCH("canvas.compose-clip");
        Render::GL::g_pHyprOpenGL->renderTextureMesh(context(),
            m_layer->getTexture(),
            CBox{0, 0, SIZE.x, SIZE.y},
            sheet_canvas_quad(),
            texture_data);
    }
    m_layer = COMPOSED;
    return true;
}
