#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mdspan>
#include <optional>
#include <span>

#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"

namespace hyprimate {

    namespace {

        using PoseExtents = std::extents<std::size_t, LIQUID_POSE_SAMPLES, LIQUID_POSE_SAMPLES>;
        using PoseView = std::mdspan<Vec2, PoseExtents, std::layout_left>;
        using ConstPoseView = std::mdspan<const Vec2, PoseExtents, std::layout_left>;

        // Half-plane extent in logical px; far larger than any monitor.
        constexpr double HUGE_SPAN = 1.0e6;
        // Grid sizes differing by less than this are the same sampling: device/scale round trips
        // move the size by ulps.
        constexpr double SIZE_TOLERANCE = 1.0e-9;

        // Gain-search constants: a rung is kept while every triangle keeps this much of its gain-0
        // area.
        constexpr double MIN_AREA_FRACTION = 0.2;
        constexpr int GAIN_HALVINGS = 8;

        double triangle_area(Vec2 first, Vec2 second, Vec2 third) {
            return 0.5 * (((second.x - first.x) * (third.y - first.y)) -
                             ((second.y - first.y) * (third.x - first.x)));
        }

        // Window sample offset from the box centre, in logical px, for a sample at rest offset
        // `base` with movement flow `flow` at `gain`. `map` holds the constants for the pose's
        // scaled size and is unused by a box pose. A box-driven open leaves only the movement
        // field: the real window box already is the pop-out. Otherwise (a retained closing
        // snapshot) the pop-out maps the rest box and open_offset shifts it.
        Vec2 sample_offset(const LiquidPose &pose,
            const EdgeTransition::RestMap &map,
            Vec2 base,
            Vec2 flow,
            double gain) {
            if (pose.box_pose) {
                return {.x = base.x + (gain * flow.x), .y = base.y + (gain * flow.y)};
            }
            const Vec2 COMPENSATION = pose.compensation;
            const Vec2 MAPPED = map.map({
                .x = (base.x + (gain * flow.x)) * COMPENSATION.x,
                .y = (base.y + (gain * flow.y)) * COMPENSATION.y,
            });
            return {.x = MAPPED.x + pose.open_offset.x, .y = MAPPED.y + pose.open_offset.y};
        }

        struct VisibleAxis {
            double edge;
            double inward;
        };

        struct VisibleSpan {
            double lo;
            double hi;
        };

        // Span along one axis of the visible half-plane: the screen side of the edge line.
        VisibleSpan visible_span(VisibleAxis axis) noexcept {
            if (axis.inward > 0.0) {
                return {.lo = axis.edge, .hi = axis.edge + HUGE_SPAN};
            }
            if (axis.inward < 0.0) {
                return {.lo = axis.edge - HUGE_SPAN, .hi = axis.edge};
            }
            return {.lo = axis.edge - HUGE_SPAN, .hi = axis.edge + HUGE_SPAN};
        }

    }  // namespace

    Vec2 liquid_edge_point(const LiquidPose &pose, double tangent, double n) {
        const Vec2 POSITION = edge_point(pose.transition.get().frame(), tangent, n);
        return {.x = POSITION.x + pose.open_offset.x, .y = POSITION.y + pose.open_offset.y};
    }

    void refresh_liquid_pose_grid(const LiquidPose &pose, Vec2 size, LiquidPoseGrid &grid) {
        if (!grid.stale && std::abs(grid.size.x - size.x) <= SIZE_TOLERANCE &&
            std::abs(grid.size.y - size.y) <= SIZE_TOLERANCE) {
            return;
        }

        const Vec2 COMPENSATION = pose.compensation;
        const EdgeTransition::RestMap MAP = pose.transition.get().rest_map(
            {.x = size.x * COMPENSATION.x, .y = size.y * COMPENSATION.y});
        for (int j = 0; j < LIQUID_POSE_SAMPLES; ++j) {
            const auto ROW = std::span{grid.offsets}.subspan(
                static_cast<std::size_t>(j) * LIQUID_POSE_SAMPLES, LIQUID_POSE_SAMPLES);
            auto sample = ROW.begin();
            for (int i = 0; i < LIQUID_POSE_SAMPLES; ++i) {
                const double TEX_U = static_cast<double>(i) / LIQUID_RENDER_CELLS;
                const double TEX_V = static_cast<double>(j) / LIQUID_RENDER_CELLS;
                const Vec2 BASE{.x = (TEX_U - 0.5) * size.x, .y = (TEX_V - 0.5) * size.y};
                *sample++ = sample_offset(pose,
                    MAP,
                    BASE,
                    pose.mesh.get().sample_displacement(TEX_U, TEX_V),
                    pose.displacement_gain);
            }
        }
        grid.size = size;
        grid.stale = false;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve gain ladder and FP order without moving the large scratch arrays.
    double fit_liquid_pose_gain(const LiquidPose &pose, Vec2 rest_size, LiquidPoseGrid *grid) {
        // Only an accepted field reaches the grid; any other outcome leaves the grid to be rebuilt
        // lazily.
        const auto SETTLE = [grid](double gain) {
            if (grid) {
                grid->invalidate();
            }
            return gain;
        };
        const EdgeTransition &transition = pose.transition.get();
        if (!transition.active() || pose.mesh.get().settled()) {
            return SETTLE(1.0);
        }
        const Vec2 COMPENSATION = pose.compensation;
        if (!std::isfinite(rest_size.x) || !std::isfinite(rest_size.y) ||
            !std::isfinite(COMPENSATION.x) || !std::isfinite(COMPENSATION.y) ||
            rest_size.x <= 0.0 || rest_size.y <= 0.0 || COMPENSATION.x <= 0.0 ||
            COMPENSATION.y <= 0.0) {
            return SETTLE(0.0);
        }

        // Same composition as the renderer: the displayed sample is map(((u-0.5)*S + g*d) * c,
        // S*c).
        const EdgeTransition::RestMap MAP = transition.rest_map(
            {.x = rest_size.x * COMPENSATION.x, .y = rest_size.y * COMPENSATION.y});
        constexpr int K_SIDE = LIQUID_POSE_SAMPLES;
        constexpr int K_VERTICES = K_SIDE * K_SIDE;
        std::array<Vec2, K_VERTICES> base{};
        std::array<Vec2, K_VERTICES> flow{};
        std::array<Vec2, K_VERTICES> map_only{};
        std::array<Vec2, K_VERTICES> moved{};
        const PoseView BASE_GRID{base.data()};
        const PoseView FLOW_GRID{flow.data()};
        const PoseView MAP_ONLY_GRID{map_only.data()};
        const ConstPoseView BASE_VALUES{base.data()};
        const ConstPoseView FLOW_VALUES{flow.data()};
        for (int j = 0; j < K_SIDE; ++j) {
            for (int i = 0; i < K_SIDE; ++i) {
                const double TEX_U = static_cast<double>(i) / LIQUID_RENDER_CELLS;
                const double TEX_V = static_cast<double>(j) / LIQUID_RENDER_CELLS;
                BASE_GRID[i, j] = {
                    .x = (TEX_U - 0.5) * rest_size.x,
                    .y = (TEX_V - 0.5) * rest_size.y,
                };
                FLOW_GRID[i, j] = pose.mesh.get().sample_displacement(TEX_U, TEX_V);
                MAP_ONLY_GRID[i, j] =
                    sample_offset(pose, MAP, BASE_VALUES[i, j], FLOW_VALUES[i, j], 0.0);
            }
        }

        // Every triangle keeps MIN_AREA_FRACTION of its gain-0 area.
        const auto KEEPS_AREA = [&](const std::array<Vec2, K_VERTICES> &field) {
            for (int j = 0; j + 1 < K_SIDE; ++j) {
                for (int i = 0; i + 1 < K_SIDE; ++i) {
                    const int P00 = (j * K_SIDE) + i;
                    const int P10 = P00 + 1;
                    const int P01 = P00 + K_SIDE;
                    const int P11 = P01 + 1;
                    const std::array TRIS{std::array{P00, P10, P01}, std::array{P10, P11, P01}};
                    for (const auto &tri_indices : TRIS) {
                        const double BEFORE = triangle_area(map_only[std::get<0>(tri_indices)],
                            map_only[std::get<1>(tri_indices)],
                            map_only[std::get<2>(tri_indices)]);
                        if (!(BEFORE > 0.0)) {
                            continue;  // degenerate at rest: nothing to keep
                        }
                        const double AFTER = triangle_area(field[std::get<0>(tri_indices)],
                            field[std::get<1>(tri_indices)],
                            field[std::get<2>(tri_indices)]);
                        if (!(AFTER >= MIN_AREA_FRACTION * BEFORE)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };
        // The grid's offsets are exactly this field at `gain`.
        const auto PUBLISH = [grid, &rest_size](
                                 const std::array<Vec2, K_VERTICES> &field, double gain) {
            if (grid) {
                grid->offsets = field;
                grid->size = rest_size;
                grid->stale = false;
            }
            return gain;
        };
        for (int halving = 0; halving <= GAIN_HALVINGS; ++halving) {
            const double GAIN = std::ldexp(1.0, -halving);
            const auto *base_sample = base.begin();
            const auto *flow_sample = flow.begin();
            for (auto &sample : moved) {
                sample = sample_offset(pose, MAP, *base_sample++, *flow_sample++, GAIN);
            }
            if (KEEPS_AREA(moved)) {
                return PUBLISH(moved, GAIN);
            }
        }
        return PUBLISH(map_only, 0.0);
    }

    void advance_liquid_pose(LiquidMesh &mesh,
        EdgeTransition &transition,
        LiquidPoseGrid &grid,
        double elapsed,
        Vec2 size,
        LiquidStrength strength,
        LiquidDrive drive) {
        grid.invalidate();
        transition.advance(elapsed);
        // Drained every frame so travel never accumulates while drive is 0.
        const Vec2 TRAVEL = transition.take_travel();
        const double SHARE = std::isfinite(drive.value) ? std::clamp(drive.value, 0.0, 1.0) : 0.0;
        if (SHARE > 0.0 && (TRAVEL.x != 0.0 || TRAVEL.y != 0.0)) {
            mesh.record_translation(
                {.x = TRAVEL.x * SHARE, .y = TRAVEL.y * SHARE}, size, strength.value);
        }
        mesh.advance(elapsed, size);
    }

    std::optional<Box> liquid_visible_box(const LiquidPose &pose, const Box &logical_box) {
        if (!pose.transition.get().active()) {
            return std::nullopt;
        }

        const auto &frame = pose.transition.get().frame();
        const Vec2 INWARD = inward_normal(frame.edge);
        const Vec2 EDGE = liquid_edge_point(pose, 0.0, 0.0);
        const double EDGE_X = logical_box.x + (0.5 * logical_box.w) + EDGE.x;
        const double EDGE_Y = logical_box.y + (0.5 * logical_box.h) + EDGE.y;

        const VisibleSpan X_SPAN = visible_span({.edge = EDGE_X, .inward = INWARD.x});
        const VisibleSpan Y_SPAN = visible_span({.edge = EDGE_Y, .inward = INWARD.y});
        return Box{
            .x = X_SPAN.lo,
            .y = Y_SPAN.lo,
            .w = X_SPAN.hi - X_SPAN.lo,
            .h = Y_SPAN.hi - Y_SPAN.lo,
        };
    }

    std::optional<Box> liquid_meniscus_extents(const LiquidPose &pose, const Box &logical_box) {
        const double LOW = pose.transition.get().meniscus_low();
        const double HIGH = pose.transition.get().meniscus_high();
        const double PEAK = pose.transition.get().meniscus_peak();
        if (!(PEAK > 0.0) || !(HIGH > LOW)) {
            return std::nullopt;
        }

        const double CENTRE_X = logical_box.x + (0.5 * logical_box.w);
        const double CENTRE_Y = logical_box.y + (0.5 * logical_box.h);
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();
        for (const double TANGENT : {LOW, HIGH}) {
            for (const double NORMAL : {-LIQUID_BEHIND_EDGE, PEAK}) {
                const Vec2 POSITION = liquid_edge_point(pose, TANGENT, NORMAL);
                min_x = std::min(min_x, CENTRE_X + POSITION.x);
                min_y = std::min(min_y, CENTRE_Y + POSITION.y);
                max_x = std::max(max_x, CENTRE_X + POSITION.x);
                max_y = std::max(max_y, CENTRE_Y + POSITION.y);
            }
        }
        return Box{.x = min_x, .y = min_y, .w = max_x - min_x, .h = max_y - min_y};
    }

    Box liquid_render_extents(
        const LiquidPose &pose, const Box &logical_box, LiquidPoseGrid &grid) {
        const Vec2 SIZE{.x = logical_box.w, .y = logical_box.h};
        refresh_liquid_pose_grid(pose, SIZE, grid);

        const double CENTRE_X = logical_box.x + (0.5 * logical_box.w);
        const double CENTRE_Y = logical_box.y + (0.5 * logical_box.h);

        // Only displayed samples and the meniscus contribute; the undeformed box is not part of the
        // canvas.
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();
        for (const Vec2 OFFSET : grid.offsets) {
            min_x = std::min(min_x, CENTRE_X + OFFSET.x);
            min_y = std::min(min_y, CENTRE_Y + OFFSET.y);
            max_x = std::max(max_x, CENTRE_X + OFFSET.x);
            max_y = std::max(max_y, CENTRE_Y + OFFSET.y);
        }

        if (const auto MENISCUS = liquid_meniscus_extents(pose, logical_box)) {
            min_x = std::min(min_x, MENISCUS->x);
            min_y = std::min(min_y, MENISCUS->y);
            max_x = std::max(max_x, MENISCUS->x + MENISCUS->w);
            max_y = std::max(max_y, MENISCUS->y + MENISCUS->h);
        }

        Box extents{.x = min_x, .y = min_y, .w = max_x - min_x, .h = max_y - min_y};
        if (const auto VISIBLE = liquid_visible_box(pose, logical_box)) {
            extents = intersect_boxes(extents, *VISIBLE);
            if (empty_box(extents)) {
                // Nothing visible yet: a 1 px sliver on the screen side of the edge keeps the
                // damage path valid.
                const Vec2 INWARD = inward_normal(pose.transition.get().frame().edge);
                const Vec2 EDGE = liquid_edge_point(pose, 0.0, 0.0);
                extents = Box{
                    .x = CENTRE_X + EDGE.x + INWARD.x,
                    .y = CENTRE_Y + EDGE.y + INWARD.y,
                    .w = 1.0,
                    .h = 1.0,
                };
            }
        }
        return extents;
    }

    Box project_to_output(const Box &extents_device, const Box &render_box, Vec2 snapshot_size) {
        const double SCALE_X = render_box.w / snapshot_size.x;
        const double SCALE_Y = render_box.h / snapshot_size.y;
        return Box{
            .x = render_box.x + (extents_device.x * SCALE_X),
            .y = render_box.y + (extents_device.y * SCALE_Y),
            .w = extents_device.w * SCALE_X,
            .h = extents_device.h * SCALE_Y,
        };
    }

    Box owned_render_box(const Box &source_device, const Box &native_box, Vec2 snapshot_size) {
        const double SCALE_X = native_box.w / snapshot_size.x;
        const double SCALE_Y = native_box.h / snapshot_size.y;
        const double MID_X = source_device.x + (source_device.w * 0.5);
        const double MID_Y = source_device.y + (source_device.h * 0.5);
        const double CENTRE_X = native_box.x + (MID_X * SCALE_X);
        const double CENTRE_Y = native_box.y + (MID_Y * SCALE_Y);
        return Box{
            .x = CENTRE_X - MID_X,
            .y = CENTRE_Y - MID_Y,
            .w = snapshot_size.x,
            .h = snapshot_size.y,
        };
    }

    namespace {

        // CBox semantics: a result that is empty on either axis keeps its origin but has both sizes
        // zeroed.
        Box normalise_empty(Box box) noexcept {
            if (box.w <= 0.0 || box.h <= 0.0) {
                box.w = 0.0;
                box.h = 0.0;
            }
            return box;
        }

    }  // namespace

    Box intersect_boxes(const Box &first, const Box &second) noexcept {
        const double MIN_X = std::max(first.x, second.x);
        const double MIN_Y = std::max(first.y, second.y);
        return normalise_empty(Box{
            .x = MIN_X,
            .y = MIN_Y,
            .w = std::min(first.x + first.w, second.x + second.w) - MIN_X,
            .h = std::min(first.y + first.h, second.y + second.h) - MIN_Y,
        });
    }

    void expand_box(Box &box, double padding) noexcept {
        box.x -= padding;
        box.y -= padding;
        box.w += 2.0 * padding;
        box.h += 2.0 * padding;
        box = normalise_empty(box);
    }

    bool empty_box(const Box &box) noexcept {
        return box.w <= 0.0 || box.h <= 0.0;
    }

}  // namespace hyprimate
