#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"

namespace hyprimate {

    namespace {

        // Half-plane extent in logical px; far larger than any monitor.
        constexpr double HUGE_SPAN = 1.0e6;
        // Grid sizes differing by less than this are the same sampling: device/scale round trips
        // move the size by ulps.
        constexpr double SIZE_TOLERANCE = 1.0e-9;

        // Gain-search constants: a rung is kept while every triangle keeps this much of its gain-0
        // area.
        constexpr double kMinAreaFraction = 0.2;
        constexpr int kGainHalvings = 8;

        double triangleArea(Vec2 a, Vec2 b, Vec2 c) {
            return 0.5 * (((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x)));
        }

        // Window sample offset from the box centre, in logical px, for a sample at rest offset
        // `base` with movement flow `flow` at `gain`. `map` holds the constants for the pose's
        // scaled size and is unused by a box pose. A box-driven open leaves only the movement
        // field: the real window box already is the pop-out. Otherwise (a retained closing
        // snapshot) the pop-out maps the rest box and openOffset shifts it.
        Vec2 sampleOffset(const LiquidPose &pose,
            const EdgeTransition::RestMap &map,
            Vec2 base,
            Vec2 flow,
            double gain) {
            if (pose.boxPose) {
                return {.x = base.x + (gain * flow.x), .y = base.y + (gain * flow.y)};
            }
            const Vec2 c = pose.compensation;
            const Vec2 MAPPED = map.map(
                {.x = (base.x + (gain * flow.x)) * c.x, .y = (base.y + (gain * flow.y)) * c.y});
            return {.x = MAPPED.x + pose.openOffset.x, .y = MAPPED.y + pose.openOffset.y};
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

    }  // namespace

    Vec2 liquidEdgePoint(const LiquidPose &pose, double t, double n) {
        const Vec2 p = edgePoint(pose.transition.frame(), t, n);
        return {.x = p.x + pose.openOffset.x, .y = p.y + pose.openOffset.y};
    }

    void refreshLiquidPoseGrid(const LiquidPose &pose, Vec2 size, LiquidPoseGrid &grid) {
        if (!grid.stale && std::abs(grid.size.x - size.x) <= SIZE_TOLERANCE &&
            std::abs(grid.size.y - size.y) <= SIZE_TOLERANCE) {
            return;
        }

        const Vec2 c = pose.compensation;
        const EdgeTransition::RestMap MAP =
            pose.transition.restMap({.x = size.x * c.x, .y = size.y * c.y});
        for (int j = 0; j < LIQUID_POSE_SAMPLES; ++j) {
            for (int i = 0; i < LIQUID_POSE_SAMPLES; ++i) {
                const double u = static_cast<double>(i) / LIQUID_RENDER_CELLS;
                const double v = static_cast<double>(j) / LIQUID_RENDER_CELLS;
                const Vec2 BASE{.x = (u - 0.5) * size.x, .y = (v - 0.5) * size.y};
                grid.offsets[(static_cast<std::size_t>(j) * LIQUID_POSE_SAMPLES) + i] =
                    sampleOffset(
                        pose, MAP, BASE, pose.mesh.sampleDisplacement(u, v), pose.displacementGain);
            }
        }
        grid.size = size;
        grid.stale = false;
    }

    double fitLiquidPoseGain(const LiquidPose &pose, Vec2 restSize, LiquidPoseGrid *grid) {
        // Only an accepted field reaches the grid; any other outcome leaves the grid to be rebuilt
        // lazily.
        const auto SETTLE = [grid](double gain) {
            if (grid) {
                grid->invalidate();
            }
            return gain;
        };
        const EdgeTransition &transition = pose.transition;
        if (!transition.active() || pose.mesh.settled()) {
            return SETTLE(1.0);
        }
        const Vec2 c = pose.compensation;
        if (!std::isfinite(restSize.x) || !std::isfinite(restSize.y) || !std::isfinite(c.x) ||
            !std::isfinite(c.y) || restSize.x <= 0.0 || restSize.y <= 0.0 || c.x <= 0.0 ||
            c.y <= 0.0) {
            return SETTLE(0.0);
        }

        // Same composition as the renderer: the displayed sample is map(((u-0.5)*S + g*d) * c,
        // S*c).
        const EdgeTransition::RestMap MAP =
            transition.restMap({.x = restSize.x * c.x, .y = restSize.y * c.y});
        constexpr int K_SIDE = LIQUID_POSE_SAMPLES;
        constexpr int K_VERTICES = K_SIDE * K_SIDE;
        std::array<Vec2, K_VERTICES> base{};
        std::array<Vec2, K_VERTICES> flow{};
        std::array<Vec2, K_VERTICES> mapOnly{};
        std::array<Vec2, K_VERTICES> moved{};
        for (int j = 0; j < K_SIDE; ++j) {
            for (int i = 0; i < K_SIDE; ++i) {
                const double u = static_cast<double>(i) / LIQUID_RENDER_CELLS;
                const double v = static_cast<double>(j) / LIQUID_RENDER_CELLS;
                const int k = (j * K_SIDE) + i;
                base[k] = {.x = (u - 0.5) * restSize.x, .y = (v - 0.5) * restSize.y};
                flow[k] = pose.mesh.sampleDisplacement(u, v);
                mapOnly[k] = sampleOffset(pose, MAP, base[k], flow[k], 0.0);
            }
        }

        // Every triangle keeps kMinAreaFraction of its gain-0 area.
        const auto KEEPS_AREA = [&](const std::array<Vec2, K_VERTICES> &field) {
            for (int j = 0; j + 1 < K_SIDE; ++j) {
                for (int i = 0; i + 1 < K_SIDE; ++i) {
                    const int P00 = (j * K_SIDE) + i;
                    const int P10 = P00 + 1;
                    const int P01 = P00 + K_SIDE;
                    const int P11 = P01 + 1;
                    const int TRIS[2][3] = {{P00, P10, P01}, {P10, P11, P01}};
                    for (const auto &t : TRIS) {
                        const double BEFORE =
                            triangleArea(mapOnly[t[0]], mapOnly[t[1]], mapOnly[t[2]]);
                        if (!(BEFORE > 0.0)) {
                            continue;  // degenerate at rest: nothing to keep
                        }
                        const double AFTER = triangleArea(field[t[0]], field[t[1]], field[t[2]]);
                        if (!(AFTER >= kMinAreaFraction * BEFORE)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };
        // The grid's offsets are exactly this field at `gain`.
        const auto PUBLISH = [grid, &restSize](
                                 const std::array<Vec2, K_VERTICES> &field, double gain) {
            if (grid) {
                grid->offsets = field;
                grid->size = restSize;
                grid->stale = false;
            }
            return gain;
        };
        for (int h = 0; h <= kGainHalvings; ++h) {
            const double GAIN = std::ldexp(1.0, -h);
            for (int k = 0; k < K_VERTICES; ++k) {
                moved[k] = sampleOffset(pose, MAP, base[k], flow[k], GAIN);
            }
            if (KEEPS_AREA(moved)) {
                return PUBLISH(moved, GAIN);
            }
        }
        return PUBLISH(mapOnly, 0.0);
    }

    void advanceLiquidPose(LiquidMesh &mesh,
        EdgeTransition &transition,
        LiquidPoseGrid &grid,
        double elapsed,
        Vec2 size,
        double strength,
        double drive) {
        grid.invalidate();
        transition.advance(elapsed);
        // Drained every frame so travel never accumulates while drive is 0.
        const Vec2 TRAVEL = transition.takeTravel();
        const double SHARE = std::isfinite(drive) ? std::clamp(drive, 0.0, 1.0) : 0.0;
        if (SHARE > 0.0 && (TRAVEL.x != 0.0 || TRAVEL.y != 0.0)) {
            mesh.recordTranslation({.x = TRAVEL.x * SHARE, .y = TRAVEL.y * SHARE}, size, strength);
        }
        mesh.advance(elapsed, size);
    }

    std::optional<Box> liquidVisibleBox(const LiquidPose &pose, const Box &logicalBox) {
        if (!pose.transition.active()) {
            return std::nullopt;
        }

        const auto &frame = pose.transition.frame();
        const Vec2 INWARD = inwardNormal(frame.edge);
        const Vec2 EDGE = liquidEdgePoint(pose, 0.0, 0.0);
        const double EDGE_X = logicalBox.x + (0.5 * logicalBox.w) + EDGE.x;
        const double EDGE_Y = logicalBox.y + (0.5 * logicalBox.h) + EDGE.y;

        double minX = 0.0;
        double maxX = 0.0;
        double minY = 0.0;
        double maxY = 0.0;
        visibleSpan(EDGE_X, INWARD.x, minX, maxX);
        visibleSpan(EDGE_Y, INWARD.y, minY, maxY);
        return Box{.x = minX, .y = minY, .w = maxX - minX, .h = maxY - minY};
    }

    std::optional<Box> liquidMeniscusExtents(const LiquidPose &pose, const Box &logicalBox) {
        const double LOW = pose.transition.meniscusLow();
        const double HIGH = pose.transition.meniscusHigh();
        const double PEAK = pose.transition.meniscusPeak();
        if (!(PEAK > 0.0) || !(HIGH > LOW)) {
            return std::nullopt;
        }

        const double CENTRE_X = logicalBox.x + (0.5 * logicalBox.w);
        const double CENTRE_Y = logicalBox.y + (0.5 * logicalBox.h);
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();
        for (const double t : {LOW, HIGH}) {
            for (const double n : {-LIQUID_BEHIND_EDGE, PEAK}) {
                const Vec2 p = liquidEdgePoint(pose, t, n);
                minX = std::min(minX, CENTRE_X + p.x);
                minY = std::min(minY, CENTRE_Y + p.y);
                maxX = std::max(maxX, CENTRE_X + p.x);
                maxY = std::max(maxY, CENTRE_Y + p.y);
            }
        }
        return Box{.x = minX, .y = minY, .w = maxX - minX, .h = maxY - minY};
    }

    Box liquidRenderExtents(const LiquidPose &pose, const Box &logicalBox, LiquidPoseGrid &grid) {
        const Vec2 SIZE{.x = logicalBox.w, .y = logicalBox.h};
        refreshLiquidPoseGrid(pose, SIZE, grid);

        const double CENTRE_X = logicalBox.x + (0.5 * logicalBox.w);
        const double CENTRE_Y = logicalBox.y + (0.5 * logicalBox.h);

        // Only displayed samples and the meniscus contribute; the undeformed box is not part of the
        // canvas.
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();
        for (const Vec2 p : grid.offsets) {
            minX = std::min(minX, CENTRE_X + p.x);
            minY = std::min(minY, CENTRE_Y + p.y);
            maxX = std::max(maxX, CENTRE_X + p.x);
            maxY = std::max(maxY, CENTRE_Y + p.y);
        }

        if (const auto MENISCUS = liquidMeniscusExtents(pose, logicalBox)) {
            minX = std::min(minX, MENISCUS->x);
            minY = std::min(minY, MENISCUS->y);
            maxX = std::max(maxX, MENISCUS->x + MENISCUS->w);
            maxY = std::max(maxY, MENISCUS->y + MENISCUS->h);
        }

        Box extents{.x = minX, .y = minY, .w = maxX - minX, .h = maxY - minY};
        if (const auto VISIBLE = liquidVisibleBox(pose, logicalBox)) {
            extents = intersectBoxes(extents, *VISIBLE);
            if (emptyBox(extents)) {
                // Nothing visible yet: a 1 px sliver on the screen side of the edge keeps the
                // damage path valid.
                const Vec2 INWARD = inwardNormal(pose.transition.frame().edge);
                const Vec2 EDGE = liquidEdgePoint(pose, 0.0, 0.0);
                extents = Box{.x = CENTRE_X + EDGE.x + INWARD.x,
                    .y = CENTRE_Y + EDGE.y + INWARD.y,
                    .w = 1.0,
                    .h = 1.0};
            }
        }
        return extents;
    }

    Box projectToOutput(const Box &extentsDevice, const Box &renderBox, Vec2 snapshotSize) {
        const double kx = renderBox.w / snapshotSize.x;
        const double ky = renderBox.h / snapshotSize.y;
        return Box{.x = renderBox.x + (extentsDevice.x * kx),
            .y = renderBox.y + (extentsDevice.y * ky),
            .w = extentsDevice.w * kx,
            .h = extentsDevice.h * ky};
    }

    Box ownedRenderBox(const Box &sourceDevice, const Box &nativeBox, Vec2 snapshotSize) {
        const double kx = nativeBox.w / snapshotSize.x;
        const double ky = nativeBox.h / snapshotSize.y;
        const double mx = sourceDevice.x + (sourceDevice.w * 0.5);
        const double my = sourceDevice.y + (sourceDevice.h * 0.5);
        const double cx = nativeBox.x + (mx * kx);
        const double cy = nativeBox.y + (my * ky);
        return Box{.x = cx - mx, .y = cy - my, .w = snapshotSize.x, .h = snapshotSize.y};
    }

    namespace {

        // CBox semantics: a result that is empty on either axis keeps its origin but has both sizes
        // zeroed.
        Box normaliseEmpty(Box box) noexcept {
            if (box.w <= 0.0 || box.h <= 0.0) {
                box.w = 0.0;
                box.h = 0.0;
            }
            return box;
        }

    }  // namespace

    Box intersectBoxes(const Box &a, const Box &b) noexcept {
        const double x = std::max(a.x, b.x);
        const double y = std::max(a.y, b.y);
        return normaliseEmpty(Box{.x = x,
            .y = y,
            .w = std::min(a.x + a.w, b.x + b.w) - x,
            .h = std::min(a.y + a.h, b.y + b.h) - y});
    }

    void expandBox(Box &box, double padding) noexcept {
        box.x -= padding;
        box.y -= padding;
        box.w += 2.0 * padding;
        box.h += 2.0 * padding;
        box = normaliseEmpty(box);
    }

    bool emptyBox(const Box &box) noexcept {
        return box.w <= 0.0 || box.h <= 0.0;
    }

}  // namespace hyprimate
