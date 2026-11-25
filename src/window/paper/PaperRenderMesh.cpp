#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <tuple>

#include "window/paper/PaperRenderMesh.hpp"
#include "window/paper/PaperRenderMeshMath.hpp"

namespace {
    using namespace paper_render_mesh_math;
    double segment_distance(Vec3 point, Vec3 begin, Vec3 end) {
        const auto EDGE = subtract(end, begin);
        const double LENGTH = dot(EDGE, EDGE);
        const double PARAM =
            LENGTH > 1e-24 ? std::clamp(dot(subtract(point, begin), EDGE) / LENGTH, 0.0, 1.0) : 0;
        const auto DELTA = subtract(point, add(begin, scale(EDGE, PARAM)));
        return dot(DELTA, DELTA);
    }
    double triangle_distance(Vec3 point, Vec3 first, Vec3 second, Vec3 third) {
        const auto EDGE_A = subtract(second, first), EDGE_B = subtract(third, first);
        const auto NORMAL = cross(EDGE_A, EDGE_B);
        const double AREA = dot(NORMAL, NORMAL);
        if (AREA > 1e-24) {
            const double HEIGHT = dot(subtract(point, first), NORMAL) / AREA;
            const auto PROJECTED = subtract(point, scale(NORMAL, HEIGHT));
            const auto RELATIVE = subtract(PROJECTED, first);
            const double AA = dot(EDGE_A, EDGE_A), AB = dot(EDGE_A, EDGE_B),
                         BB = dot(EDGE_B, EDGE_B);
            const double RA = dot(RELATIVE, EDGE_A), RB = dot(RELATIVE, EDGE_B);
            const double DENOMINATOR = AA * BB - AB * AB;
            const double U = (BB * RA - AB * RB) / DENOMINATOR,
                         V = (AA * RB - AB * RA) / DENOMINATOR;
            if (U >= 0 && V >= 0 && U + V <= 1) return HEIGHT * HEIGHT * AREA;
        }
        return std::min({segment_distance(point, first, second),
            segment_distance(point, second, third),
            segment_distance(point, third, first)});
    }
    Vec3 normal(const SPaperVertex &vertex) {
        return {vertex.nx, vertex.ny, vertex.nz};
    }
    SPaperVertex midpoint(const SPaperVertex &first, const SPaperVertex &second, Vec3 point) {
        const auto NORMAL = normalized(add(normal(first), normal(second)));
        return {static_cast<float>(point.x),
            static_cast<float>(point.y),
            static_cast<float>(point.z),
            (first.u + second.u) * .5F,
            (first.v + second.v) * .5F,
            static_cast<float>(NORMAL.x),
            static_cast<float>(NORMAL.y),
            static_cast<float>(NORMAL.z)};
    }
    bool finite(Vec3 p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    bool finite(hyprimate::Vec2 p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
    }
    double transmission(
        const hyprimate::PaperSurface &surface, std::size_t edge, std::size_t other_edge) {
        const double CREASE =
            std::clamp(static_cast<double>(std::max(surface.faces[edge / 3].crease[edge % 3],
                           surface.faces[other_edge / 3].crease[other_edge % 3])),
                0.0,
                1.0);
        return 1 - hyprimate::smoothstep(0, 1, CREASE);
    }
}  // namespace

void CPaperRenderMesh::topology(const hyprimate::PaperSurface &surface) {
    // Invalidate before any allocating mutation: a failed rebuild must never hit
    // the previous key with partially replaced adjacency on the next frame.
    m_faces.clear();
    const auto COUNT = surface.faces.size() * 3;
    m_neighbors.assign(COUNT, std::array<Neighbor, 2>{});
    m_heads.assign(surface.points.size(), SIZE_MAX);
    m_next.resize(COUNT);
    for (std::size_t face = 0; face < surface.faces.size(); ++face)
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto INDEX = face * 3 + corner;
            const auto VERTEX = surface.faces[face].vertex[corner];
            m_next[INDEX] = m_heads[VERTEX];
            m_heads[VERTEX] = INDEX;
        }
    m_edges.clear();
    m_edges.reserve(COUNT);
    for (std::size_t f = 0; f < surface.faces.size(); ++f) {
        const auto &FACE = surface.faces[f];
        for (std::size_t e = 0; e < 3; ++e) {
            auto a = FACE.vertex[e], b = FACE.vertex[(e + 1) % 3];
            auto first = f * 3 + e, second = f * 3 + (e + 1) % 3;
            if (a > b) {
                std::swap(a, b);
                std::swap(first, second);
            }
            m_edges.push_back({a, b, first, second, f * 3 + e});
        }
    }
    std::ranges::sort(m_edges,
        [](const Edge &a, const Edge &b) { return std::tie(a.a, a.b) < std::tie(b.a, b.b); });
    for (std::size_t begin = 0; begin < m_edges.size();) {
        auto end = begin + 1;
        while (end < m_edges.size() && m_edges[end].a == m_edges[begin].a &&
               m_edges[end].b == m_edges[begin].b)
            ++end;
        // Keep the manifold fan topology independent of crease strength. Per-frame
        // transmission weights, not binary smoothing islands, control its normals.
        if (end - begin == 2) {
            const auto &FIRST = m_edges[begin], &SECOND = m_edges[begin + 1];
            const auto LINK =
                [&](std::size_t from, std::size_t to, std::size_t edge, std::size_t other) {
                    auto &neighbors = m_neighbors[from];
                    auto &slot = neighbors[0].corner == SIZE_MAX ? neighbors[0] : neighbors[1];
                    slot = {to, edge, other};
                };
            LINK(FIRST.first, SECOND.first, FIRST.edge, SECOND.edge);
            LINK(SECOND.first, FIRST.first, SECOND.edge, FIRST.edge);
            LINK(FIRST.second, SECOND.second, FIRST.edge, SECOND.edge);
            LINK(SECOND.second, FIRST.second, SECOND.edge, FIRST.edge);
        }
        begin = end;
    }
    m_faces = surface.faces;
}

bool CPaperRenderMesh::build(const hyprimate::PaperSurface &surface) try {
    if (surface.faces.empty() ||
        surface.faces.size() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) / 12)
        return false;
    for (const auto &FACE : surface.faces)
        for (const auto v : FACE.vertex)
            if (v >= surface.points.size()) return false;
    for (const auto &POINT : surface.points)
        if (!finite(POINT.position) || !finite(POINT.uv)) return false;
    bool changed =
        m_faces.size() != surface.faces.size() || m_heads.size() != surface.points.size();
    if (!changed) {
        for (std::size_t f = 0; f < m_faces.size() && !changed; ++f) {
            changed = m_faces[f].vertex != surface.faces[f].vertex;
        }
    }
    if (changed) topology(surface);
    const auto COUNT = surface.faces.size() * 3;
    m_normals.resize(surface.faces.size());
    if (surface.interaction_mesh) {
        m_vertices.resize(COUNT);
    } else {
        m_weights.resize(COUNT);
        m_visited.resize(COUNT);
        m_corners.resize(COUNT);
    }
    for (std::size_t face = 0; face < surface.faces.size(); ++face) {
        const auto &VERTICES = surface.faces[face].vertex;
        m_normals[face] = cross(
            subtract(surface.points[VERTICES[1]].position, surface.points[VERTICES[0]].position),
            subtract(surface.points[VERTICES[2]].position, surface.points[VERTICES[0]].position));
    }
    for (std::size_t vertex = 0; vertex < m_heads.size(); ++vertex) {
        m_fan.clear();
        for (auto corner = m_heads[vertex]; corner != SIZE_MAX; corner = m_next[corner])
            m_fan.push_back(corner);
        if (surface.interaction_mesh) {
            Vec3 accumulated{};
            for (const auto corner : m_fan) accumulated = add(accumulated, m_normals[corner / 3]);
            const auto NORMAL = normalized(accumulated);
            const auto &POINT = surface.points[vertex];
            for (const auto corner : m_fan)
                m_vertices[corner] = {static_cast<float>(POINT.position.x),
                    static_cast<float>(POINT.position.y),
                    static_cast<float>(POINT.position.z),
                    static_cast<float>(POINT.uv.x),
                    static_cast<float>(POINT.uv.y),
                    static_cast<float>(NORMAL.x),
                    static_cast<float>(NORMAL.y),
                    static_cast<float>(NORMAL.z)};
            continue;
        }
        for (const auto source : m_fan) {
            for (const auto corner : m_fan) {
                m_weights[corner] = 0;
                m_visited[corner] = 0;
            }
            m_weights[source] = 1;
            Vec3 accumulated{};
            // Maximum-product paths around the local manifold fan. Valence is small;
            // this avoids heap queues/allocations and gives exact smooth area normals
            // at crease=0 and independent face normals at crease=1.
            for (std::size_t step = 0; step < m_fan.size(); ++step) {
                std::size_t best = SIZE_MAX;
                double weight = 0;
                for (const auto corner : m_fan)
                    if (m_visited[corner] == 0 && m_weights[corner] > weight) {
                        best = corner;
                        weight = m_weights[corner];
                    }
                if (best == SIZE_MAX) break;
                m_visited[best] = 1;
                accumulated = add(accumulated, scale(m_normals[best / 3], weight));
                for (const auto &neighbor : m_neighbors[best]) {
                    if (neighbor.corner == SIZE_MAX) continue;
                    m_weights[neighbor.corner] = std::max(m_weights[neighbor.corner],
                        weight * transmission(surface, neighbor.edge, neighbor.other_edge));
                }
            }
            const auto NORMAL = normalized(accumulated);
            const auto &POINT = surface.points[vertex];
            m_corners[source] = {static_cast<float>(POINT.position.x),
                static_cast<float>(POINT.position.y),
                static_cast<float>(POINT.position.z),
                static_cast<float>(POINT.uv.x),
                static_cast<float>(POINT.uv.y),
                static_cast<float>(NORMAL.x),
                static_cast<float>(NORMAL.y),
                static_cast<float>(NORMAL.z)};
        }
    }
    if (!surface.interaction_mesh) reconstruct(surface);
    return true;
} catch (const std::bad_alloc &) {
    m_faces.clear();
    return false;
} catch (const std::length_error &) {
    m_faces.clear();
    return false;
}

std::size_t CPaperRenderMesh::build_bounds(
    const hyprimate::PaperSurface &surface, std::size_t first, std::size_t count) {
    const auto INDEX = m_bounds.size();
    const double INF = std::numeric_limits<double>::infinity();
    Bounds bounds{{INF, INF, INF}, {-INF, -INF, -INF}, first, count, 0, 0};
    for (std::size_t item = first; item < first + count; ++item)
        for (const auto vertex : surface.faces[m_order[item]].vertex) {
            const auto POINT = surface.points[vertex].position;
            bounds.low.x = std::min(bounds.low.x, POINT.x);
            bounds.low.y = std::min(bounds.low.y, POINT.y);
            bounds.low.z = std::min(bounds.low.z, POINT.z);
            bounds.high.x = std::max(bounds.high.x, POINT.x);
            bounds.high.y = std::max(bounds.high.y, POINT.y);
            bounds.high.z = std::max(bounds.high.z, POINT.z);
        }
    m_bounds.push_back(bounds);
    if (count > 8) {
        const auto SPAN = subtract(bounds.high, bounds.low);
        const int AXIS = SPAN.x >= SPAN.y && SPAN.x >= SPAN.z ? 0 : (SPAN.y >= SPAN.z ? 1 : 2);
        const auto COORDINATE = [&](std::size_t face) {
            Vec3 centroid{};
            for (const auto vertex : surface.faces[face].vertex)
                centroid = add(centroid, surface.points[vertex].position);
            return AXIS == 0 ? centroid.x : (AXIS == 1 ? centroid.y : centroid.z);
        };
        const auto HALF = count / 2;
        std::nth_element(m_order.begin() + static_cast<std::ptrdiff_t>(first),
            m_order.begin() + static_cast<std::ptrdiff_t>(first + HALF),
            m_order.begin() + static_cast<std::ptrdiff_t>(first + count),
            [&](std::size_t lhs, std::size_t rhs) { return COORDINATE(lhs) < COORDINATE(rhs); });
        const auto LEFT = build_bounds(surface, first, HALF);
        const auto RIGHT = build_bounds(surface, first + HALF, count - HALF);
        m_bounds[INDEX].left = LEFT;
        m_bounds[INDEX].right = RIGHT;
        m_bounds[INDEX].count = 0;
    }
    return INDEX;
}

double CPaperRenderMesh::clearance(const hyprimate::PaperSurface &surface,
    Vec3 point,
    std::uint32_t endpoint_a,
    std::uint32_t endpoint_b,
    double limit,
    std::size_t branch) const {
    const auto &BOX = m_bounds[branch];
    const Vec3 NEAREST{std::clamp(point.x, BOX.low.x, BOX.high.x),
        std::clamp(point.y, BOX.low.y, BOX.high.y),
        std::clamp(point.z, BOX.low.z, BOX.high.z)};
    const auto DELTA = subtract(point, NEAREST);
    if (dot(DELTA, DELTA) >= limit) return limit;
    if (BOX.count == 0) {
        limit = clearance(surface, point, endpoint_a, endpoint_b, limit, BOX.left);
        return clearance(surface, point, endpoint_a, endpoint_b, limit, BOX.right);
    }
    for (std::size_t item = BOX.first; item < BOX.first + BOX.count; ++item) {
        const auto &FACE = surface.faces[m_order[item]];
        if (std::ranges::find(FACE.vertex, endpoint_a) != FACE.vertex.end() ||
            std::ranges::find(FACE.vertex, endpoint_b) != FACE.vertex.end())
            continue;
        limit = std::min(limit,
            triangle_distance(point,
                surface.points[FACE.vertex[0]].position,
                surface.points[FACE.vertex[1]].position,
                surface.points[FACE.vertex[2]].position));
    }
    return limit;
}

void CPaperRenderMesh::reconstruct(const hyprimate::PaperSurface &surface) {
    // Most interaction patches are nearly planar. Build the clearance hierarchy only
    // when a genuinely curved edge needs it, not for every flat frame.
    bool bounds_ready = false;
    m_midpoints.resize(surface.faces.size() * 3);
    for (std::size_t begin = 0; begin < m_edges.size();) {
        auto end = begin + 1;
        while (end < m_edges.size() && m_edges[end].a == m_edges[begin].a &&
               m_edges[end].b == m_edges[begin].b)
            ++end;
        const auto &EDGE = m_edges[begin];
        const auto FIRST = surface.points[EDGE.a].position;
        const auto SECOND = surface.points[EDGE.b].position;
        const auto VECTOR = subtract(SECOND, FIRST);
        const double LENGTH = std::sqrt(dot(VECTOR, VECTOR));
        const auto MIDDLE = scale(add(FIRST, SECOND), .5);
        bool constrained = end - begin > 2;
        double smooth = 1;
        Vec3 normal_a{}, normal_b{};
        double radius = std::min(hyprimate::PAPER_PATCH_BULGE, LENGTH * .08);
        for (std::size_t item = begin; item < end; ++item) {
            const auto &RECORD = m_edges[item];
            smooth = std::min(smooth, transmission(surface, RECORD.edge, RECORD.edge));
            normal_a = add(normal_a, normal(m_corners[RECORD.first]));
            normal_b = add(normal_b, normal(m_corners[RECORD.second]));
            const auto FACE_INDEX = RECORD.first / 3;
            const auto &FACE = surface.faces[FACE_INDEX];
            for (const auto vertex : FACE.vertex)
                constrained = constrained || surface.points[vertex].contact;
            Vec3 third = FIRST;
            for (const auto vertex : FACE.vertex)
                if (vertex != EDGE.a && vertex != EDGE.b) third = surface.points[vertex].position;
            // Sliver-safe: a curved edge cannot move far enough to invert a subtriangle.
            const auto AREA = cross(VECTOR, subtract(third, FIRST));
            if (LENGTH > 1e-15)
                radius = std::min(radius, .08 * std::sqrt(dot(AREA, AREA)) / LENGTH);
        }
        radius *= smooth;
        Vec3 reconstructed = MIDDLE;
        if (!constrained && radius > 1e-12) {
            const auto NORMAL_A = normalized(normal_a);
            const auto NORMAL_B = normalized(normal_b);
            // Cubic PN edge at t=.5. Both incident patches borrow this one position,
            // while a ridge retains independent normals on its two sides.
            auto offset = scale(subtract(scale(NORMAL_B, dot(VECTOR, NORMAL_B)),
                                    scale(NORMAL_A, dot(VECTOR, NORMAL_A))),
                .125);
            const double DISPLACEMENT = std::sqrt(dot(offset, offset));
            if (DISPLACEMENT > 1e-12) {
                if (!bounds_ready) {
                    m_order.resize(surface.faces.size());
                    std::iota(m_order.begin(), m_order.end(), std::size_t{0});
                    m_bounds.clear();
                    m_bounds.reserve(surface.faces.size() * 2);
                    (void)build_bounds(surface, 0, surface.faces.size());
                    bounds_ready = true;
                }
                radius = std::min(radius, DISPLACEMENT);
                const double GAP_SQUARED =
                    clearance(surface, MIDDLE, EDGE.a, EDGE.b, radius * radius * 25);
                radius = std::min(radius, .2 * std::sqrt(GAP_SQUARED));
                offset = scale(offset, radius / DISPLACEMENT);
                reconstructed = add(MIDDLE, offset);
            }
        }
        for (std::size_t item = begin; item < end; ++item) {
            const auto &RECORD = m_edges[item];
            const auto FACE = RECORD.first / 3;
            const auto CORNER_A = RECORD.first % 3;
            const auto CORNER_B = RECORD.second % 3;
            const auto EDGE_CORNER = (CORNER_A + 1) % 3 == CORNER_B ? CORNER_A : CORNER_B;
            m_midpoints[FACE * 3 + EDGE_CORNER] =
                midpoint(m_corners[RECORD.first], m_corners[RECORD.second], reconstructed);
        }
        begin = end;
    }
    m_vertices.resize(surface.faces.size() * 12);
    for (std::size_t face = 0; face < surface.faces.size(); ++face) {
        const auto *corners = m_corners.data() + face * 3;
        const auto *midpoints = m_midpoints.data() + face * 3;
        const std::array PATCH{corners[0],
            midpoints[0],
            midpoints[2],
            midpoints[0],
            corners[1],
            midpoints[1],
            midpoints[2],
            midpoints[1],
            corners[2],
            midpoints[0],
            midpoints[1],
            midpoints[2]};
        std::copy(PATCH.begin(),
            PATCH.end(),
            m_vertices.begin() + static_cast<std::ptrdiff_t>(face * 12));
    }
}
