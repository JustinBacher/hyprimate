#include "LiquidPose.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace liquid_glass {

namespace {

// Half-plane extent in logical px; far larger than any monitor.
constexpr double HUGE_SPAN = 1.0e6;
// Grid sizes differing by less than this are the same sampling: device/scale round trips move the size by ulps.
constexpr double SIZE_TOLERANCE = 1.0e-9;

// Gain-search constants: a rung is kept while every triangle keeps this much of its gain-0 area.
constexpr double kMinAreaFraction = 0.2;
constexpr int    kGainHalvings = 8;

double triangleArea(Vec2 a, Vec2 b, Vec2 c) { return 0.5 * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)); }

// Window sample offset from the box centre, in logical px, for a sample at rest offset `base` with movement flow `flow`
// at `gain`. `map` holds the constants for the pose's scaled size and is unused by a box pose. A box-driven open leaves
// only the movement field: the real window box already is the pop-out. Otherwise (a retained closing snapshot) the
// pop-out maps the rest box and openOffset shifts it.
Vec2 sampleOffset(const LiquidPose &pose, const EdgeTransition::RestMap &map, Vec2 base, Vec2 flow, double gain) {
    if (pose.boxPose)
        return {base.x + gain * flow.x, base.y + gain * flow.y};
    const Vec2 c = pose.compensation;
    const Vec2 mapped = map.map({(base.x + gain * flow.x) * c.x, (base.y + gain * flow.y) * c.y});
    return {mapped.x + pose.openOffset.x, mapped.y + pose.openOffset.y};
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

Vec2 liquidEdgePoint(const LiquidPose &pose, double t, double n) {
    const Vec2 p = edgePoint(pose.transition.frame(), t, n);
    return {p.x + pose.openOffset.x, p.y + pose.openOffset.y};
}

void refreshLiquidPoseGrid(const LiquidPose &pose, Vec2 size, LiquidPoseGrid &grid) {
    if (!grid.stale && std::abs(grid.size.x - size.x) <= SIZE_TOLERANCE &&
        std::abs(grid.size.y - size.y) <= SIZE_TOLERANCE)
        return;

    const Vec2                    c = pose.compensation;
    const EdgeTransition::RestMap map = pose.transition.restMap({size.x * c.x, size.y * c.y});
    for (int j = 0; j < LIQUID_POSE_SAMPLES; ++j) {
        for (int i = 0; i < LIQUID_POSE_SAMPLES; ++i) {
            const double u = static_cast<double>(i) / LIQUID_RENDER_CELLS;
            const double v = static_cast<double>(j) / LIQUID_RENDER_CELLS;
            const Vec2   base{(u - 0.5) * size.x, (v - 0.5) * size.y};
            grid.offsets[static_cast<std::size_t>(j) * LIQUID_POSE_SAMPLES + i] =
                sampleOffset(pose, map, base, pose.mesh.sampleDisplacement(u, v), pose.displacementGain);
        }
    }
    grid.size = size;
    grid.stale = false;
}

double fitLiquidPoseGain(const LiquidPose &pose, Vec2 restSize, LiquidPoseGrid *grid) {
    // Only an accepted field reaches the grid; any other outcome leaves the grid to be rebuilt lazily.
    const auto settle = [grid](double gain) {
        if (grid)
            grid->invalidate();
        return gain;
    };
    const EdgeTransition &transition = pose.transition;
    if (!transition.active() || pose.mesh.settled())
        return settle(1.0);
    const Vec2 c = pose.compensation;
    if (!std::isfinite(restSize.x) || !std::isfinite(restSize.y) || !std::isfinite(c.x) || !std::isfinite(c.y) ||
        restSize.x <= 0.0 || restSize.y <= 0.0 || c.x <= 0.0 || c.y <= 0.0)
        return settle(0.0);

    // Same composition as the renderer: the displayed sample is map(((u-0.5)*S + g*d) * c, S*c).
    const EdgeTransition::RestMap map = transition.restMap({restSize.x * c.x, restSize.y * c.y});
    constexpr int                 kSide = LIQUID_POSE_SAMPLES;
    constexpr int                 kVertices = kSide * kSide;
    std::array<Vec2, kVertices>   base{}, flow{}, mapOnly{}, moved{};
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            const double u = static_cast<double>(i) / LIQUID_RENDER_CELLS;
            const double v = static_cast<double>(j) / LIQUID_RENDER_CELLS;
            const int    k = j * kSide + i;
            base[k] = {(u - 0.5) * restSize.x, (v - 0.5) * restSize.y};
            flow[k] = pose.mesh.sampleDisplacement(u, v);
            mapOnly[k] = sampleOffset(pose, map, base[k], flow[k], 0.0);
        }
    }

    // Every triangle keeps kMinAreaFraction of its gain-0 area.
    const auto keepsArea = [&](const std::array<Vec2, kVertices> &field) {
        for (int j = 0; j + 1 < kSide; ++j) {
            for (int i = 0; i + 1 < kSide; ++i) {
                const int p00 = j * kSide + i, p10 = p00 + 1, p01 = p00 + kSide, p11 = p01 + 1;
                const int tris[2][3] = {{p00, p10, p01}, {p10, p11, p01}};
                for (const auto &t : tris) {
                    const double before = triangleArea(mapOnly[t[0]], mapOnly[t[1]], mapOnly[t[2]]);
                    if (!(before > 0.0))
                        continue; // degenerate at rest: nothing to keep
                    const double after = triangleArea(field[t[0]], field[t[1]], field[t[2]]);
                    if (!(after >= kMinAreaFraction * before))
                        return false;
                }
            }
        }
        return true;
    };
    // The grid's offsets are exactly this field at `gain`.
    const auto publish = [grid, &restSize](const std::array<Vec2, kVertices> &field, double gain) {
        if (grid) {
            grid->offsets = field;
            grid->size = restSize;
            grid->stale = false;
        }
        return gain;
    };
    for (int h = 0; h <= kGainHalvings; ++h) {
        const double gain = std::ldexp(1.0, -h);
        for (int k = 0; k < kVertices; ++k)
            moved[k] = sampleOffset(pose, map, base[k], flow[k], gain);
        if (keepsArea(moved))
            return publish(moved, gain);
    }
    return publish(mapOnly, 0.0);
}

void advanceLiquidPose(LiquidMesh &mesh, EdgeTransition &transition, LiquidPoseGrid &grid, double elapsed, Vec2 size,
                       double strength, double drive) {
    grid.invalidate();
    transition.advance(elapsed);
    // Drained every frame so travel never accumulates while drive is 0.
    const Vec2   travel = transition.takeTravel();
    const double share = std::isfinite(drive) ? std::clamp(drive, 0.0, 1.0) : 0.0;
    if (share > 0.0 && (travel.x != 0.0 || travel.y != 0.0))
        mesh.recordTranslation({travel.x * share, travel.y * share}, size, strength);
    mesh.advance(elapsed, size);
}

std::optional<Box> liquidVisibleBox(const LiquidPose &pose, const Box &logicalBox) {
    if (!pose.transition.active())
        return std::nullopt;

    const auto  &frame = pose.transition.frame();
    const Vec2   inward = inwardNormal(frame.edge);
    const Vec2   edge = liquidEdgePoint(pose, 0.0, 0.0);
    const double edgeX = logicalBox.x + 0.5 * logicalBox.w + edge.x;
    const double edgeY = logicalBox.y + 0.5 * logicalBox.h + edge.y;

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    visibleSpan(edgeX, inward.x, minX, maxX);
    visibleSpan(edgeY, inward.y, minY, maxY);
    return Box{minX, minY, maxX - minX, maxY - minY};
}

std::optional<Box> liquidMeniscusExtents(const LiquidPose &pose, const Box &logicalBox) {
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
        for (const double n : {-LIQUID_BEHIND_EDGE, peak}) {
            const Vec2 p = liquidEdgePoint(pose, t, n);
            minX = std::min(minX, centreX + p.x);
            minY = std::min(minY, centreY + p.y);
            maxX = std::max(maxX, centreX + p.x);
            maxY = std::max(maxY, centreY + p.y);
        }
    }
    return Box{minX, minY, maxX - minX, maxY - minY};
}

Box liquidRenderExtents(const LiquidPose &pose, const Box &logicalBox, LiquidPoseGrid &grid) {
    const Vec2 size{logicalBox.w, logicalBox.h};
    refreshLiquidPoseGrid(pose, size, grid);

    const double centreX = logicalBox.x + 0.5 * logicalBox.w;
    const double centreY = logicalBox.y + 0.5 * logicalBox.h;

    // Only displayed samples and the meniscus contribute; the undeformed box is not part of the canvas.
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const Vec2 p : grid.offsets) {
        minX = std::min(minX, centreX + p.x);
        minY = std::min(minY, centreY + p.y);
        maxX = std::max(maxX, centreX + p.x);
        maxY = std::max(maxY, centreY + p.y);
    }

    if (const auto meniscus = liquidMeniscusExtents(pose, logicalBox)) {
        minX = std::min(minX, meniscus->x);
        minY = std::min(minY, meniscus->y);
        maxX = std::max(maxX, meniscus->x + meniscus->w);
        maxY = std::max(maxY, meniscus->y + meniscus->h);
    }

    Box extents{minX, minY, maxX - minX, maxY - minY};
    if (const auto visible = liquidVisibleBox(pose, logicalBox)) {
        extents = intersectBoxes(extents, *visible);
        if (emptyBox(extents)) {
            // Nothing visible yet: a 1 px sliver on the screen side of the edge keeps the damage path valid.
            const Vec2 inward = inwardNormal(pose.transition.frame().edge);
            const Vec2 edge = liquidEdgePoint(pose, 0.0, 0.0);
            extents = Box{centreX + edge.x + inward.x, centreY + edge.y + inward.y, 1.0, 1.0};
        }
    }
    return extents;
}

Box projectToOutput(const Box &extentsDevice, const Box &renderBox, Vec2 snapshotSize) {
    const double kx = renderBox.w / snapshotSize.x;
    const double ky = renderBox.h / snapshotSize.y;
    return Box{renderBox.x + extentsDevice.x * kx, renderBox.y + extentsDevice.y * ky, extentsDevice.w * kx,
               extentsDevice.h * ky};
}

Box ownedRenderBox(const Box &sourceDevice, const Box &nativeBox, Vec2 snapshotSize) {
    const double kx = nativeBox.w / snapshotSize.x;
    const double ky = nativeBox.h / snapshotSize.y;
    const double mx = sourceDevice.x + sourceDevice.w * 0.5;
    const double my = sourceDevice.y + sourceDevice.h * 0.5;
    const double cx = nativeBox.x + mx * kx;
    const double cy = nativeBox.y + my * ky;
    return Box{cx - mx, cy - my, snapshotSize.x, snapshotSize.y};
}

namespace {

// CBox semantics: a result that is empty on either axis keeps its origin but has both sizes zeroed.
Box normaliseEmpty(Box box) noexcept {
    if (box.w <= 0.0 || box.h <= 0.0) {
        box.w = 0.0;
        box.h = 0.0;
    }
    return box;
}

} // namespace

Box intersectBoxes(const Box &a, const Box &b) noexcept {
    const double x = std::max(a.x, b.x);
    const double y = std::max(a.y, b.y);
    return normaliseEmpty(Box{x, y, std::min(a.x + a.w, b.x + b.w) - x, std::min(a.y + a.h, b.y + b.h) - y});
}

void expandBox(Box &box, double padding) noexcept {
    box.x -= padding;
    box.y -= padding;
    box.w += 2.0 * padding;
    box.h += 2.0 * padding;
    box = normaliseEmpty(box);
}

bool emptyBox(const Box &box) noexcept { return box.w <= 0.0 || box.h <= 0.0; }

} // namespace liquid_glass
