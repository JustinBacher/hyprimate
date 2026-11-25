#include <limits>

#include "ConstructionClosePrivate.hpp"

namespace hyprimate::construction::close_detail {
    namespace {
        bool identity(const ConstructionQuad &q) noexcept {
            return q.origin == std::array{q.uv[0], q.uv[1], 0.F} &&
                   q.u == std::array{q.uv[2], 0.F, 0.F} && q.v == std::array{0.F, q.uv[3], 0.F};
        }

        struct SourceEdges {
            double left, top, right, bottom;
        };

        double seam_epsilon(double span) noexcept {
            return 4 * std::numeric_limits<float>::epsilon() * std::max(1.0, span);
        }

        // Adjacent partitions packed as separate float origins and widths can have a
        // roundoff-sized gap/overlap. Snap shared cuts once, then adjust pose by the same
        // fractions.
        void align_source_axis(std::span<SourceEdges> faces,
            double SourceEdges::*first,
            double SourceEdges::*last,
            double limit) noexcept {
            std::array<double *, 2 * Regional::MAX_VISIBLE_FACES> edges;
            std::size_t count = 0;
            for (auto &face : faces) {
                edges[count++] = &(face.*first);
                edges[count++] = &(face.*last);
            }
            const auto ordered = std::span{edges}.first(count);
            std::ranges::sort(ordered, {}, [](const double *edge) { return *edge; });
            for (std::size_t i = 0; i < ordered.size();) {
                const double edge = *ordered[i];
                std::size_t j = i + 1;
                while (j < ordered.size() && *ordered[j] - edge <= seam_epsilon(edge)) ++j;
                const double aligned = edge <= seam_epsilon(0)                          ? 0
                                       : limit - *ordered[j - 1] <= seam_epsilon(limit) ? limit
                                                                                        : edge;
                while (i < j) *ordered[i++] = aligned;
            }
        }
    }  // namespace

    bool import_close_faces(std::optional<std::span<const ConstructionQuad>> currently_visible,
        Box cap,
        std::array<Regional::VisibleFace, Regional::MAX_VISIBLE_FACES> &faces,
        std::size_t &count,
        Box &required_snapshot) noexcept {
        if (currently_visible) {
            count = currently_visible->size();
            if (count > faces.size()) return false;
            std::array<SourceEdges, Regional::MAX_VISIBLE_FACES> edges;
            const double eps_x = seam_epsilon(cap.w), eps_y = seam_epsilon(cap.h);
            const auto cuts = std::span{edges}.first(count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &q = (*currently_visible)[i];
                const Box source{q.uv[0], q.uv[1], q.uv[2], q.uv[3]};
                if (q.surface != ConstructionSurface::Client || !valid_box(source) ||
                    source.x < -seam_epsilon(0) || source.y < -seam_epsilon(0) ||
                    right(source) > cap.w + eps_x || bottom(source) > cap.h + eps_y)
                    return false;
                cuts[i] = {source.x, source.y, right(source), bottom(source)};
            }
            align_source_axis(cuts, &SourceEdges::left, &SourceEdges::right, cap.w);
            align_source_axis(cuts, &SourceEdges::top, &SourceEdges::bottom, cap.h);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &q = (*currently_visible)[i];
                const auto &cut = cuts[i];
                const Box source{cut.left, cut.top, cut.right - cut.left, cut.bottom - cut.top};
                if (!valid_box(source)) return false;
                const Vec3 u = unpack(q.u), v = unpack(q.v);
                faces[i] = {source,
                    unpack(q.origin) + u * ((source.x - q.uv[0]) / q.uv[2]) +
                        v * ((source.y - q.uv[1]) / q.uv[3]),
                    u * (source.w / q.uv[2]),
                    v * (source.h / q.uv[3])};
                if (!identity(q)) required_snapshot = united(required_snapshot, source);
            }
        } else {
            // The idle source is the genuine retained capture, not a synthetic body crop.
            faces[0] = Regional::VisibleFace::flat(cap);
        }
        return true;
    }
}  // namespace hyprimate::construction::close_detail
