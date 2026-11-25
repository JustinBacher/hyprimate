#include <utility>

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "WindowAnimationPass.hpp"
#include "gl/NativeResource.hpp"
#include "gl/Storage.hpp"
#include "window/WindowAnimations.hpp"
#include "window/shared/SheetRenderer.hpp"
#include "window/Profiling.hpp"
#include "perf/GpuTimer.hpp"
#include "perf/Diagnostics.hpp"

using namespace hyprimate::animation;

CWindowAnimationPass::CWindowAnimationPass(SData data) : m_data(std::move(data)) {
}

CWindowAnimationPass::~CWindowAnimationPass() {
    hyprimate::gl::release_native(m_data.image, m_data.image_owner);
}

std::vector<UP<IPassElement>> CWindowAnimationPass::draw(Render::CRenderContext &ctx) {
    if (!m_data.image_owner.ensure_current()) {
        if (m_data.close_guard) { m_data.close_guard->physical_failure(ctx); }
        return {};
    }
    const auto valid = [&] {
        return (!m_data.retained_tail || !m_data.no_screen_share ||
                   !CHyprAnimationCanvas::exports_output(ctx)) &&
               (!m_data.close_guard || m_data.close_guard->valid(ctx));
    };
    with_guarded_display(valid, [&](const auto &guard_display) {
        if (m_data.animations == nullptr || m_data.resources == nullptr || !m_data.image ||
            !m_data.animations->active(m_data.handle)) { return; }
        const auto PROFILE_KEY =
            profile_key(m_data.handle.style, m_data.retained_tail ? "shared" : "close");
        const hyprimate::perf::GpuScope GPU_PROFILE{PROFILE_KEY};
        HYPRIMATE_GPU_STOPWATCH("style.outer-close");
        CHyprAnimationCanvas canvas{ctx, *m_data.resources};
        const bool shader = m_data.handle.style == hyprimate::config::Style::Shader;
        if (shader && !m_data.blur) canvas.allow_direct();
        const Draw<CHyprAnimationCanvas> DRAW{.canvas = &canvas, .image = &m_data.image,
            .source = m_data.source, .output = m_data.output, .canvas_box = m_data.canvas_box,
            .image_origin = m_data.image_origin, .image_size = m_data.image_size,
            .scale = m_data.scale, .alpha = m_data.alpha, .real_pass = true, .monitor = m_data.monitor,
            .frame = m_data.frame};
        CHyprAnimationCanvas matte{ctx, *m_data.resources, true,
            m_data.blur_rounding, m_data.blur_rounding_power};
        matte.set_blur_box(m_data.blur_box);
        const auto composite = [&] {
            if (canvas.presented_directly()) {
                if (canvas.pass() == PassKind::Live && !canvas.captures_pixels()) {
                    hyprimate::perf::touch(PROFILE_KEY, m_data.monitor);
                }
                if (m_data.close_guard) { m_data.close_guard->physical_presented(ctx, true); }
                return;
            }
            if (!canvas.layer()) {
                if (m_data.close_guard) { m_data.close_guard->physical_presented(ctx, false); }
                return;
            }
            const auto &box = m_data.canvas_box;
            const CBox BOX{box.x, box.y, box.w, box.h};
            const hyprimate::gl::NativeMeshSamplers SAMPLERS;
            if (m_data.blur) {
                CTexPassElement::SRenderData texture_data;
                texture_data.tex = canvas.layer()->getTexture();
                texture_data.box = BOX;
                texture_data.a = m_data.alpha;
                texture_data.damage = m_data.damage;
                texture_data.useProvidedDamage = true;
                texture_data.blur = true;
                texture_data.forceBlurBlend = true;
                texture_data.blurShapeInvalid = true;
                texture_data.liveBlurOverride = m_data.blur_live;
                texture_data.blurPatternBox = BOX;
                if (shader) texture_data.blurA = m_data.blur_alpha;
                texture_data.blurAlphaMatte = matte.layer()->getTexture();
                texture_data.discardMode = 0;
                HYPRIMATE_GPU_STOPWATCH("pass.blur-composite");
                g_pHyprRenderer->draw(ctx, texture_data, m_data.damage);
            } else {
                Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
                texture_data.damage = &m_data.damage;
                texture_data.a = m_data.alpha;
                texture_data.allowCustomUV = true;
                HYPRIMATE_GPU_STOPWATCH("canvas.final-mesh");
                Render::GL::g_pHyprOpenGL->renderTextureMesh(
                    ctx, canvas.layer()->getTexture(), BOX, sheet_canvas_quad(), texture_data);
            }
            if (canvas.pass() == PassKind::Live && !canvas.captures_pixels()) {
                hyprimate::perf::touch(PROFILE_KEY, m_data.monitor);
            }
            if (m_data.close_guard) { m_data.close_guard->physical_presented(ctx, true); }
        };
        if (m_data.retained_tail) {
            m_data.animations->display_tail(m_data.handle, DRAW, m_data.no_screen_share, [&] { guard_display(composite); });
        } else if (m_data.animations->draw(m_data.handle, DRAW)) {
            if (m_data.blur) {
                auto mask_draw = DRAW;
                mask_draw.canvas = &matte;
                mask_draw.real_pass = false;
                if (!m_data.animations->draw(m_data.handle, mask_draw) || !matte.layer()) {
                    if (m_data.close_guard) { m_data.close_guard->physical_failure(ctx); }
                    return;
                }
            }
            guard_display(composite);
        } else if (m_data.close_guard) {
            m_data.close_guard->physical_failure(ctx);
        }
    });
    return {};
}

bool CWindowAnimationPass::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.blur && m_data.blur_live;
}
bool CWindowAnimationPass::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.blur && !m_data.blur_live;
}
const char *CWindowAnimationPass::passName() {
    return "hyprimate-window-animation";
}
ePassElementType CWindowAnimationPass::type() {
    return EK_CUSTOM;
}
std::optional<CBox> CWindowAnimationPass::boundingBox(
    [[maybe_unused]] Render::CRenderContext &ctx) {
    const auto &box = m_data.canvas_box;
    return CBox{box.x, box.y, box.w, box.h}.scale(1.0 / m_data.scale).round();
}
CRegion CWindowAnimationPass::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
