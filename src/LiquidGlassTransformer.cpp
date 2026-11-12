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
using liquid_glass::Vec2;

namespace {
constexpr double EXTENTS_PADDING = 4.0;
constexpr int    SAMPLES = liquid_glass::LIQUID_RENDER_CELLS + 1;
// Edge liquid extends this far behind the screen edge so it meets the window without a gap.
constexpr double BEHIND_EDGE = 2.0;
// Half-plane extent in logical px; far larger than any monitor.
constexpr double HUGE_SPAN = 1.0e6;

// Lattice coordinate of render sample i along one axis, in [0, 1].
constexpr double sampleCoordinate(int i) noexcept { return static_cast<double>(i) / liquid_glass::LIQUID_RENDER_CELLS; }

// Window sample offset from the box centre, in logical px. A box-driven open leaves only the movement field: the
// real window box already is the pop-out. Otherwise (a retained closing snapshot) the pop-out maps the rest box.
Vec2 poseOffset(const SLiquidPose &pose, double u, double v, Vec2 size) {
    const Vec2   d = pose.mesh.sampleDisplacement(u, v);
    const double gain = pose.displacementGain;
    if (pose.boxPose)
        return {(u - 0.5) * size.x + d.x * gain, (v - 0.5) * size.y + d.y * gain};
    const Vec2 c = pose.compensation;
    const Vec2 scaled{size.x * c.x, size.y * c.y};
    const Vec2 mapped =
        pose.transition.map({((u - 0.5) * size.x + d.x * gain) * c.x, ((v - 0.5) * size.y + d.y * gain) * c.y}, scaled);
    return {mapped.x + pose.openOffset.x, mapped.y + pose.openOffset.y};
}

// Edge liquid point relative to the box centre, in logical px.
Vec2 liquidPoint(const SLiquidPose &pose, double t, double n) {
    const Vec2 p = liquid_glass::edgePoint(pose.transition.frame(), t, n);
    return {p.x + pose.openOffset.x, p.y + pose.openOffset.y};
}

// Span along one axis of the visible half-plane: the screen side of the edge line.
void visibleSpan(double edge, double inward, double &lo, double &hi) noexcept {
    if (inward > 0.0) {
        lo = edge;
        hi = edge + HUGE_SPAN;
    } else if (inward < 0.0) {
        lo = edge - HUGE_SPAN;
        hi = edge;
    } else {
        lo = edge - HUGE_SPAN;
        hi = edge + HUGE_SPAN;
    }
}
} // namespace

void advanceLiquidPose(LiquidMesh &mesh, EdgeTransition &transition, double elapsed, Vec2 size, double strength,
                       double drive) {
    transition.advance(elapsed);
    // Drained every frame so travel never accumulates while drive is 0.
    const Vec2   travel = transition.takeTravel();
    const double share = std::isfinite(drive) ? std::clamp(drive, 0.0, 1.0) : 0.0;
    if (share > 0.0 && (travel.x != 0.0 || travel.y != 0.0))
        mesh.recordTranslation({travel.x * share, travel.y * share}, size, strength);
    mesh.advance(elapsed, size);
}

std::optional<CBox> liquidVisibleBox(const SLiquidPose &pose, const CBox &logicalBox) {
    if (!pose.transition.active())
        return std::nullopt;

    const auto  &frame = pose.transition.frame();
    const Vec2   inward = liquid_glass::inwardNormal(frame.edge);
    const Vec2   edge = liquidPoint(pose, 0.0, 0.0);
    const double edgeX = logicalBox.x + 0.5 * logicalBox.w + edge.x;
    const double edgeY = logicalBox.y + 0.5 * logicalBox.h + edge.y;

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    visibleSpan(edgeX, inward.x, minX, maxX);
    visibleSpan(edgeY, inward.y, minY, maxY);
    return CBox{minX, minY, maxX - minX, maxY - minY};
}

std::optional<CBox> liquidMeniscusExtents(const SLiquidPose &pose, const CBox &logicalBox) {
    const double low = pose.transition.meniscusLow();
    const double high = pose.transition.meniscusHigh();
    const double peak = pose.transition.meniscusPeak();
    if (!(peak > 0.0) || !(high > low))
        return std::nullopt;

    const double centreX = logicalBox.x + 0.5 * logicalBox.w;
    const double centreY = logicalBox.y + 0.5 * logicalBox.h;
    double       minX = std::numeric_limits<double>::max();
    double       minY = std::numeric_limits<double>::max();
    double       maxX = std::numeric_limits<double>::lowest();
    double       maxY = std::numeric_limits<double>::lowest();
    for (const double t : {low, high}) {
        for (const double n : {-BEHIND_EDGE, peak}) {
            const Vec2 p = liquidPoint(pose, t, n);
            minX = std::min(minX, centreX + p.x);
            minY = std::min(minY, centreY + p.y);
            maxX = std::max(maxX, centreX + p.x);
            maxY = std::max(maxY, centreY + p.y);
        }
    }
    return CBox{minX, minY, maxX - minX, maxY - minY};
}

CBox liquidRenderExtents(const SLiquidPose &pose, const CBox &logicalBox) {
    // Only displayed samples and the meniscus contribute; the undeformed box is not part of the canvas.
    double       minX = std::numeric_limits<double>::max();
    double       minY = std::numeric_limits<double>::max();
    double       maxX = std::numeric_limits<double>::lowest();
    double       maxY = std::numeric_limits<double>::lowest();
    const Vec2   size{logicalBox.w, logicalBox.h};
    const double centreX = logicalBox.x + 0.5 * logicalBox.w;
    const double centreY = logicalBox.y + 0.5 * logicalBox.h;

    for (int j = 0; j < SAMPLES; ++j) {
        for (int i = 0; i < SAMPLES; ++i) {
            const Vec2 p = poseOffset(pose, sampleCoordinate(i), sampleCoordinate(j), size);
            minX = std::min(minX, centreX + p.x);
            minY = std::min(minY, centreY + p.y);
            maxX = std::max(maxX, centreX + p.x);
            maxY = std::max(maxY, centreY + p.y);
        }
    }

    if (const auto meniscus = liquidMeniscusExtents(pose, logicalBox)) {
        minX = std::min(minX, meniscus->x);
        minY = std::min(minY, meniscus->y);
        maxX = std::max(maxX, meniscus->x + meniscus->w);
        maxY = std::max(maxY, meniscus->y + meniscus->h);
    }

    CBox extents{minX, minY, maxX - minX, maxY - minY};
    if (const auto visible = liquidVisibleBox(pose, logicalBox)) {
        extents = extents.intersection(*visible);
        if (extents.empty()) {
            // Nothing visible yet: a 1 px sliver on the screen side of the edge keeps the damage path valid.
            const Vec2 inward = liquid_glass::inwardNormal(pose.transition.frame().edge);
            const Vec2 edge = liquidPoint(pose, 0.0, 0.0);
            extents = CBox{centreX + edge.x + inward.x, centreY + edge.y + inward.y, 1.0, 1.0};
        }
    }
    return extents;
}

void fillLiquidRenderMesh(const SLiquidPose &pose, const CBox &sourceDevice, double scale, const CBox &outputDevice,
                          Vec2 textureOrigin, Vec2 textureSize, std::vector<SMeshRenderVertex> &out) {
    if (out.size() != LIQUID_RENDER_VERTEX_COUNT)
        out.resize(LIQUID_RENDER_VERTEX_COUNT);

    // Compute each grid sample once, then reuse it for its neighbouring triangles. Fixed stack storage.
    struct Sample {
        float x, y, u, v;
    };
    std::array<Sample, SAMPLES * SAMPLES> samples;
    const Vec2                            size{sourceDevice.w / scale, sourceDevice.h / scale};
    const double                          centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double                          centreY = sourceDevice.y + 0.5 * sourceDevice.h;
    for (int j = 0; j < SAMPLES; ++j) {
        for (int i = 0; i < SAMPLES; ++i) {
            const double u = sampleCoordinate(i);
            const double v = sampleCoordinate(j);
            const Vec2   p = poseOffset(pose, u, v, size);
            samples[j * SAMPLES + i] = {
                static_cast<float>((centreX + p.x * scale - outputDevice.x) / outputDevice.w),
                static_cast<float>((centreY + p.y * scale - outputDevice.y) / outputDevice.h),
                static_cast<float>((sourceDevice.x + sourceDevice.w * u - textureOrigin.x) / textureSize.x),
                static_cast<float>((sourceDevice.y + sourceDevice.h * v - textureOrigin.y) / textureSize.y),
            };
        }
    }

    constexpr int ORDER[6][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 0}, {0, 1}, {1, 1}};
    std::size_t   k = 0;
    for (int cj = 0; cj < liquid_glass::LIQUID_RENDER_CELLS; ++cj) {
        for (int ci = 0; ci < liquid_glass::LIQUID_RENDER_CELLS; ++ci) {
            for (const auto &corner : ORDER) {
                SMeshRenderVertex &vertex = out[k++];
                const Sample      &sample = samples[(cj + corner[1]) * SAMPLES + ci + corner[0]];
                vertex.x = sample.x;
                vertex.y = sample.y;
                vertex.u = sample.u;
                vertex.v = sample.v;
            }
        }
    }
}

void fillLiquidMeniscusMesh(const SLiquidPose &pose, const CBox &sourceDevice, double scale, const CBox &outputDevice,
                            std::vector<SMeshRenderVertex> &out) {
    if (out.size() != LIQUID_MENISCUS_VERTEX_COUNT)
        out.resize(LIQUID_MENISCUS_VERTEX_COUNT);

    const double centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double centreY = sourceDevice.y + 0.5 * sourceDevice.h;
    const double low = pose.transition.meniscusLow();
    const double span = pose.transition.meniscusHigh() - low;

    // Texture coordinates are unused: the liquid is a flat colour sampled at its centre.
    const auto place = [&](SMeshRenderVertex &vertex, double t, double n) {
        const Vec2 p = liquidPoint(pose, t, n);
        vertex.x = static_cast<float>((centreX + p.x * scale - outputDevice.x) / outputDevice.w);
        vertex.y = static_cast<float>((centreY + p.y * scale - outputDevice.y) / outputDevice.h);
        vertex.u = 0.5F;
        vertex.v = 0.5F;
    };

    for (int i = 0; i < LIQUID_MENISCUS_COLUMNS; ++i) {
        const double t0 = low + span * static_cast<double>(i) / LIQUID_MENISCUS_COLUMNS;
        const double t1 = low + span * static_cast<double>(i + 1) / LIQUID_MENISCUS_COLUMNS;
        const double h0 = pose.transition.meniscus(t0);
        const double h1 = pose.transition.meniscus(t1);

        // Corners: a = behind edge at t0, b = behind edge at t1, c = crest at t0, d = crest at t1. Triangles abc, bdc.
        SMeshRenderVertex *quad = &out[static_cast<std::size_t>(i) * 6];
        place(quad[0], t0, -BEHIND_EDGE);
        place(quad[1], t1, -BEHIND_EDGE);
        place(quad[2], t0, h0);
        place(quad[3], t1, -BEHIND_EDGE);
        place(quad[4], t1, h1);
        place(quad[5], t0, h0);
    }
}

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
SLiquidPose CLiquidGlassTransformer::closePose() const {
    const auto       &state = liquid();
    const auto        window = m_window.lock();
    const SLiquidPose unitPose{state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, state.displacementGain, false};
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

SLiquidPose CLiquidGlassTransformer::renderPose() const {
    const auto &state = liquid();
    if (!state.boxDriven)
        return {state.mesh, state.transition, {1.0, 1.0}, {0.0, 0.0}, state.displacementGain, false};
    // The window is drawn at the pop-out box, so the rest centre sits at minus the bead's centre from it.
    const Vec2 bead = state.transition.beadCentre();
    return {state.mesh, state.transition, {1.0, 1.0}, {-bead.x, -bead.y}, state.displacementGain, true};
}

void CLiquidGlassTransformer::refreshPose(Vec2 captureSize) {
    auto &state = liquid();
    state.displacementGain = state.transition.displacementGain(state.mesh, captureSize, {1.0, 1.0}, state.boxDriven);
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

    const SLiquidPose pose = renderPose();
    CBox              extents = liquidRenderExtents(pose, currentBox);
    extents.expand(EXTENTS_PADDING);
    if (const auto visible = liquidVisibleBox(pose, currentBox))
        extents = extents.intersection(*visible);
    return extents;
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

    auto              &state = liquid();
    const SLiquidPose  pose = renderPose();
    const auto         visible = liquidVisibleBox(pose, context.currentBox);
    const auto         meniscus = liquidMeniscusExtents(pose, context.currentBox);
    const SP<ITexture> solid = meniscus && m_solid ? m_solid->texture() : SP<ITexture>{};
    const CBox         LIQUIDBOX = meniscus ? pixelBoxForLogical(*meniscus, SCALE) : CBox{};

    fillLiquidRenderMesh(pose, SOURCEBOX, SCALE, OUTPUTBOX, Vec2{in.box.x, in.box.y}, Vec2{texW, texH}, state.vertices);
    if (solid)
        fillLiquidMeniscusMesh(pose, SOURCEBOX, SCALE, LIQUIDBOX, state.meniscusVertices);

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
        CBox clip = pixelBoxForLogical(*visible, SCALE);
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
    if (auto *state = std::get_if<SLiquidStyle>(&m_style))
        state->mesh.reset();
    cancelOpening();
    if (auto *paper = std::get_if<SPaperStyle>(&m_style))
        paper->crumple.reset();
    else if (auto *cloth = std::get_if<SClothStyle>(&m_style))
        cloth->cloth.reset();
}
