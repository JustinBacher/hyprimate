#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <mdspan>
#include <optional>
#include <vector>

#include "shared/Core.hpp"
#include "window/WindowAnimationFacts.hpp"
#include "window/liquid/LiquidMesh.hpp"
#include "window/liquid/EdgeTransition.hpp"

namespace hyprimate {

    // Samples per side of the render lattice: LIQUID_RENDER_CELLS quads need one more sample than
    // quads.
    inline constexpr int LIQUID_POSE_SAMPLES = LIQUID_RENDER_CELLS + 1;
    // Columns across the meniscus strip; each column is two triangles.
    inline constexpr int LIQUID_MENISCUS_COLUMNS = 96;
    inline constexpr std::size_t LIQUID_RENDER_VERTEX_COUNT =
        static_cast<std::size_t>(LIQUID_RENDER_CELLS) * LIQUID_RENDER_CELLS * 6;
    inline constexpr std::size_t LIQUID_MENISCUS_VERTEX_COUNT =
        static_cast<std::size_t>(LIQUID_MENISCUS_COLUMNS) * 6;
    // The edge liquid reaches this far behind the screen edge so it meets the window without a gap.
    inline constexpr double LIQUID_BEHIND_EDGE = 2.0;

    // Vertex layout written by the fills. The plugin's render vertex has the same four float fields
    // and is checked there.
    struct LiquidVertex {
        float x = 0.F, y = 0.F, u = 0.F, v = 0.F;
    };

    // The pose a liquid window or closing snapshot is drawn with. Borrows the mesh and
    // transition, which are owned by the caller.
    struct LiquidPose {
        std::reference_wrapper<const LiquidMesh> mesh;
        std::reference_wrapper<const EdgeTransition> transition;
        Vec2 compensation{.x = 1.0, .y = 1.0};
        Vec2 open_offset{.x = 0.0, .y = 0.0};
        double displacement_gain = 1.0;
        bool box_pose = false;
    };

    // Pose offsets (logical px from the box centre) at every render sample, for one logical size.
    // Owned by the caller next to the mesh it samples. The owner MUST call invalidate() whenever
    // any pose input changes: the mesh, the transition, compensation, open_offset,
    // displacement_gain or box_pose. The grid rebuilds itself when the size changes.
    struct LiquidPoseGrid {
        std::array<Vec2, static_cast<std::size_t>(LIQUID_POSE_SAMPLES) * LIQUID_POSE_SAMPLES>
            offsets{};
        Vec2 size{};
        bool stale = true;

        void invalidate() noexcept {
            stale = true;
        }
    };

    // Rebuilds grid for pose at size when it is stale or the size differs.
    void refresh_liquid_pose_grid(const LiquidPose &pose, Vec2 size, LiquidPoseGrid &grid);

    // Movement-field gain for `pose` at `rest_size`: the largest of 1, 1/2, 1/4, ... (0 when none
    // qualifies) at which every triangle of the render lattice keeps >= 20% of its gain-0 area.
    // Ignores pose.displacement_gain; 1 when the transition is idle or the mesh settled. With a
    // non-null `grid` the grid is filled with the offsets of `pose` at the returned gain (exactly
    // what refresh_liquid_pose_grid writes), so the caller does not sample the grid again; a grid
    // is only invalidated where no field is produced.
    double fit_liquid_pose_gain(const LiquidPose &pose, Vec2 rest_size, LiquidPoseGrid *grid);

    struct LiquidStrength {
        double value;
    };

    struct LiquidDrive {
        double value;
    };

    // Steps the mesh and transition by elapsed seconds, recording drive travel. Invalidates the
    // grid.
    void advance_liquid_pose(LiquidMesh &mesh,
        EdgeTransition &transition,
        LiquidPoseGrid &grid,
        double elapsed,
        Vec2 size,
        LiquidStrength strength,
        LiquidDrive drive);

    // Logical-px bounds of the visible half-plane on the screen side of the edge; nullopt when the
    // transition is idle.
    std::optional<Box> liquid_visible_box(const LiquidPose &pose, const Box &logical_box);

    // Logical-px bounds of the meniscus strip; nullopt when there is no meniscus.
    std::optional<Box> liquid_meniscus_extents(const LiquidPose &pose, const Box &logical_box);

    // Logical-px bounds of the displayed window: the render samples and meniscus, clipped to the
    // visible half-plane.
    Box liquid_render_extents(const LiquidPose &pose, const Box &logical_box, LiquidPoseGrid &grid);

    // Edge liquid point relative to the box centre, in logical px, including the open offset.
    Vec2 liquid_edge_point(const LiquidPose &pose, double tangent, double n);

    // Maps snapshot-space texels into the output: F(p) = B.pos + p * B.size / T.
    Box project_to_output(const Box &extents_device, const Box &render_box, Vec2 snapshot_size);

    // Owned placement: the snapshot at unit scale (device px), with the window's centre riding the
    // native fadeout's centre.
    Box owned_render_box(const Box &source_device, const Box &native_box, Vec2 snapshot_size);

    // Box helpers matching the clipping and padding the liquid paths use.
    Box intersect_boxes(const Box &first, const Box &second) noexcept;
    void expand_box(Box &box, double padding) noexcept;

    struct LiquidMeshGeometry {
        std::optional<Box> visible;
        std::optional<Box> meniscus;
    };

    // Logical geometry is separate from device rounding, which differs for live and close.
    inline LiquidMeshGeometry liquid_mesh_geometry(const LiquidPose &pose, const Box &logical_box) {
        return {.visible = liquid_visible_box(pose, logical_box),
            .meniscus = liquid_meniscus_extents(pose, logical_box)};
    }
    bool empty_box(const Box &box) noexcept;

    // Places the render mesh. source_device is the window in device px, output_device the target in
    // device px, and texture_origin/texture_size the texture in device px.
    template <class Vertex>
    void fill_liquid_render_mesh(const LiquidPose &pose,
        const Box &source_device,
        double scale,
        const Box &output_device,
        Vec2 texture_origin,
        Vec2 texture_size,
        LiquidPoseGrid &grid,
        std::vector<Vertex> &out) {
        if (out.size() != LIQUID_RENDER_VERTEX_COUNT) {
            out.resize(LIQUID_RENDER_VERTEX_COUNT);
        }

        refresh_liquid_pose_grid(
            pose, Vec2{.x = source_device.w / scale, .y = source_device.h / scale}, grid);

        const double CENTRE_X = source_device.x + (0.5 * source_device.w);
        const double CENTRE_Y = source_device.y + (0.5 * source_device.h);

        // Each grid sample is placed once, then reused by its neighbouring triangles.
        std::array<LiquidVertex,
            static_cast<std::size_t>(LIQUID_POSE_SAMPLES) * LIQUID_POSE_SAMPLES>
            samples;
        using PoseExtents = std::extents<std::size_t, LIQUID_POSE_SAMPLES, LIQUID_POSE_SAMPLES>;
        const std::mdspan<const Vec2, PoseExtents, std::layout_left> OFFSETS{grid.offsets.data()};
        const std::mdspan<LiquidVertex, PoseExtents, std::layout_left> SAMPLE_GRID{samples.data()};
        const std::mdspan<const LiquidVertex, PoseExtents, std::layout_left> CONST_SAMPLES{
            samples.data()};
        for (int j = 0; j < LIQUID_POSE_SAMPLES; ++j) {
            for (int i = 0; i < LIQUID_POSE_SAMPLES; ++i) {
                const double TEX_U = static_cast<double>(i) / LIQUID_RENDER_CELLS;
                const double TEX_V = static_cast<double>(j) / LIQUID_RENDER_CELLS;
                const Vec2 OFFSET = OFFSETS[i, j];
                SAMPLE_GRID[i, j] = {
                    .x = static_cast<float>(
                        (CENTRE_X + (OFFSET.x * scale) - output_device.x) / output_device.w),
                    .y = static_cast<float>(
                        (CENTRE_Y + (OFFSET.y * scale) - output_device.y) / output_device.h),
                    .u = static_cast<float>(
                        (source_device.x + (source_device.w * TEX_U) - texture_origin.x) /
                        texture_size.x),
                    .v = static_cast<float>(
                        (source_device.y + (source_device.h * TEX_V) - texture_origin.y) /
                        texture_size.y),
                };
            }
        }

        constexpr std::array<std::array<int, 2>, 6> ORDER{
            {{0, 0}, {0, 1}, {1, 0}, {1, 0}, {0, 1}, {1, 1}},
        };
        auto output = out.begin();
        for (int cj = 0; cj < LIQUID_RENDER_CELLS; ++cj) {
            for (int ci = 0; ci < LIQUID_RENDER_CELLS; ++ci) {
                for (const auto &[CORNER_X, CORNER_Y] : ORDER) {
                    Vertex &vertex = *output++;
                    const LiquidVertex &sample = CONST_SAMPLES[ci + CORNER_X, cj + CORNER_Y];
                    vertex.x = sample.x;
                    vertex.y = sample.y;
                    vertex.u = sample.u;
                    vertex.v = sample.v;
                }
            }
        }
    }

    // Places the meniscus strip in output_device. Texture coordinates are a flat centre sample.
    template <class Vertex>
    void fill_liquid_meniscus_mesh(const LiquidPose &pose,
        const Box &source_device,
        double scale,
        const Box &output_device,
        std::vector<Vertex> &out) {
        if (out.size() != LIQUID_MENISCUS_VERTEX_COUNT) {
            out.resize(LIQUID_MENISCUS_VERTEX_COUNT);
        }

        const double CENTRE_X = source_device.x + (0.5 * source_device.w);
        const double CENTRE_Y = source_device.y + (0.5 * source_device.h);
        const double LOW = pose.transition.get().meniscus_low();
        const double SPAN = pose.transition.get().meniscus_high() - LOW;

        const auto PLACE = [&](Vertex &vertex, double tangent, double n) {
            const Vec2 POSITION = liquid_edge_point(pose, tangent, n);
            vertex.x = static_cast<float>(
                (CENTRE_X + (POSITION.x * scale) - output_device.x) / output_device.w);
            vertex.y = static_cast<float>(
                (CENTRE_Y + (POSITION.y * scale) - output_device.y) / output_device.h);
            vertex.u = 0.5F;
            vertex.v = 0.5F;
        };

        auto output = out.begin();
        for (int i = 0; i < LIQUID_MENISCUS_COLUMNS; ++i) {
            const double TANGENT_START =
                LOW + (SPAN * static_cast<double>(i) / LIQUID_MENISCUS_COLUMNS);
            const double TANGENT_END =
                LOW + (SPAN * static_cast<double>(i + 1) / LIQUID_MENISCUS_COLUMNS);
            const double HEIGHT_START = pose.transition.get().meniscus(TANGENT_START);
            const double HEIGHT_END = pose.transition.get().meniscus(TANGENT_END);

            // Corners: a = behind edge at t0, b = behind edge at t1, c = crest at t0, d = crest at
            // t1. Triangles abc, bdc.
            PLACE(*output++, TANGENT_START, -LIQUID_BEHIND_EDGE);
            PLACE(*output++, TANGENT_END, -LIQUID_BEHIND_EDGE);
            PLACE(*output++, TANGENT_START, HEIGHT_START);
            PLACE(*output++, TANGENT_END, -LIQUID_BEHIND_EDGE);
            PLACE(*output++, TANGENT_END, HEIGHT_END);
            PLACE(*output++, TANGENT_START, HEIGHT_START);
        }
    }

    // The caller supplies device-space clipping and the separately normalised meniscus box.
    // Returned storage is borrowed from target and stays valid until its next fill.
    template <animation::Vertex V>
    animation::Mesh<V> fill_liquid_mesh(const LiquidPose &pose,
        const animation::MeshTarget<V> &target,
        LiquidPoseGrid &grid,
        std::optional<Box> under_output,
        animation::Rgba under_color,
        std::optional<Box> clip) {
        fill_liquid_render_mesh(pose,
            target.source,
            target.scale,
            target.output,
            target.image_origin,
            target.image_size,
            grid,
            target.buffers->window);
        animation::Mesh<V> result{};
        result.window = &target.buffers->window;
        if (under_output) {
            fill_liquid_meniscus_mesh(
                pose, target.source, target.scale, *under_output, target.buffers->under);
            result.under = &target.buffers->under;
            result.under_output = *under_output;
            result.under_color = under_color;
        }
        result.clip = clip;
        return result;
    }

}  // namespace hyprimate
