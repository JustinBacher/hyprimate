#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <tuple>

#include "animations/paper/PaperMaterial.hpp"
#include "gl/Storage.hpp"

namespace {
    using hyprimate::Vec3;
    Vec3 subtract(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    Vec3 add(Vec3 lhs, Vec3 rhs) {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }
    Vec3 scale(Vec3 point, double factor) {
        return {point.x * factor, point.y * factor, point.z * factor};
    }
    double dot(Vec3 lhs, Vec3 rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }
    Vec3 normalized(Vec3 point) {
        const double LENGTH = std::sqrt(dot(point, point));
        return LENGTH > 1e-15 ? scale(point, 1 / LENGTH) : Vec3{0, 0, 1};
    }
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
    // State not covered by the compositor's RawGlBracket. Keep it local so tests and
    // production exercise exactly the same depth and scratch-attachment lifecycle.
    struct LocalState {
        GLint depth_function = 0, renderbuffer = 0;
        GLboolean depth_mask = GL_TRUE;
        GLfloat depth_clear = 1;
        std::array<GLfloat, 4> color_clear{};
        std::array<GLboolean, 4> color_mask{};
        bool blend_changed = false;
        GLint blend_equation_rgb = 0, blend_equation_alpha = 0;
        explicit LocalState(bool blend) : blend_changed(blend) {
            if (blend_changed) {
                glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
                glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
            }
            glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depth_clear);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, color_clear.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask.data());
        }
        ~LocalState() {
            if (blend_changed) {
                glBlendEquationSeparate(static_cast<GLenum>(blend_equation_rgb),
                    static_cast<GLenum>(blend_equation_alpha));
            }
            glDepthFunc(static_cast<GLenum>(depth_function));
            glDepthMask(depth_mask);
            glClearDepthf(depth_clear);
            glClearColor(color_clear[0], color_clear[1], color_clear[2], color_clear[3]);
            glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        }
        LocalState(const LocalState &) = delete;
        LocalState &operator=(const LocalState &) = delete;
    };
    bool texture_storage(GLuint texture, GLenum internal, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return hyprimate::gl::allocate_texture_2d(internal, width, height);
    }
}  // namespace

bool CPaperMaterial::initialize(GLuint program) {
    if (m_program != 0) return m_program == program;
    if (program == 0) return false;
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return false;
    m_canvas = glGetUniformLocation(program, "canvasSize");
    m_center = glGetUniformLocation(program, "center");
    m_unit = glGetUniformLocation(program, "pixelsPerUnit");
    m_uv = glGetUniformLocation(program, "uvTransform");
    m_aspect = glGetUniformLocation(program, "shapeScale");
    m_uv_bounds = glGetUniformLocation(program, "uvBounds");
    m_image = glGetUniformLocation(program, "image");
    m_scene_depth = glGetUniformLocation(program, "sceneDepth");
    m_opacity = glGetUniformLocation(program, "opacity");
    m_shading = glGetUniformLocation(program, "shading");
    m_depth_pass = glGetUniformLocation(program, "depthPass");
    m_light_basis = glGetUniformLocation(program, "lightBasis");
    m_light_bounds = glGetUniformLocation(program, "lightBounds");
    m_light_range = glGetUniformLocation(program, "lightRange");
    m_shadow_size = glGetUniformLocation(program, "shadowSize");
    m_thickness = glGetUniformLocation(program, "thickness");
    m_shadow_strength = glGetUniformLocation(program, "shadowStrength");
    glGenVertexArrays(1, &m_vao);
    if (m_vao == 0) {
        release();
        return false;
    }
    glBindVertexArray(m_vao);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    GLint vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    bool configured = static_cast<GLuint>(vao) == m_vao;
    for (GLuint attribute = 0; attribute < 3; ++attribute) {
        GLint enabled = GL_FALSE;
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        configured &= enabled == GL_TRUE;
    }
    if (!configured) {
        release();
        return false;
    }
    m_program = program;
    return true;
}

void CPaperMaterial::release() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_buffer);
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteFramebuffers(1, &m_shadow_framebuffer);
    glDeleteTextures(1, &m_color);
    glDeleteTextures(1, &m_depth);
    glDeleteRenderbuffers(1, &m_depth_buffer);
    m_program = m_vao = m_buffer = m_framebuffer = m_shadow_framebuffer = m_color = m_depth =
        m_depth_buffer = 0;
    m_width = m_height = m_shadow_width = m_shadow_height = 0;
    m_capacity = 0;
}

namespace {
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

void CPaperMaterial::topology(const hyprimate::PaperSurface &surface) {
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

bool CPaperMaterial::mesh(const hyprimate::PaperSurface &surface) try {
    if (surface.faces.empty() ||
        surface.faces.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) / 12)
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
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    const auto BYTES = m_vertices.size() * sizeof(SPaperVertex);
    if (BYTES > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) return false;
    if (BYTES > m_capacity) {
        const auto LIMIT = static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
        const auto CAPACITY = BYTES + std::min(BYTES / 2, LIMIT - BYTES);
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        if (buffer == 0) return false;
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        if (!hyprimate::gl::allocate_buffer(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(CAPACITY), nullptr, GL_STREAM_DRAW)) {
            glDeleteBuffers(1, &buffer);
            return false;
        }
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SPaperVertex), nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SPaperVertex),
            reinterpret_cast<const void *>(offsetof(SPaperVertex, u)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SPaperVertex),
            reinterpret_cast<const void *>(offsetof(SPaperVertex, nx)));
        glDeleteBuffers(1, &m_buffer);
        m_buffer = buffer;
        m_capacity = CAPACITY;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(BYTES), m_vertices.data());
    return true;
} catch (const std::bad_alloc &) {
    m_faces.clear();
    return false;
} catch (const std::length_error &) {
    m_faces.clear();
    return false;
}

std::size_t CPaperMaterial::build_bounds(
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

double CPaperMaterial::clearance(const hyprimate::PaperSurface &surface,
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

void CPaperMaterial::reconstruct(const hyprimate::PaperSurface &surface) {
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

bool CPaperMaterial::resize(GLsizei width, GLsizei height, bool shaded) {
    // Round in a wider type before narrowing or asking the driver.
    const auto rounded = [](GLsizei value) -> GLsizei {
        if (value <= 0) return 0;
        const auto result = ((static_cast<std::int64_t>(value) + 63) / 64) * 64;
        return result <= std::numeric_limits<GLsizei>::max() ? static_cast<GLsizei>(result) : 0;
    };
    width = rounded(width);
    height = rounded(height);
    if (width == 0 || height == 0) return false;
    const auto SHADOW_WIDTH = shaded ? std::min(1024, width) : 0;
    const auto SHADOW_HEIGHT = shaded ? std::min(1024, height) : 0;
    if (width <= m_width && height <= m_height && SHADOW_WIDTH <= m_shadow_width &&
        SHADOW_HEIGHT <= m_shadow_height)
        return true;
    width = std::max(m_width, width);
    height = std::max(m_height, height);
    GLint texture_limit = 0, renderbuffer_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer_limit);
    if (width > std::min(texture_limit, renderbuffer_limit) ||
        height > std::min(texture_limit, renderbuffer_limit))
        return false;
    struct Pending {
        GLuint framebuffer = 0, shadow_framebuffer = 0, color = 0, depth = 0, depth_buffer = 0;
        ~Pending() {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteFramebuffers(1, &shadow_framebuffer);
            glDeleteTextures(1, &color);
            glDeleteTextures(1, &depth);
            glDeleteRenderbuffers(1, &depth_buffer);
        }
    } pending;
    const bool SHADOW = SHADOW_WIDTH > m_shadow_width || SHADOW_HEIGHT > m_shadow_height;
    const bool COLOR = width > m_width || height > m_height;
    const auto BACKING_WIDTH = std::max(m_shadow_width, SHADOW_WIDTH);
    const auto BACKING_HEIGHT = std::max(m_shadow_height, SHADOW_HEIGHT);
    glActiveTexture(GL_TEXTURE1);
    if (SHADOW) {
        glGenTextures(1, &pending.depth);
        glGenFramebuffers(1, &pending.shadow_framebuffer);
        if (pending.depth == 0 || pending.shadow_framebuffer == 0 ||
            !texture_storage(pending.depth, GL_DEPTH_COMPONENT24, BACKING_WIDTH, BACKING_HEIGHT))
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, pending.shadow_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pending.depth, 0);
        constexpr GLenum NONE = GL_NONE;
        glDrawBuffers(1, &NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    }
    if (COLOR) {
        glGenTextures(1, &pending.color);
        glGenRenderbuffers(1, &pending.depth_buffer);
        glGenFramebuffers(1, &pending.framebuffer);
        if (pending.color == 0 || pending.depth_buffer == 0 || pending.framebuffer == 0 ||
            !texture_storage(pending.color, GL_RGBA8, width, height))
            return false;
        glBindRenderbuffer(GL_RENDERBUFFER, pending.depth_buffer);
        if (!hyprimate::gl::allocate_renderbuffer(GL_DEPTH_COMPONENT24, width, height)) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, pending.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pending.color, 0);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pending.depth_buffer);
        if (!hyprimate::gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT0, pending.color) ||
            !hyprimate::gl::framebuffer_attachment_is(GL_DEPTH_ATTACHMENT, pending.depth_buffer) ||
            glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
    }
    // Publish both targets together; pending then destroys only the replaced objects.
    if (SHADOW) {
        std::swap(m_depth, pending.depth);
        std::swap(m_shadow_framebuffer, pending.shadow_framebuffer);
        m_shadow_width = BACKING_WIDTH;
        m_shadow_height = BACKING_HEIGHT;
    }
    if (COLOR) {
        std::swap(m_color, pending.color);
        std::swap(m_depth_buffer, pending.depth_buffer);
        std::swap(m_framebuffer, pending.framebuffer);
        m_width = width;
        m_height = height;
    }
    return true;
}

bool CPaperMaterial::draw(const hyprimate::PaperSurface &surface,
    const SPaperTarget &target,
    GLuint window,
    GLuint output_framebuffer) {
    if (m_program == 0 || window == 0 || window == m_color || window == m_depth ||
        !hyprimate::is_usable(target.source) || !finite(target.canvas) || !finite(target.origin) ||
        !finite(target.texture_origin) || !finite(target.texture_size) || target.canvas.x < 1 ||
        target.canvas.y < 1 || target.canvas.x > 32768 || target.canvas.y > 32768 ||
        target.texture_size.x < 1 || target.texture_size.y < 1 || !finite(surface.size) ||
        surface.size.x <= 0 || surface.size.y <= 0 || !std::isfinite(surface.opacity) ||
        !std::isfinite(surface.shading) || !std::isfinite(surface.thickness) ||
        !std::isfinite(surface.shadow_strength) || !std::isfinite(target.alpha))
        return false;
    const bool CASTS_SHADOW =
        surface.shadow_strength > 0 && std::any_of(surface.points.begin(),
                                           surface.points.end(),
                                           [](const auto &point) { return point.position.z > 0; });
    const LocalState STATE(CASTS_SHADOW);
    const auto WIDTH = static_cast<GLsizei>(std::ceil(target.canvas.x));
    const auto HEIGHT = static_cast<GLsizei>(std::ceil(target.canvas.y));
    if (!resize(WIDTH, HEIGHT, surface.shading > 0) || !mesh(surface)) return false;
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepthf(1);
    glUseProgram(m_program);
    glUniform2f(m_canvas, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
    const double SHAPE =
        std::sqrt(target.source.w / target.source.h) / std::sqrt(surface.size.x / surface.size.y);
    glUniform2f(m_aspect, static_cast<float>(SHAPE), static_cast<float>(1 / SHAPE));
    const bool SHADED = surface.shading > 0;
    const auto SHADOW_WIDTH = std::min(WIDTH, 1024), SHADOW_HEIGHT = std::min(HEIGHT, 1024);
    if (SHADED) {
        const auto LIGHT = normalized(Vec3{-.32, -.42, 1});
        const auto RIGHT = normalized(Vec3{LIGHT.z, 0, -LIGHT.x});
        const auto UP = cross(LIGHT, RIGHT);
        const std::array<GLfloat, 9> BASIS{static_cast<float>(RIGHT.x),
            static_cast<float>(RIGHT.y),
            static_cast<float>(RIGHT.z),
            static_cast<float>(UP.x),
            static_cast<float>(UP.y),
            static_cast<float>(UP.z),
            static_cast<float>(LIGHT.x),
            static_cast<float>(LIGHT.y),
            static_cast<float>(LIGHT.z)};
        const double INF = std::numeric_limits<double>::infinity();
        Vec3 low{INF, INF, INF}, high{-INF, -INF, -INF};
        for (const auto &vertex : m_vertices) {
            const Vec3 POINT{vertex.x * SHAPE, vertex.y / SHAPE, vertex.z};
            const Vec3 PROJECTED{dot(RIGHT, POINT), dot(UP, POINT), dot(LIGHT, POINT)};
            low.x = std::min(low.x, PROJECTED.x);
            low.y = std::min(low.y, PROJECTED.y);
            low.z = std::min(low.z, PROJECTED.z);
            high.x = std::max(high.x, PROJECTED.x);
            high.y = std::max(high.y, PROJECTED.y);
            high.z = std::max(high.z, PROJECTED.z);
        }
        // Fit every physical patch, not the camera crop: hidden/offscreen folds cast.
        const double PAD = std::max(.01, static_cast<double>(surface.thickness) * 4);
        glUniformMatrix3fv(m_light_basis, 1, GL_FALSE, BASIS.data());
        glUniform4f(m_light_bounds,
            static_cast<float>((low.x + high.x) * .5),
            static_cast<float>((low.y + high.y) * .5),
            static_cast<float>((high.x - low.x) * .5 + PAD),
            static_cast<float>((high.y - low.y) * .5 + PAD));
        glUniform2f(
            m_light_range, static_cast<float>(low.z - PAD), static_cast<float>(high.z + PAD));
        glUniform2f(
            m_shadow_size, static_cast<float>(SHADOW_WIDTH), static_cast<float>(SHADOW_HEIGHT));
        glUniform1f(m_thickness, std::max(.0001F, surface.thickness));
    }
    glUniform2f(m_center,
        static_cast<float>(target.source.x + target.source.w * .5 - target.origin.x),
        static_cast<float>(target.source.y + target.source.h * .5 - target.origin.y));
    glUniform1f(
        m_unit, static_cast<float>(std::sqrt(target.source.w) * std::sqrt(target.source.h)));
    const auto &S = target.source;
    const auto &O = target.texture_origin;
    const auto &T = target.texture_size;
    glUniform4f(m_uv,
        static_cast<float>((S.x - O.x) / T.x),
        static_cast<float>((S.y - O.y) / T.y),
        static_cast<float>(S.w / T.x),
        static_cast<float>(S.h / T.y));
    glUniform4f(m_uv_bounds,
        static_cast<float>(std::max(.5, (S.x - O.x + .5)) / T.x),
        static_cast<float>(std::max(.5, (S.y - O.y + .5)) / T.y),
        static_cast<float>(std::min(T.x - .5, S.x + S.w - O.x - .5) / T.x),
        static_cast<float>(std::min(T.y - .5, S.y + S.h - O.y - .5) / T.y));
    glUniform1f(m_opacity, std::clamp(surface.opacity * target.alpha, 0.F, 1.F));
    glUniform1f(m_shading, std::clamp(surface.shading, 0.F, 1.F));
    glUniform1i(m_image, 0);
    glUniform1i(m_scene_depth, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window);
    glBindSampler(0, 0);
    glActiveTexture(GL_TEXTURE1);
    // Never sample an attached depth texture, even in the depth-only branch: GLES
    // considers that a framebuffer feedback loop. Bind it only for the colour pass.
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(1, 0);
    glEnable(GL_SCISSOR_TEST);
    if (SHADED) {
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glScissor(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_framebuffer);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniform1i(m_depth_pass, 1);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    }
    glViewport(0, 0, WIDTH, HEIGHT);
    glScissor(0, 0, WIDTH, HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (CASTS_SHADOW) {
        glEnable(GL_BLEND);
        // Max coverage is order-independent across overlapping folds and shared patches.
        glBlendEquation(GL_MAX);
        glDisable(GL_DEPTH_TEST);
        glUniform1i(m_depth_pass, 2);
        glUniform1f(m_shadow_strength,
            static_cast<float>(std::clamp(surface.shadow_strength, 0.0, 1.0)) * .18F);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
        // Only the nearest sheet fragment owns a texel. Blending translucent
        // folds here would bring back the hidden copy when draw order changes.
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
    glUniform1i(m_depth_pass, 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output_framebuffer);
    glBlitFramebuffer(0,
        0,
        WIDTH,
        HEIGHT,
        0,
        target.output_top_first ? HEIGHT : 0,
        WIDTH,
        target.output_top_first ? 0 : HEIGHT,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
    return true;
}
