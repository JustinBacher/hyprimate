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
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

using namespace Render;

// The core fills write hyprimate::LiquidVertex straight into Hyprland's mesh vertices: the
// layout must match.
static_assert(sizeof(SMeshRenderVertex) == sizeof(hyprimate::LiquidVertex));
static_assert(offsetof(SMeshRenderVertex, x) == offsetof(hyprimate::LiquidVertex, x));
static_assert(offsetof(SMeshRenderVertex, y) == offsetof(hyprimate::LiquidVertex, y));
static_assert(offsetof(SMeshRenderVertex, u) == offsetof(hyprimate::LiquidVertex, u));
static_assert(offsetof(SMeshRenderVertex, v) == offsetof(hyprimate::LiquidVertex, v));

SStyleState initialStyle(hyprimate::config::Style style,
    hyprimate::MaterialParams liquid,
    hyprimate::ClothMovementParams cloth,
    std::uint64_t seed) {
    return withLiveStyle(style, [&](auto live) { return live.initial(liquid, cloth, seed); });
}

CAnimationTransformer::CAnimationTransformer(PHLWINDOWREF window,
    SStyleState state,
    SP<CSolidFill> solid,
    SP<CPaperShade> shade,
    SP<CSwarmSprites> sprites)
    : m_window(std::move(window)),
      m_style(std::move(state)),
      m_solid(std::move(solid)),
      m_shade(std::move(shade)),
      m_sprites(std::move(sprites)) {
}

bool CAnimationTransformer::builtFor(hyprimate::config::Style style,
    hyprimate::MaterialParams liquid,
    hyprimate::ClothMovementParams cloth) const noexcept {
    return std::visit(
        [&](const auto &state) {
            return liveStyleFor(state).builtFor(state, style, liquid, cloth);
        },
        m_style);
}

void CAnimationTransformer::cancelOpening() {
    std::visit([&](const auto &state) { liveStyleFor(state).cancelOpening(*this); }, m_style);
}

void CAnimationTransformer::cancelMovement() {
    std::visit([&](const auto &state) { liveStyleFor(state).cancelMovement(*this); }, m_style);
}

bool CAnimationTransformer::deforming() const noexcept {
    return std::visit(
        [&](const auto &state) { return liveStyleFor(state).deforming(state); }, m_style);
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

CBox CAnimationTransformer::transformedExtents(const CBox &currentBox) const {
    if (!active()) {
        return currentBox;
    }

    return std::visit([&](const auto &state) { return liveExtents(state, currentBox); }, m_style);
}

CBox CAnimationTransformer::sourceBoxForOutput(const CBox &outputBox, const CBox &inputBox) const {
    // While deforming, the inverse capture must see the full input so no displaced texel is
    // clipped.
    if (active()) {
        return inputBox;
    }

    return outputBox.intersection(inputBox);
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
    if (!(TEX_W > 0.0 && TEX_H > 0.0)) {
        return input;
    }

    const SLiveRenderGeometry GEOMETRY{.canvas = OUTPUTCANVAS,
        .source = SOURCEBOX,
        .output = OUTPUTBOX,
        .localOutput = LOCALOUTPUTBOX,
        .scale = SCALE,
        .textureSize = {.x = TEX_W, .y = TEX_H}};
    return std::visit(
        [&](auto &state) {
            return renderLive(state, ctx, input, context, GEOMETRY, m_solid, m_shade, m_sprites);
        },
        m_style);
}

void CAnimationTransformer::markDetached() {
    m_detached = true;
    // A close may have moved a sheet state into its retained record.
    if (!std::visit(
            [&](const auto &state) { return liveStyleFor(state).available(state); }, m_style)) {
        return;
    }
    cancelOpening();
    cancelMovement();
}
