#include "LiquidGlassTransformer.hpp"

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

namespace {
constexpr double EXTENTS_PADDING = 4.0;
} // namespace

// The core fills write liquid_glass::LiquidVertex straight into Hyprland's mesh vertices: the layout must match.
static_assert(sizeof(SMeshRenderVertex) == sizeof(liquid_glass::LiquidVertex));
static_assert(offsetof(SMeshRenderVertex, x) == offsetof(liquid_glass::LiquidVertex, x));
static_assert(offsetof(SMeshRenderVertex, y) == offsetof(liquid_glass::LiquidVertex, y));
static_assert(offsetof(SMeshRenderVertex, u) == offsetof(liquid_glass::LiquidVertex, u));
static_assert(offsetof(SMeshRenderVertex, v) == offsetof(liquid_glass::LiquidVertex, v));

CLiquidGlassTransformer::CLiquidGlassTransformer(PHLWINDOWREF window, SStyleState state, SP<CSolidFill> solid,
                                                 SP<CPaperShade> shade)
    : m_window(std::move(window)), m_style(std::move(state)), m_solid(std::move(solid)), m_shade(std::move(shade)) {}

bool CLiquidGlassTransformer::builtFor(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                                       liquid_glass::ClothMovementParams cloth) const noexcept {
    switch (style) {
    case liquid_glass::config::Style::Paper:
        return std::holds_alternative<SPaperStyle>(m_style);
    case liquid_glass::config::Style::Cloth: {
        const auto *state = std::get_if<SClothStyle>(&m_style);
        return state && state->movement == cloth;
    }
    case liquid_glass::config::Style::Liquid: {
        const auto *state = std::get_if<SLiquidStyle>(&m_style);
        return state && state->mesh.params() == liquid;
    }
    }
    return false;
}

void CLiquidGlassTransformer::beginOpen(const liquid_glass::EdgeFrame        &frame,
                                        const liquid_glass::TransitionParams &params, double strength,
                                        Vec2 captureSize) {
    auto &state = liquid();
    state.transition.begin(liquid_glass::Transition::Open, frame, params, strength);
    state.boxDriven = state.transition.active();
    state.warped = false;
    if (const auto window = m_window.lock(); window && state.boxDriven)
        state.goalBody = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    refreshPose(captureSize);
}

void CLiquidGlassTransformer::cancelOpening() {
    if (std::holds_alternative<SLiquidStyle>(m_style)) {
        restoreBox();
        auto &state = liquid();
        state.transition.reset();
        state.boxDriven = false;
        state.displacementGain = 1.0;
        state.grid.invalidate();
        return;
    }
    if (auto *paper = std::get_if<SPaperStyle>(&m_style); paper && paper->crumple.active())
        paper->crumple.reset();
    else if (clothOpening())
        std::get<SClothStyle>(m_style).cloth.reset();
}

void CLiquidGlassTransformer::cancelMovement() {
    if (std::holds_alternative<SLiquidStyle>(m_style)) {
        liquid().mesh.reset();
        liquid().grid.invalidate();
        return;
    }
    if (clothMoving()) {
        std::get<SClothStyle>(m_style).cloth.reset();
        refreshSheetFrame(m_style);
    }
}

void CLiquidGlassTransformer::beginPaperOpen(const liquid_glass::PaperParams &params, std::uint64_t seed,
                                             Vec2 captureSize) {
    auto *paper = std::get_if<SPaperStyle>(&m_style);
    if (!paper)
        return;
    paper->crumple.reset();
    paper->crumple.begin(liquid_glass::Transition::Open, params, seed);
    paper->sheet.captureSize = captureSize;
    refreshSheetFrame(m_style);
}

void CLiquidGlassTransformer::beginClothOpen(const liquid_glass::ClothParams &params, std::uint64_t seed,
                                             const liquid_glass::Box           &window,
                                             std::span<const liquid_glass::Box> obstacles) {
    auto *cloth = std::get_if<SClothStyle>(&m_style);
    if (!cloth)
        return;
    cloth->cloth.reset();
    cloth->cloth.begin(liquid_glass::Transition::Open, params, seed, window, obstacles, std::nullopt);
    cloth->sheet.captureSize = {window.w, window.h};
    refreshSheetFrame(m_style);
}

void CLiquidGlassTransformer::beginClothMovement(std::uint64_t seed, const liquid_glass::Box &window, Vec2 grabUV) {
    auto *cloth = std::get_if<SClothStyle>(&m_style);
    if (!cloth)
        return;
    // An interrupted cloth opening keeps its displayed pose; the movement model inherits it.
    cloth->cloth.beginMovement(cloth->movement, seed, window, grabUV);
    cloth->sheet.captureSize = {window.w, window.h};
    refreshSheetFrame(m_style);
}

void CLiquidGlassTransformer::updateClothMovement(const liquid_glass::Box &window, double elapsedSeconds) {
    auto *cloth = std::get_if<SClothStyle>(&m_style);
    if (!cloth)
        return;
    cloth->cloth.updateMovement(window, elapsedSeconds);
    cloth->sheet.captureSize = {window.w, window.h};
    refreshSheetFrame(m_style);
}

bool CLiquidGlassTransformer::clothMoving() const noexcept {
    const auto *cloth = std::get_if<SClothStyle>(&m_style);
    return cloth && cloth->cloth.moving();
}

bool CLiquidGlassTransformer::clothOpening() const noexcept {
    const auto *cloth = std::get_if<SClothStyle>(&m_style);
    return cloth && cloth->cloth.active() && !cloth->cloth.moving();
}

void CLiquidGlassTransformer::advanceSheet(double elapsed, Vec2 captureSize) {
    SSheetBuffers *sheet = sheetBuffersOf(m_style);
    if (!sheet || !sheetActive())
        return;
    sheet->captureSize = captureSize;
    advanceSheetStyle(m_style, elapsed);
    refreshSheetFrame(m_style);
}

SSheetBuffers *sheetBuffersOf(SStyleState &style) noexcept {
    if (auto *paper = std::get_if<SPaperStyle>(&style))
        return &paper->sheet;
    if (auto *cloth = std::get_if<SClothStyle>(&style))
        return &cloth->sheet;
    return nullptr;
}

const SSheetBuffers *sheetBuffersOf(const SStyleState &style) noexcept {
    if (const auto *paper = std::get_if<SPaperStyle>(&style))
        return &paper->sheet;
    if (const auto *cloth = std::get_if<SClothStyle>(&style))
        return &cloth->sheet;
    return nullptr;
}

void advanceSheetStyle(SStyleState &style, double elapsed) {
    if (auto *paper = std::get_if<SPaperStyle>(&style))
        paper->crumple.advance(elapsed);
    else if (auto *cloth = std::get_if<SClothStyle>(&style))
        cloth->cloth.advance(elapsed);
}

void refreshSheetFrame(SStyleState &style) {
    if (auto *paper = std::get_if<SPaperStyle>(&style))
        paper->crumple.frame(paper->sheet.captureSize, *paper->sheet.frame);
    else if (auto *cloth = std::get_if<SClothStyle>(&style))
        cloth->cloth.frame(cloth->sheet.captureSize, *cloth->sheet.frame);
}

SStyleState CLiquidGlassTransformer::takeStyle() noexcept { return std::exchange(m_style, SLiquidStyle{}); }

bool CLiquidGlassTransformer::sheetActive() const noexcept {
    if (const auto *paper = std::get_if<SPaperStyle>(&m_style))
        return paper->crumple.active();
    if (const auto *cloth = std::get_if<SClothStyle>(&m_style))
        return cloth->cloth.active();
    return false;
}

double CLiquidGlassTransformer::sheetOpacity() const noexcept {
    if (const auto *paper = std::get_if<SPaperStyle>(&m_style))
        return paper->crumple.opacity();
    if (const auto *cloth = std::get_if<SClothStyle>(&m_style))
        return cloth->cloth.opacity();
    return 0.0;
}

bool CLiquidGlassTransformer::sheetShaded() const noexcept {
    if (const auto *paper = std::get_if<SPaperStyle>(&m_style))
        return paper->crumple.shaded();
    if (const auto *cloth = std::get_if<SClothStyle>(&m_style))
        return cloth->cloth.shaded();
    return false;
}

void CLiquidGlassTransformer::restoreBox() {
    auto &state = liquid();
    if (!state.boxDriven || !state.warped)
        return;
    state.warped = false;
    const auto window = m_window.lock();
    if (!window || !(state.goalBody.w > 0.0 && state.goalBody.h > 0.0))
        return;
    window->positionAnimation()->setValueAndWarp(state.goalBody.pos());
    window->sizeAnimation()->setValueAndWarp(state.goalBody.size());
}

void CLiquidGlassTransformer::driveBox() {
    if (!std::holds_alternative<SLiquidStyle>(m_style))
        return;
    auto &state = liquid();
    if (!state.boxDriven)
        return;
    if (!state.transition.active()) {
        restoreBox();
        state.boxDriven = false;
        return;
    }
    const auto window = m_window.lock();
    if (!window)
        return;

    auto &positionVar = window->positionAnimation();
    auto &sizeVar = window->sizeAnimation();
    // The layout retargets the real box through the same variables. A goal other than the one this wrote is the
    // layout's.
    if (state.warped && (positionVar->goal() != state.warpPosition || sizeVar->goal() != state.warpSize))
        state.goalBody = CBox{positionVar->goal(), sizeVar->goal()};
    if (!(state.goalBody.w > 0.0 && state.goalBody.h > 0.0))
        return;

    // Decorations (border, shadow) do not scale with the box, so the live margins hold at any size.
    const CBox   current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox   capture = window->getFullWindowBoundingBox();
    const double left = current.x - capture.x;
    const double top = current.y - capture.y;
    const double right = (capture.x + capture.w) - (current.x + current.w);
    const double bottom = (capture.y + capture.h) - (current.y + current.h);
    if (!(std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom)))
        return;

    const CBox       rest = {state.goalBody.x - left, state.goalBody.y - top, state.goalBody.w + left + right,
                             state.goalBody.h + top + bottom};
    const Vec2       half = state.transition.beadHalf({rest.w, rest.h});
    const Vec2       centre = state.transition.beadCentre();
    const double     cx = rest.x + 0.5 * rest.w + centre.x;
    const double     cy = rest.y + 0.5 * rest.h + centre.y;
    constexpr double MIN_SIDE = 2.0;
    const double     w = std::max(MIN_SIDE, 2.0 * half.x - left - right);
    const double     h = std::max(MIN_SIDE, 2.0 * half.y - top - bottom);
    const Vector2D   position{cx - half.x + left, cy - half.y + top};
    const Vector2D   size{w, h};

    positionVar->setValueAndWarp(position);
    sizeVar->setValueAndWarp(size);
    state.warpPosition = position;
    state.warpSize = size;
    state.warped = true;
}

// Pop-out geometry for a retained closing snapshot: the window as drawn at its driven box, scaled and moved back to the
// rest box. The scale is large for a small bead; the bound only guards against degenerate boxes.
LiquidPose CLiquidGlassTransformer::closePose() const {
    const auto      &state = liquid();
    const auto       window = m_window.lock();
    const LiquidPose unitPose{state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, state.displacementGain, false};
    if (!window || !state.boxDriven)
        return unitPose;
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal = state.goalBody;
    if (!(current.w > 0.0 && current.h > 0.0 && goal.w > 0.0 && goal.h > 0.0))
        return unitPose;
    constexpr double MAX_SCALE = 64.0;
    return {state.mesh,
            state.transition,
            {std::clamp(goal.w / current.w, 1.0 / MAX_SCALE, MAX_SCALE),
             std::clamp(goal.h / current.h, 1.0 / MAX_SCALE, MAX_SCALE)},
            {(goal.x + 0.5 * goal.w) - (current.x + 0.5 * current.w),
             (goal.y + 0.5 * goal.h) - (current.y + 0.5 * current.h)},
            state.displacementGain,
            false};
}

LiquidPose CLiquidGlassTransformer::renderPose() const {
    const auto &state = liquid();
    if (!state.boxDriven)
        return {state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, state.displacementGain, false};
    // The window is drawn at the pop-out box, so the rest centre sits at minus the bead's centre from it.
    const Vec2 bead = state.transition.beadCentre();
    return {state.mesh, state.transition, {1.0, 1.0}, {-bead.x, -bead.y}, state.displacementGain, true};
}

void CLiquidGlassTransformer::refreshPose(Vec2 captureSize) {
    auto &state = liquid();
    // Gain only: the shared grid is built by the pose that draws it, so nothing is pre-filled here.
    const liquid_glass::LiquidPose fit{state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, 1.0, state.boxDriven};
    state.displacementGain = liquid_glass::fitLiquidPoseGain(fit, captureSize, nullptr);
    state.grid.invalidate();
}

void CLiquidGlassTransformer::advanceLiquid(double elapsed, Vec2 size, double strength, double drive) {
    auto &state = liquid();
    liquid_glass::advanceLiquidPose(state.mesh, state.transition, state.grid, elapsed, size, strength, drive);
}

bool CLiquidGlassTransformer::transitionActive() const noexcept {
    return liquidStyle() && liquid().transition.active();
}

void CLiquidGlassTransformer::recordLiquidMove(Vec2 delta, Vec2 size, double strength, bool moving, bool enabled) {
    if (!liquidStyle())
        return;
    auto &state = liquid();
    if (moving)
        state.mesh.recordTranslation(delta, size, strength);
    else if (enabled)
        state.mesh.advance(0.0, size);
    else
        state.mesh.reset();
    refreshPose(size);
}

void CLiquidGlassTransformer::step(double elapsed, Vec2 size, double strength, bool moving, double emergeDrag) {
    advanceSheet(elapsed, size);
    if (!liquidStyle())
        return;
    if (!moving)
        liquid().mesh.reset();
    advanceLiquid(elapsed, size, strength, moving ? emergeDrag : 0.0);
    refreshPose(size);
    driveBox();
}

bool CLiquidGlassTransformer::deforming() const noexcept {
    if (const auto *state = std::get_if<SLiquidStyle>(&m_style))
        return !(state->mesh.settled() && !state->transition.active());
    return sheetActive();
}

bool CLiquidGlassTransformer::active() const {
    const auto window = m_window.lock();
    if (m_detached || !window || !window->mapped() || Fullscreen::controller()->isFullscreen(window))
        return false;
    if (!deforming())
        return false;
    const auto box = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && !box.empty();
}

int CLiquidGlassTransformer::priority() const { return 10; }

bool CLiquidGlassTransformer::blocksDirectScanout() const { return active(); }

CBox CLiquidGlassTransformer::transformedExtents(const CBox &currentBox) const {
    if (!active())
        return currentBox;

    if (const auto *sheet = sheetBuffersOf(m_style); sheet && sheetActive()) {
        CBox extents = sheetExtents(*sheet->frame, currentBox);
        extents.expand(EXTENTS_PADDING);
        return extents;
    }

    const LiquidPose        pose = renderPose();
    const liquid_glass::Box logical = toBox(currentBox);
    liquid_glass::Box       extents = liquid_glass::liquidRenderExtents(pose, logical, liquid().grid);
    liquid_glass::expandBox(extents, EXTENTS_PADDING);
    if (const auto visible = liquid_glass::liquidVisibleBox(pose, logical))
        extents = liquid_glass::intersectBoxes(extents, *visible);
    return toCBox(extents);
}

CBox CLiquidGlassTransformer::sourceBoxForOutput(const CBox &outputBox, const CBox &inputBox) const {
    // While deforming, the inverse capture must see the full input so no displaced texel is clipped.
    if (active())
        return inputBox;

    return outputBox.intersection(inputBox);
}

SWindowTransformBuffer CLiquidGlassTransformer::transform(CRenderContext &ctx, const SWindowTransformBuffer &in,
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

    if (SSheetBuffers *sheet = sheetBuffersOf(m_style); sheet && sheetActive()) {
        // The sheet branch draws its own work buffer, so it never allocates the liquid OUT buffer.
        fillSheetMeshes(*sheet->frame, SOURCEBOX, OUTPUTBOX, Vec2{in.box.x, in.box.y}, Vec2{texW, texH}, sheet->window,
                        sheet->shade);
        const auto layer =
            renderPaper(ctx, SPaperDraw{
                                 .window = in.framebuffer->getTexture(),
                                 .shade = sheetShaded() && m_shade ? m_shade->texture() : SP<Render::ITexture>{},
                                 .canvas = OUTPUTCANVAS.size(),
                                 .meshBox = LOCALOUTPUTBOX,
                                 .windowVertices = &sheet->window,
                                 .shadeVertices = &sheet->shade,
                                 .alpha = float(sheetOpacity()),
                             });
        if (!layer)
            return {
                .framebuffer = in.framebuffer,
                .box = in.box,
                .success = false,
            };
        return {
            .framebuffer = layer,
            .box = OUTPUTCANVAS,
        };
    }

    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, OUTPUTCANVAS.size());
    if (!OUT)
        return {
            .framebuffer = in.framebuffer,
            .box = in.box,
            .success = false,
        };

    auto                   &state = liquid();
    const LiquidPose        pose = renderPose();
    const liquid_glass::Box logical = toBox(context.currentBox);
    const auto              visible = liquid_glass::liquidVisibleBox(pose, logical);
    const auto              meniscus = liquid_glass::liquidMeniscusExtents(pose, logical);
    const SP<ITexture>      solid = meniscus && m_solid ? m_solid->texture() : SP<ITexture>{};
    const CBox              LIQUIDBOX = meniscus ? pixelBoxForLogical(toCBox(*meniscus), SCALE) : CBox{};

    liquid_glass::fillLiquidRenderMesh(pose, toBox(SOURCEBOX), SCALE, toBox(OUTPUTBOX), Vec2{in.box.x, in.box.y},
                                       Vec2{texW, texH}, state.grid, state.vertices);
    if (solid)
        liquid_glass::fillLiquidMeniscusMesh(pose, toBox(SOURCEBOX), SCALE, toBox(LIQUIDBOX), state.meniscusVertices);

    GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto                         drawState = ctx.saveDrawState();
    auto                        &renderData = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    renderData.damage = CRegion{0.0, 0.0, OUTPUTCANVAS.w, OUTPUTCANVAS.h};
    renderData.fbSize = OUTPUTCANVAS.size();
    g_pHyprRenderer->setProjectionType(ctx, RPT_EXPORT);

    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});

    // Nothing behind the screen edge is visible: the window and liquid are clipped to the screen side.
    if (visible) {
        CBox clip = pixelBoxForLogical(toCBox(*visible), SCALE);
        clip.translate(-OUTPUTCANVAS.pos());
        renderData.damage.intersect(CRegion{clip});
    }

    GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &renderData.damage;
    textureData.a = 1.F;
    textureData.allowCustomUV = true;
    if (solid)
        GL::g_pHyprOpenGL->renderTextureMesh(ctx, solid, LIQUIDBOX.copy().translate(-OUTPUTCANVAS.pos()),
                                             state.meniscusVertices, textureData);
    GL::g_pHyprOpenGL->renderTextureMesh(ctx, in.framebuffer->getTexture(), LOCALOUTPUTBOX, state.vertices,
                                         textureData);

    return {
        .framebuffer = OUT,
        .box = OUTPUTCANVAS,
    };
}

void CLiquidGlassTransformer::markDetached() {
    m_detached = true;
    if (auto *state = std::get_if<SLiquidStyle>(&m_style)) {
        state->mesh.reset();
        state->grid.invalidate();
    }
    cancelOpening();
    if (auto *paper = std::get_if<SPaperStyle>(&m_style))
        paper->crumple.reset();
    else if (auto *cloth = std::get_if<SClothStyle>(&m_style))
        cloth->cloth.reset();
}
