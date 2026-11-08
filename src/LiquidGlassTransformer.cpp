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

using namespace Render;
using liquid_glass::EdgeTransition;
using liquid_glass::LiquidMesh;
using liquid_glass::Vec2;

namespace {
    constexpr double EXTENTS_PADDING = 4.0;
    constexpr int    SAMPLES         = LIQUID_RENDER_CELLS + 1;
    // Edge liquid extends this far behind the screen edge so it meets the window without a gap.
    constexpr double BEHIND_EDGE     = 2.0;
    // Half-plane extent in logical px; far larger than any monitor.
    constexpr double HUGE_SPAN       = 1.0e6;

    // Lattice coordinate of render sample i along one axis, in [0, 1].
    constexpr double sampleCoordinate(int i) noexcept {
        return static_cast<double>(i) / LIQUID_RENDER_CELLS;
    }

    // Window sample offset from the box centre, in logical px: mesh displacement, pop-out travel and open geometry.
    Vec2 poseOffset(const SLiquidPose& pose, double u, double v, Vec2 size) {
        const Vec2   d        = pose.mesh.sampleDisplacement(u, v);
        const Vec2   c        = pose.compensation;
        const double gain     = pose.displacementGain;
        const Vec2   scaled{size.x * c.x, size.y * c.y};
        const Vec2   mapped   = pose.transition.map({((u - 0.5) * size.x + d.x * gain) * c.x, ((v - 0.5) * size.y + d.y * gain) * c.y}, scaled);
        return {mapped.x + pose.openOffset.x, mapped.y + pose.openOffset.y};
    }

    // Edge liquid point relative to the box centre, in logical px.
    Vec2 liquidPoint(const SLiquidPose& pose, double t, double n) {
        const Vec2 p = liquid_glass::edgePoint(pose.transition.frame(), t, n);
        return {p.x + pose.openOffset.x, p.y + pose.openOffset.y};
    }

    // Span along one axis of the visible half-plane: the screen side of the edge line.
    void visibleSpan(double edge, double inward, double& lo, double& hi) noexcept {
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
}

void advanceLiquidPose(LiquidMesh& mesh, EdgeTransition& transition, double elapsed, Vec2 size, double strength, double drive) {
    transition.advance(elapsed);
    // Drained every frame so travel never accumulates while drive is 0.
    const Vec2   travel = transition.takeTravel();
    const double share  = std::isfinite(drive) ? std::clamp(drive, 0.0, 1.0) : 0.0;
    if (share > 0.0 && (travel.x != 0.0 || travel.y != 0.0))
        mesh.recordTranslation({travel.x * share, travel.y * share}, size, strength);
    mesh.advance(elapsed, size);
}

std::optional<CBox> liquidVisibleBox(const SLiquidPose& pose, const CBox& logicalBox) {
    if (!pose.transition.active())
        return std::nullopt;

    const auto&  frame  = pose.transition.frame();
    const Vec2   inward = liquid_glass::inwardNormal(frame.edge);
    const Vec2   edge   = liquidPoint(pose, 0.0, 0.0);
    const double edgeX  = logicalBox.x + 0.5 * logicalBox.w + edge.x;
    const double edgeY  = logicalBox.y + 0.5 * logicalBox.h + edge.y;

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    visibleSpan(edgeX, inward.x, minX, maxX);
    visibleSpan(edgeY, inward.y, minY, maxY);
    return CBox{minX, minY, maxX - minX, maxY - minY};
}

std::optional<CBox> liquidMeniscusExtents(const SLiquidPose& pose, const CBox& logicalBox) {
    const double low  = pose.transition.meniscusLow();
    const double high = pose.transition.meniscusHigh();
    const double peak = pose.transition.meniscusPeak();
    if (!(peak > 0.0) || !(high > low))
        return std::nullopt;

    const double centreX = logicalBox.x + 0.5 * logicalBox.w;
    const double centreY = logicalBox.y + 0.5 * logicalBox.h;
    double       minX    = std::numeric_limits<double>::max();
    double       minY    = std::numeric_limits<double>::max();
    double       maxX    = std::numeric_limits<double>::lowest();
    double       maxY    = std::numeric_limits<double>::lowest();
    for (const double t : {low, high}) {
        for (const double n : {-BEHIND_EDGE, peak}) {
            const Vec2 p = liquidPoint(pose, t, n);
            minX         = std::min(minX, centreX + p.x);
            minY         = std::min(minY, centreY + p.y);
            maxX         = std::max(maxX, centreX + p.x);
            maxY         = std::max(maxY, centreY + p.y);
        }
    }
    return CBox{minX, minY, maxX - minX, maxY - minY};
}

CBox liquidRenderExtents(const SLiquidPose& pose, const CBox& logicalBox) {
    // Only displayed samples and the meniscus contribute; the undeformed box is not part of the canvas.
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    const Vec2 size{logicalBox.w, logicalBox.h};
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
            const Vec2   inward = liquid_glass::inwardNormal(pose.transition.frame().edge);
            const Vec2   edge   = liquidPoint(pose, 0.0, 0.0);
            extents             = CBox{centreX + edge.x + inward.x, centreY + edge.y + inward.y, 1.0, 1.0};
        }
    }
    return extents;
}

void fillLiquidRenderMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                          Vec2 textureOrigin, Vec2 textureSize, std::vector<SMeshRenderVertex>& out) {
    if (out.size() != LIQUID_RENDER_VERTEX_COUNT)
        out.resize(LIQUID_RENDER_VERTEX_COUNT);

    // Compute each grid sample once, then reuse it for its neighbouring triangles. Fixed stack storage.
    struct Sample {
        float x, y, u, v;
    };
    std::array<Sample, SAMPLES * SAMPLES> samples;
    const Vec2   size{sourceDevice.w / scale, sourceDevice.h / scale};
    const double centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double centreY = sourceDevice.y + 0.5 * sourceDevice.h;
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
    std::size_t   k           = 0;
    for (int cj = 0; cj < LIQUID_RENDER_CELLS; ++cj) {
        for (int ci = 0; ci < LIQUID_RENDER_CELLS; ++ci) {
            for (const auto& corner : ORDER) {
                SMeshRenderVertex& vertex = out[k++];
                const Sample& sample = samples[(cj + corner[1]) * SAMPLES + ci + corner[0]];
                vertex.x = sample.x;
                vertex.y = sample.y;
                vertex.u = sample.u;
                vertex.v = sample.v;
            }
        }
    }
}

void fillLiquidMeniscusMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                            std::vector<SMeshRenderVertex>& out) {
    if (out.size() != LIQUID_MENISCUS_VERTEX_COUNT)
        out.resize(LIQUID_MENISCUS_VERTEX_COUNT);

    const double centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double centreY = sourceDevice.y + 0.5 * sourceDevice.h;
    const double low     = pose.transition.meniscusLow();
    const double span    = pose.transition.meniscusHigh() - low;

    // Texture coordinates are unused: the liquid is a flat colour sampled at its centre.
    const auto place = [&](SMeshRenderVertex& vertex, double t, double n) {
        const Vec2 p = liquidPoint(pose, t, n);
        vertex.x     = static_cast<float>((centreX + p.x * scale - outputDevice.x) / outputDevice.w);
        vertex.y     = static_cast<float>((centreY + p.y * scale - outputDevice.y) / outputDevice.h);
        vertex.u     = 0.5F;
        vertex.v     = 0.5F;
    };

    for (int i = 0; i < LIQUID_MENISCUS_COLUMNS; ++i) {
        const double t0 = low + span * static_cast<double>(i) / LIQUID_MENISCUS_COLUMNS;
        const double t1 = low + span * static_cast<double>(i + 1) / LIQUID_MENISCUS_COLUMNS;
        const double h0 = pose.transition.meniscus(t0);
        const double h1 = pose.transition.meniscus(t1);

        // Corners: a = behind edge at t0, b = behind edge at t1, c = crest at t0, d = crest at t1. Triangles abc, bdc.
        SMeshRenderVertex* quad = &out[static_cast<std::size_t>(i) * 6];
        place(quad[0], t0, -BEHIND_EDGE);
        place(quad[1], t1, -BEHIND_EDGE);
        place(quad[2], t0, h0);
        place(quad[3], t1, -BEHIND_EDGE);
        place(quad[4], t1, h1);
        place(quad[5], t0, h0);
    }
}

CLiquidGlassTransformer::CLiquidGlassTransformer(PHLWINDOWREF window, liquid_glass::MotionMode mode, liquid_glass::MaterialParams params,
                                                 SP<CSolidFill> solid)
    : m_window(std::move(window)), m_mesh(mode, params), m_solid(std::move(solid)), m_vertices(LIQUID_RENDER_VERTEX_COUNT),
      m_meniscusVertices(LIQUID_MENISCUS_VERTEX_COUNT) {}

void CLiquidGlassTransformer::beginOpen(const liquid_glass::EdgeFrame& frame, const liquid_glass::MaterialParams& params, double strength,
                                        Vec2 captureSize) {
    m_transition.begin(liquid_glass::Transition::Open, frame, params, strength);
    m_openGeometry = m_transition.active();
    refreshPose(captureSize);
}

Vec2 CLiquidGlassTransformer::openCompensation(const PHLWINDOW& window) const {
    if (!m_openGeometry || !window->presentation().animatingIn())
        return {1.0, 1.0};
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    if (!(current.w > 0.0 && current.h > 0.0 && std::isfinite(goal.w) && std::isfinite(goal.h) && goal.w > 0.0 && goal.h > 0.0))
        return {1.0, 1.0};
    return {std::clamp(goal.w / current.w, 0.25, 4.0), std::clamp(goal.h / current.h, 0.25, 4.0)};
}

Vec2 CLiquidGlassTransformer::openOffset(const PHLWINDOW& window) const {
    if (!m_openGeometry || !window->presentation().animatingIn())
        return {0.0, 0.0};
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal    = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    if (!(std::isfinite(current.x) && std::isfinite(current.y) && std::isfinite(current.w) && std::isfinite(current.h) &&
          std::isfinite(goal.x) && std::isfinite(goal.y) && std::isfinite(goal.w) && std::isfinite(goal.h)))
        return {0.0, 0.0};
    return {(goal.x + 0.5 * goal.w) - (current.x + 0.5 * current.w), (goal.y + 0.5 * goal.h) - (current.y + 0.5 * current.h)};
}

SLiquidPose CLiquidGlassTransformer::renderPose() const {
    const auto window = m_window.lock();
    return {m_mesh, m_transition, m_compensation, window ? openOffset(window) : Vec2{0.0, 0.0}, m_displacementGain};
}

void CLiquidGlassTransformer::refreshPose(Vec2 captureSize) {
    const auto window = m_window.lock();
    m_compensation     = window ? openCompensation(window) : Vec2{1.0, 1.0};
    m_displacementGain = m_transition.displacementGain(m_mesh, captureSize, m_compensation);
}

bool CLiquidGlassTransformer::active() const {
    const auto window = m_window.lock();
    if (m_detached || !window || !window->mapped() || Fullscreen::controller()->isFullscreen(window))
        return false;
    if (m_mesh.settled() && !m_transition.active() && !(m_openGeometry && window->presentation().animatingIn()))
        return false;
    const auto box = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && !box.empty();
}

int CLiquidGlassTransformer::priority() const {
    return 10;
}

bool CLiquidGlassTransformer::blocksDirectScanout() const {
    return active();
}

CBox CLiquidGlassTransformer::transformedExtents(const CBox& currentBox) const {
    if (!active())
        return currentBox;

    const SLiquidPose pose = renderPose();
    CBox extents = liquidRenderExtents(pose, currentBox);
    extents.expand(EXTENTS_PADDING);
    if (const auto visible = liquidVisibleBox(pose, currentBox))
        extents = extents.intersection(*visible);
    return extents;
}

CBox CLiquidGlassTransformer::sourceBoxForOutput(const CBox& outputBox, const CBox& inputBox) const {
    // While deforming, the inverse capture must see the full input so no displaced texel is clipped.
    if (active())
        return inputBox;

    return outputBox.intersection(inputBox);
}

SWindowTransformBuffer CLiquidGlassTransformer::transform(CRenderContext& ctx, const SWindowTransformBuffer& in, const SWindowTransformContext& context) {
    if (!active() || context.standalone || context.renderingSnapshot || !context.monitor || !in.framebuffer || !in.framebuffer->getTexture())
        return in;

    const double SCALE          = context.monitor->m_scale;
    if (!std::isfinite(SCALE) || SCALE <= 0.0 || context.currentBox.empty())
        return in;
    const double CANVASPADDING  = 1.0 / SCALE;
    const CBox   OUTPUTCANVAS   = pixelBoxForLogical(context.outputBox.copy().expand(CANVASPADDING), SCALE);
    const CBox   SOURCEBOX      = context.currentBox.copy().scale(SCALE);
    const CBox   OUTPUTBOX      = transformedExtents(context.currentBox).scale(SCALE);
    const CBox   LOCALOUTPUTBOX = OUTPUTBOX.copy().translate(-OUTPUTCANVAS.pos());

    if (OUTPUTCANVAS.empty() || SOURCEBOX.empty() || OUTPUTBOX.empty())
        return in;

    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, OUTPUTCANVAS.size());
    if (!OUT)
        return {
            .framebuffer = in.framebuffer,
            .box         = in.box,
            .success     = false,
        };

    const auto   texSize = in.framebuffer->getTexture()->m_size;
    const double texW    = texSize.x;
    const double texH    = texSize.y;
    if (!(texW > 0.0 && texH > 0.0))
        return in;

    const SLiquidPose pose    = renderPose();
    const auto        visible = liquidVisibleBox(pose, context.currentBox);
    const auto        meniscus = liquidMeniscusExtents(pose, context.currentBox);
    const SP<ITexture> solid = meniscus && m_solid ? m_solid->texture() : SP<ITexture>{};
    const CBox        LIQUIDBOX = meniscus ? pixelBoxForLogical(*meniscus, SCALE) : CBox{};

    fillLiquidRenderMesh(pose, SOURCEBOX, SCALE, OUTPUTBOX, Vec2{in.box.x, in.box.y}, Vec2{texW, texH}, m_vertices);
    if (solid)
        fillLiquidMeniscusMesh(pose, SOURCEBOX, SCALE, LIQUIDBOX, m_meniscusVertices);

    GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto                         state      = ctx.saveDrawState();
    auto&                        renderData = ctx.m_data;
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
        GL::g_pHyprOpenGL->renderTextureMesh(ctx, solid, LIQUIDBOX.copy().translate(-OUTPUTCANVAS.pos()), m_meniscusVertices, textureData);
    GL::g_pHyprOpenGL->renderTextureMesh(ctx, in.framebuffer->getTexture(), LOCALOUTPUTBOX, m_vertices, textureData);

    return {
        .framebuffer = OUT,
        .box         = OUTPUTCANVAS,
    };
}
