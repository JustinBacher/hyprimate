#include "WindowAnimationPass.hpp"

#include <utility>

#include <hyprland/src/render/OpenGL.hpp>

#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimations.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "gl/Storage.hpp"
#include "gl/NativeResource.hpp"

using namespace hyprimate::animation;

CWindowAnimationPass::CWindowAnimationPass(SData data) : m_data(std::move(data)) {}

CWindowAnimationPass::~CWindowAnimationPass() {
    hyprimate::gl::release_native(m_data.image, m_data.image_owner);
}

std::vector<UP<IPassElement>> CWindowAnimationPass::draw(Render::CRenderContext &ctx) {
    if (!m_data.image_owner.ensure_current()) return {};
    const auto valid = [&] { return !m_data.close_guard || m_data.close_guard->valid(ctx); };
    with_guarded_display(valid, [&](const auto &guard_display) {
        if (m_data.animations == nullptr || m_data.resources == nullptr || !m_data.image ||
            !m_data.animations->active(m_data.handle)) { return; }
        CHyprAnimationCanvas canvas{ctx, *m_data.resources};
        const Draw<CHyprAnimationCanvas> DRAW{.canvas = &canvas, .image = &m_data.image,
            .source = m_data.source, .output = m_data.output, .canvas_box = m_data.canvas_box,
            .image_origin = m_data.image_origin, .image_size = m_data.image_size,
            .scale = m_data.scale, .alpha = m_data.alpha, .real_pass = true, .monitor = m_data.monitor,
            .frame = m_data.frame};
        const auto composite = [&] {
            if (!canvas.layer()) { return; }
            Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
            texture_data.damage = &m_data.damage;
            texture_data.a = m_data.alpha;
            texture_data.allowCustomUV = true;
            const auto &box = m_data.canvas_box;
            const hyprimate::gl::NativeMeshSamplers SAMPLERS;
            Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, canvas.layer()->getTexture(),
                CBox{box.x, box.y, box.w, box.h}, sheet_canvas_quad(), texture_data);
        };
        if (m_data.retained_tail) {
            m_data.animations->display_tail(m_data.handle, DRAW, m_data.no_screen_share, [&] { guard_display(composite); });
        } else if (m_data.animations->draw(m_data.handle, DRAW)) {
            guard_display(composite);
        }
    });
    return {};
}

bool CWindowAnimationPass::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) { return false; }
bool CWindowAnimationPass::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) { return false; }
const char *CWindowAnimationPass::passName() { return "hyprimate-window-animation"; }
ePassElementType CWindowAnimationPass::type() { return EK_CUSTOM; }
std::optional<CBox> CWindowAnimationPass::boundingBox([[maybe_unused]] Render::CRenderContext &ctx) {
    const auto &box = m_data.canvas_box;
    return CBox{box.x, box.y, box.w, box.h}.scale(1.0 / m_data.scale).round();
}
CRegion CWindowAnimationPass::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) { return {}; }
