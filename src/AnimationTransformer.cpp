#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

using namespace Render;



CAnimationTransformer::CAnimationTransformer(
    PHLWINDOWREF window, SStyleState state, SStyleResources<StyleModules> &resources)
    : m_window(std::move(window)), m_style(std::move(state)), m_resources(resources) {
}

bool CAnimationTransformer::built_for(hyprimate::config::Style style) const {
    return std::visit(
        [&](const auto &state) {
            using Live = decltype(live_style_for(state));
            return style == Live::KIND && Live::builtFor(state, m_resources.get<typename Live::Module>());
        },
        m_style);
}

void CAnimationTransformer::cancel_opening() {
    std::visit([&](const auto &state) { live_style_for(state).cancelOpening(*this); }, m_style);
}

void CAnimationTransformer::cancel_movement() {
    std::visit([&](const auto &state) { live_style_for(state).cancelMovement(*this); }, m_style);
}

bool CAnimationTransformer::deforming() const {
    return std::visit(
        [&](const auto &state) { return live_style_for(state).deforming(state); }, m_style);
}

bool CAnimationTransformer::active() const {
    const auto WINDOW = m_window.lock();
    if (m_detached || !WINDOW || !WINDOW->mapped() ||
        Fullscreen::controller()->isFullscreen(WINDOW)) {
        return false;
    }
    if (!deforming()) {
        return false;
    }
    const auto BOX = WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return std::isfinite(BOX.x) && std::isfinite(BOX.y) && std::isfinite(BOX.w) &&
           std::isfinite(BOX.h) && !BOX.empty();
}

int CAnimationTransformer::priority() const {
    return 10;
}

bool CAnimationTransformer::blocksDirectScanout() const {
    return active();
}

CBox CAnimationTransformer::transformedExtents(const CBox &current_box) const {
    if (!active()) {
        return current_box;
    }

    return std::visit([&](const auto &state) { return live_extents(state, current_box); }, m_style);
}

CBox CAnimationTransformer::sourceBoxForOutput(
    const CBox &output_box, const CBox &input_box) const {
    // While deforming, the inverse capture must see the full input so no displaced texel is
    // clipped.
    if (active()) {
        return input_box;
    }

    return output_box.intersection(input_box);
}

SWindowTransformBuffer CAnimationTransformer::transform(CRenderContext &ctx,
    const SWindowTransformBuffer &input,
    const SWindowTransformContext &context) {
    if (!active() || context.standalone || context.renderingSnapshot || !context.monitor ||
        !input.framebuffer || !input.framebuffer->getTexture()) {
        return input;
    }

    const double SCALE = context.monitor->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0 || context.currentBox.empty()) {
        return input;
    }
    const double CANVASPADDING = 1.0 / SCALE;
    const CBox OUTPUTCANVAS =
        pixelBoxForLogical(context.outputBox.copy().expand(CANVASPADDING), SCALE);
    const CBox SOURCEBOX = context.currentBox.copy().scale(SCALE);
    const CBox OUTPUTBOX = transformedExtents(context.currentBox).scale(SCALE);
    const CBox LOCALOUTPUTBOX = OUTPUTBOX.copy().translate(-OUTPUTCANVAS.pos());

    if (OUTPUTCANVAS.empty() || SOURCEBOX.empty() || OUTPUTBOX.empty()) {
        return input;
    }
    const auto TEX_SIZE = input.framebuffer->getTexture()->m_size;
    const double TEX_W = TEX_SIZE.x;
    const double TEX_H = TEX_SIZE.y;
    if (!(TEX_W > 0.0) || !(TEX_H > 0.0)) {
        return input;
    }

    const SLiveRenderGeometry GEOMETRY{
        .canvas = OUTPUTCANVAS,
        .source = SOURCEBOX,
        .output = OUTPUTBOX,
        .local_output = LOCALOUTPUTBOX,
        .scale = SCALE,
        .texture_size = {.x = TEX_W, .y = TEX_H},
    };
    return std::visit(
        [&](auto &state) {
            using Module = typename SStyleModuleFor<std::decay_t<decltype(state)>>::Module;
            return render_live(state, ctx, input, context, GEOMETRY, m_resources.get<Module>());
        },
        m_style);
}

void CAnimationTransformer::mark_detached() {
    m_detached = true;
    // A close may have moved a sheet state into its retained record.
    if (!std::visit(
            [&](const auto &state) { return live_style_for(state).available(state); }, m_style)) {
        return;
    }
    cancel_opening();
    cancel_movement();
}
