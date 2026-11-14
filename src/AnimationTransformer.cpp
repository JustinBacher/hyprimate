#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

using namespace Render;
using liquid_glass::EdgeTransition;
using liquid_glass::LiquidMesh;
using liquid_glass::LiquidPose;
using liquid_glass::Vec2;


// The core fills write liquid_glass::LiquidVertex straight into Hyprland's mesh vertices: the layout must match.
static_assert(sizeof(SMeshRenderVertex) == sizeof(liquid_glass::LiquidVertex));
static_assert(offsetof(SMeshRenderVertex, x) == offsetof(liquid_glass::LiquidVertex, x));
static_assert(offsetof(SMeshRenderVertex, y) == offsetof(liquid_glass::LiquidVertex, y));
static_assert(offsetof(SMeshRenderVertex, u) == offsetof(liquid_glass::LiquidVertex, u));
static_assert(offsetof(SMeshRenderVertex, v) == offsetof(liquid_glass::LiquidVertex, v));

SStyleState initialStyle(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                         liquid_glass::ClothMovementParams cloth, std::uint64_t seed) {
    return withLiveStyle(style, [&](auto live) { return live.initial(liquid, cloth, seed); });
}

CAnimationTransformer::CAnimationTransformer(PHLWINDOWREF window, SStyleState state, SP<CSolidFill> solid,
                                                 SP<CPaperShade> shade, SP<CSwarmSprites> sprites)
    : m_window(std::move(window)), m_style(std::move(state)), m_solid(std::move(solid)), m_shade(std::move(shade)),
      m_sprites(std::move(sprites)) {}

bool CAnimationTransformer::builtFor(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                                       liquid_glass::ClothMovementParams cloth) const noexcept {
    return std::visit([&](const auto &state) { return liveStyleFor(state).builtFor(*this, style, liquid, cloth); },
                      m_style);
}


void CAnimationTransformer::cancelOpening() {
    std::visit([&](const auto &state) { liveStyleFor(state).cancelOpening(*this); }, m_style);
}

void CAnimationTransformer::cancelMovement() {
    std::visit([&](const auto &state) { liveStyleFor(state).cancelMovement(*this); }, m_style);
}













bool CAnimationTransformer::deforming() const noexcept {
    return std::visit([&](const auto &state) { return liveStyleFor(state).deforming(*this); }, m_style);
}

bool CAnimationTransformer::active() const {
    const auto window = m_window.lock();
    if (m_detached || !window || !window->mapped() || Fullscreen::controller()->isFullscreen(window))
        return false;
    if (!deforming())
        return false;
    const auto box = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && !box.empty();
}

int CAnimationTransformer::priority() const { return 10; }

bool CAnimationTransformer::blocksDirectScanout() const { return active(); }

CBox CAnimationTransformer::transformedExtents(const CBox &currentBox) const {
    if (!active())
        return currentBox;

    return std::visit([&](const auto &state) { return liveExtents(state, currentBox); }, m_style);
}

CBox CAnimationTransformer::sourceBoxForOutput(const CBox &outputBox, const CBox &inputBox) const {
    // While deforming, the inverse capture must see the full input so no displaced texel is clipped.
    if (active())
        return inputBox;

    return outputBox.intersection(inputBox);
}

SWindowTransformBuffer CAnimationTransformer::transform(CRenderContext &ctx, const SWindowTransformBuffer &in,
                                                          const SWindowTransformContext &context) {
    if (!active() || context.standalone || context.renderingSnapshot || !context.monitor || !in.framebuffer ||
        !in.framebuffer->getTexture())
        return in;

    const double SCALE = context.monitor->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0 || context.currentBox.empty())
        return in;
    const double CANVASPADDING = 1.0 / SCALE;
    const CBox   OUTPUTCANVAS = pixelBoxForLogical(context.outputBox.copy().expand(CANVASPADDING), SCALE);
    const CBox   SOURCEBOX = context.currentBox.copy().scale(SCALE);
    const CBox   OUTPUTBOX = transformedExtents(context.currentBox).scale(SCALE);
    const CBox   LOCALOUTPUTBOX = OUTPUTBOX.copy().translate(-OUTPUTCANVAS.pos());

    if (OUTPUTCANVAS.empty() || SOURCEBOX.empty() || OUTPUTBOX.empty())
        return in;
    const auto   texSize = in.framebuffer->getTexture()->m_size;
    const double texW = texSize.x;
    const double texH = texSize.y;
    if (!(texW > 0.0 && texH > 0.0))
        return in;

    const SLiveRenderGeometry geometry{OUTPUTCANVAS, SOURCEBOX, OUTPUTBOX, LOCALOUTPUTBOX, SCALE, {texW, texH}};
    return std::visit(
        [&](auto &state) { return renderLive(state, ctx, in, context, geometry, m_solid, m_shade, m_sprites); },
        m_style);
}

void CAnimationTransformer::markDetached() {
    m_detached = true;
    // A close may have moved a sheet state into its retained record.
    if (!std::visit([&](const auto &state) { return liveStyleFor(state).available(*this); }, m_style))
        return;
    cancelOpening();
    cancelMovement();
}
