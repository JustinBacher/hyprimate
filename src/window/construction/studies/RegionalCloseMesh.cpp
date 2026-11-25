#include <limits>

#include "RegionalClosePrivate.hpp"

namespace hyprimate::construction::studies {
    namespace {
        using namespace regional_close_detail;

        std::span<const double> enclosing_grid(
            std::span<const double> grid, double first, double last) noexcept {
            const auto lo = std::ranges::upper_bound(grid, first) - 1;
            const auto hi = std::ranges::lower_bound(grid, last);
            return {lo, hi + 1};
        }

        using Vertex = RegionalClose::Vertex;
        struct ClipRect {
            double left, top, right, bottom;
        };

        struct Polygon {
            std::array<Vertex, 7> vertices;
            std::size_t count = 0;
        };

        void clip_half_plane(const Polygon &input,
            Polygon &out,
            double limit,
            double Vec2::*axis,
            bool keep_above) noexcept {
            out.count = 0;
            if (input.count < 3) return;
            const auto inside = [limit, keep_above](
                                    double x) { return keep_above ? x >= limit : x <= limit; };
            const Vertex *previous = &input.vertices[input.count - 1];
            for (std::size_t i = 0; i < input.count; ++i) {
                const auto &current = input.vertices[i];
                const double from = previous->source.*axis, to = current.source.*axis;
                if (inside(from) != inside(to) && from != limit && to != limit) {
                    // Interpolate each edge in one canonical order even if it is traversed
                    // backwards by the neighboring triangle.
                    const auto &low = from < to ? *previous : current;
                    const auto &high = from < to ? current : *previous;
                    const double t =
                        (limit - low.source.*axis) / (high.source.*axis - low.source.*axis);
                    auto &crossing = out.vertices[out.count++];
                    crossing = {{std::lerp(low.source.x, high.source.x, t),
                                    std::lerp(low.source.y, high.source.y, t)},
                        mix(low.position, high.position, t)};
                    crossing.source.*axis = limit;
                }
                if (inside(to)) out.vertices[out.count++] = current;
                previous = &current;
            }
        }

        Polygon clip_triangle(
            const Vertex &a, const Vertex &b, const Vertex &c, ClipRect crop) noexcept {
            Polygon polygon{{a, b, c}, 3}, scratch;
            clip_half_plane(polygon, scratch, crop.left, &Vec2::x, true);
            clip_half_plane(scratch, polygon, crop.right, &Vec2::x, false);
            clip_half_plane(polygon, scratch, crop.top, &Vec2::y, true);
            clip_half_plane(scratch, polygon, crop.bottom, &Vec2::y, false);
            return polygon;
        }
    }  // namespace

    bool RegionalClose::emit_mesh(SourceView view, Frame &out, double crop_right, double crop_bottom) const noexcept {
        const bool moving = m_progress > CUE + .018;
        std::array<double, MAX_REGIONS> lower, upper;
        if (moving) {
            for (std::size_t i = 0; i < m_region_count; ++i) {
                lower[i] = m_regions[i].lower.drop(m_progress);
                upper[i] = m_regions[i].upper.drop(m_progress);
            }
        }
        struct Column {
            double head, foot, knee;
        };
        const auto profile = [&](double x) -> Column {
            const double u = (x - m_body.x) / m_body.w;
            double head = upper[0], foot = lower[0];
            for (std::size_t i = 1; i < m_region_count; ++i) {
                const double edge = m_edges[i];
                const double span = .20 * std::min(edge - m_edges[i - 1], m_edges[i + 1] - edge);
                if (u <= edge - span) break;
                const double join = smoothstep(edge - span, edge + span, u);
                head = std::lerp(upper[i - 1], upper[i], join);
                foot = std::lerp(lower[i - 1], lower[i], join);
                if (join < 1) break;
            }
            // Tall edge takes hold their crown while the low shoulder goes first.
            // Keep that bite local so the crown catches a folding sheet, not a gap.
            const double distance = std::abs(u - m_origin);
            if (m_bite > 0)
                head = std::max(head, foot - (.012 + .095 * (1 - smoothstep(0, .37, distance))));
            const double fold = std::sin(12.7 * u + m_fold);
            const double knee = std::lerp(.765, .534, m_bite) + .103 * distance + .037 * fold;
            const double rake = .98 + .056 * fold;
            return {rake * head, rake * std::max(head, foot), knee};
        };
        const auto pose =
            [&](const VisibleFace &face, const Column &column, Vec2 source) -> Vertex {
            auto p = rest_point(face, source);
            const double v = (source.y - m_body.y) / m_body.h;
            const double bottom = smoothstep(column.knee - .075, column.knee + .075, v);
            const double drop = std::lerp(column.head, column.foot, bottom);
            if (drop == 0) return {source, p};
            const double rest_y = p.y;
            p.y = std::min(m_floor, p.y + m_fall_distance * drop);
            p.x = std::lerp(p.x, m_cue.x, .105 * smoothstep(.035, .76, drop));
            const double toe = smoothstep(.67, 1, v) * smoothstep(.035, .28, drop);
            const double clear = smoothstep(0, .14, (m_floor - p.y) / m_body.h);
            const double packed = smoothstep(0, 1, (p.y - rest_y) / (m_floor - rest_y));
            // The toe bends in depth but ends on one line; leave the saved pose untouched at
            // release.
            p.z = p.z * (1 - packed) - .108 * m_scale * toe * clear * (1 - clear);
            return {source, p};
        };
        constexpr auto NO_INDEX = std::numeric_limits<std::uint16_t>::max();
        struct Sample {
            Vertex vertex;
            std::uint16_t index;
        };
        const auto append_vertex = [&out](const Vertex &vertex) {
            const auto index = static_cast<std::uint16_t>(out.m_vertex_count);
            out.m_vertices[out.m_vertex_count++] = vertex;
            return index;
        };
        const auto index_vertex = [&append_vertex](Sample &sample) {
            if (sample.index == NO_INDEX) sample.index = append_vertex(sample.vertex);
            return sample.index;
        };
        const auto emit_triangle =
            [&](Sample &a, Sample &b, Sample &c, ClipRect allowed, bool clipped) {
                if (std::min({a.vertex.position.y, b.vertex.position.y, c.vertex.position.y}) >=
                    m_floor)
                    return;
                if (!clipped) {
                    out.m_faces[out.m_face_count++] = {
                        index_vertex(a), index_vertex(b), index_vertex(c)};
                    return;
                }
                const Polygon polygon = clip_triangle(a.vertex, b.vertex, c.vertex, allowed);
                if (polygon.count < 3) return;
                const auto start = out.m_vertex_count;
                const auto first_face = out.m_face_count;
                for (std::size_t j = 0; j < polygon.count; ++j) append_vertex(polygon.vertices[j]);
                const auto &origin = polygon.vertices[0];
                for (std::size_t j = 2; j < polygon.count; ++j) {
                    const auto &v = polygon.vertices[j - 1], &w = polygon.vertices[j];
                    const double area =
                        (v.source.x - origin.source.x) * (w.source.y - origin.source.y) -
                        (v.source.y - origin.source.y) * (w.source.x - origin.source.x);
                    if (area <= 0 ||
                        std::min({origin.position.y, v.position.y, w.position.y}) >= m_floor)
                        continue;
                    out.m_faces[out.m_face_count++] = {std::uint16_t(start),
                        std::uint16_t(start + j - 1),
                        std::uint16_t(start + j)};
                }
                if (out.m_face_count == first_face) out.m_vertex_count = start;
            };
        bool has_authorized_patch = false;
        for (std::size_t i = 0; i < m_patch_count; ++i) {
            const auto &patch = m_patches[i];
            // Use the actual endpoints, not reconstructed widths: only the four
            // authorization edges may split the fixed lattice.
            const auto &b = patch.visible;
            const ClipRect allowed{std::max(view.crop.x, b.x),
                std::max(view.crop.y, b.y),
                std::min(crop_right, b.x + b.w),
                std::min(crop_bottom, b.y + b.h)};
            if (allowed.left >= allowed.right || allowed.top >= allowed.bottom) continue;
            has_authorized_patch = true;
            if (!moving) {
                const auto vertex = [&](double x, double y) -> Vertex {
                    return {{x, y}, rest_point(patch.face, {x, y})};
                };
                const auto tl = append_vertex(vertex(allowed.left, allowed.top));
                const auto tr = append_vertex(vertex(allowed.right, allowed.top));
                const auto bl = append_vertex(vertex(allowed.left, allowed.bottom));
                const auto br = append_vertex(vertex(allowed.right, allowed.bottom));
                out.m_faces[out.m_face_count++] = {tl, tr, bl};
                out.m_faces[out.m_face_count++] = {bl, tr, br};
                continue;
            }
            const auto xs =
                enclosing_grid({m_x_edges.data(), m_x_count}, allowed.left, allowed.right);
            const auto ys =
                enclosing_grid({m_y_edges.data(), m_y_count}, allowed.top, allowed.bottom);
            std::array<Column, MAX_COLUMNS + 1> columns;
            for (std::size_t x = 0; x < xs.size(); ++x) columns[x] = profile(xs[x]);
            std::array<std::array<Sample, MAX_COLUMNS + 1>, 2> samples;
            for (std::size_t y = 0; y < ys.size(); ++y) {
                auto &current = samples[y & 1U];
                for (std::size_t x = 0; x < xs.size(); ++x)
                    current[x] = {pose(patch.face, columns[x], {xs[x], ys[y]}), NO_INDEX};
                if (y == 0) continue;
                auto &previous = samples[(y - 1) & 1U];
                const bool clipped_row = ys[y - 1] < allowed.top || ys[y] > allowed.bottom;
                for (std::size_t x = 1; x < xs.size(); ++x) {
                    // A cropped vertex interpolates the fixed posed triangle, not
                    // the nonlinear motion field. Keep the full-view diagonal.
                    const bool clipped =
                        clipped_row || xs[x - 1] < allowed.left || xs[x] > allowed.right;
                    emit_triangle(previous[x - 1], previous[x], current[x - 1], allowed, clipped);
                    emit_triangle(current[x - 1], previous[x], current[x], allowed, clipped);
                }
            }
        }
        return has_authorized_patch;
    }
}  // namespace hyprimate::construction::studies
