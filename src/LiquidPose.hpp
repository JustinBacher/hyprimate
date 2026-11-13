#pragma once

#include "Core.hpp"
#include "EdgeTransition.hpp"
#include "LiquidMesh.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace liquid_glass {

// Samples per side of the render lattice: LIQUID_RENDER_CELLS quads need one more sample than quads.
inline constexpr int LIQUID_POSE_SAMPLES = LIQUID_RENDER_CELLS + 1;
// Columns across the meniscus strip; each column is two triangles.
inline constexpr int         LIQUID_MENISCUS_COLUMNS = 96;
inline constexpr std::size_t LIQUID_RENDER_VERTEX_COUNT =
    static_cast<std::size_t>(LIQUID_RENDER_CELLS) * LIQUID_RENDER_CELLS * 6;
inline constexpr std::size_t LIQUID_MENISCUS_VERTEX_COUNT = static_cast<std::size_t>(LIQUID_MENISCUS_COLUMNS) * 6;
// The edge liquid reaches this far behind the screen edge so it meets the window without a gap.
inline constexpr double LIQUID_BEHIND_EDGE = 2.0;

// Vertex layout written by the fills. The plugin's render vertex has the same four float fields and is checked there.
struct LiquidVertex {
    float x = 0.F, y = 0.F, u = 0.F, v = 0.F;
};

// The pose a liquid window or closing snapshot is drawn with. Holds references: the mesh and transition are owned by
// the caller.
struct LiquidPose {
    const LiquidMesh     &mesh;
    const EdgeTransition &transition;
    Vec2                  compensation{1.0, 1.0};
    Vec2                  openOffset{0.0, 0.0};
    double                displacementGain = 1.0;
    bool                  boxPose = false;
};

// Pose offsets (logical px from the box centre) at every render sample, for one logical size. Owned by the caller next
// to the mesh it samples. The owner MUST call invalidate() whenever any pose input changes: the mesh, the transition,
// compensation, openOffset, displacementGain or boxPose. The grid rebuilds itself when the size changes.
struct LiquidPoseGrid {
    std::array<Vec2, static_cast<std::size_t>(LIQUID_POSE_SAMPLES) * LIQUID_POSE_SAMPLES> offsets{};
    Vec2                                                                                  size{};
    bool                                                                                  stale = true;

    void invalidate() noexcept { stale = true; }
};

// Rebuilds grid for pose at size when it is stale or the size differs.
void refreshLiquidPoseGrid(const LiquidPose &pose, Vec2 size, LiquidPoseGrid &grid);

// Movement-field gain for `pose` at `restSize`: the largest of 1, 1/2, 1/4, ... (0 when none qualifies) at which every
// triangle of the render lattice keeps >= 20% of its gain-0 area. Ignores pose.displacementGain; 1 when the transition
// is idle or the mesh settled. With a non-null `grid` the grid is filled with the offsets of `pose` at the returned
// gain (exactly what refreshLiquidPoseGrid writes), so the caller does not sample the grid again; a grid is only
// invalidated where no field is produced.
double fitLiquidPoseGain(const LiquidPose &pose, Vec2 restSize, LiquidPoseGrid *grid);

// Steps the mesh and transition by elapsed seconds, recording drive travel. Invalidates the grid.
void advanceLiquidPose(LiquidMesh &mesh, EdgeTransition &transition, LiquidPoseGrid &grid, double elapsed, Vec2 size,
                       double strength, double drive);

// Logical-px bounds of the visible half-plane on the screen side of the edge; nullopt when the transition is idle.
std::optional<Box> liquidVisibleBox(const LiquidPose &pose, const Box &logicalBox);

// Logical-px bounds of the meniscus strip; nullopt when there is no meniscus.
std::optional<Box> liquidMeniscusExtents(const LiquidPose &pose, const Box &logicalBox);

// Logical-px bounds of the displayed window: the render samples and meniscus, clipped to the visible half-plane.
Box liquidRenderExtents(const LiquidPose &pose, const Box &logicalBox, LiquidPoseGrid &grid);

// Edge liquid point relative to the box centre, in logical px, including the open offset.
Vec2 liquidEdgePoint(const LiquidPose &pose, double t, double n);

// Maps snapshot-space texels into the output: F(p) = B.pos + p * B.size / T.
Box projectToOutput(const Box &extentsDevice, const Box &renderBox, Vec2 snapshotSize);

// Owned placement: the snapshot at unit scale (device px), with the window's centre riding the native fadeout's centre.
Box ownedRenderBox(const Box &sourceDevice, const Box &nativeBox, Vec2 snapshotSize);

// Box helpers matching the clipping and padding the liquid paths use.
Box  intersectBoxes(const Box &a, const Box &b) noexcept;
void expandBox(Box &box, double padding) noexcept;
bool emptyBox(const Box &box) noexcept;

// Places the render mesh. sourceDevice is the window in device px, outputDevice the target in device px, and
// textureOrigin/textureSize the texture in device px.
template <class Vertex>
void fillLiquidRenderMesh(const LiquidPose &pose, const Box &sourceDevice, double scale, const Box &outputDevice,
                          Vec2 textureOrigin, Vec2 textureSize, LiquidPoseGrid &grid, std::vector<Vertex> &out) {
    if (out.size() != LIQUID_RENDER_VERTEX_COUNT)
        out.resize(LIQUID_RENDER_VERTEX_COUNT);

    refreshLiquidPoseGrid(pose, Vec2{sourceDevice.w / scale, sourceDevice.h / scale}, grid);

    const double centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double centreY = sourceDevice.y + 0.5 * sourceDevice.h;

    // Each grid sample is placed once, then reused by its neighbouring triangles.
    std::array<LiquidVertex, static_cast<std::size_t>(LIQUID_POSE_SAMPLES) * LIQUID_POSE_SAMPLES> samples;
    for (int j = 0; j < LIQUID_POSE_SAMPLES; ++j) {
        for (int i = 0; i < LIQUID_POSE_SAMPLES; ++i) {
            const double      u = static_cast<double>(i) / LIQUID_RENDER_CELLS;
            const double      v = static_cast<double>(j) / LIQUID_RENDER_CELLS;
            const std::size_t index = static_cast<std::size_t>(j) * LIQUID_POSE_SAMPLES + i;
            const Vec2        p = grid.offsets[index];
            samples[index] = {
                static_cast<float>((centreX + p.x * scale - outputDevice.x) / outputDevice.w),
                static_cast<float>((centreY + p.y * scale - outputDevice.y) / outputDevice.h),
                static_cast<float>((sourceDevice.x + sourceDevice.w * u - textureOrigin.x) / textureSize.x),
                static_cast<float>((sourceDevice.y + sourceDevice.h * v - textureOrigin.y) / textureSize.y),
            };
        }
    }

    constexpr int ORDER[6][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 0}, {0, 1}, {1, 1}};
    std::size_t   k = 0;
    for (int cj = 0; cj < LIQUID_RENDER_CELLS; ++cj) {
        for (int ci = 0; ci < LIQUID_RENDER_CELLS; ++ci) {
            for (const auto &corner : ORDER) {
                Vertex             &vertex = out[k++];
                const LiquidVertex &sample =
                    samples[static_cast<std::size_t>(cj + corner[1]) * LIQUID_POSE_SAMPLES + ci + corner[0]];
                vertex.x = sample.x;
                vertex.y = sample.y;
                vertex.u = sample.u;
                vertex.v = sample.v;
            }
        }
    }
}

// Places the meniscus strip in outputDevice. Texture coordinates are a flat centre sample.
template <class Vertex>
void fillLiquidMeniscusMesh(const LiquidPose &pose, const Box &sourceDevice, double scale, const Box &outputDevice,
                            std::vector<Vertex> &out) {
    if (out.size() != LIQUID_MENISCUS_VERTEX_COUNT)
        out.resize(LIQUID_MENISCUS_VERTEX_COUNT);

    const double centreX = sourceDevice.x + 0.5 * sourceDevice.w;
    const double centreY = sourceDevice.y + 0.5 * sourceDevice.h;
    const double low = pose.transition.meniscusLow();
    const double span = pose.transition.meniscusHigh() - low;

    const auto place = [&](Vertex &vertex, double t, double n) {
        const Vec2 p = liquidEdgePoint(pose, t, n);
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
        Vertex *quad = &out[static_cast<std::size_t>(i) * 6];
        place(quad[0], t0, -LIQUID_BEHIND_EDGE);
        place(quad[1], t1, -LIQUID_BEHIND_EDGE);
        place(quad[2], t0, h0);
        place(quad[3], t1, -LIQUID_BEHIND_EDGE);
        place(quad[4], t1, h1);
        place(quad[5], t0, h0);
    }
}

} // namespace liquid_glass
