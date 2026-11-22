#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "PaperTopology.hpp"

namespace hyprimate::paper {
    namespace {
        Vec3 sub(Vec3 a, Vec3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        double dot(Vec3 a, Vec3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        double norm(Vec3 a) {
            return std::sqrt(dot(a, a));
        }
        Vec3 midpoint(Vec3 a, Vec3 b) {
            return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
        }
        using Key = std::pair<std::uint64_t, std::uint64_t>;
        Key key(std::uint64_t a, std::uint64_t b) {
            return std::minmax(a, b);
        }
        double mass(const std::vector<Node> &nodes) {
            double result = 0;
            for (const auto &n : nodes)
                if (n.inv_mass > 0) result += 1 / n.inv_mass;
            return result;
        }
        Vec3 angular_momentum(
            const std::vector<Node> &nodes, const std::vector<std::uint32_t> &patch) {
            Vec3 result{};
            for (const auto index : patch) {
                const auto &node = nodes[index];
                const auto spin = cross(node.x, node.velocity);
                result.x += spin.x / node.inv_mass;
                result.y += spin.y / node.inv_mass;
                result.z += spin.z / node.inv_mass;
            }
            return result;
        }
        Vec3 patch_momentum(
            const std::vector<Node> &nodes, const std::vector<std::uint32_t> &patch) {
            Vec3 result{};
            for (const auto index : patch) {
                const auto &node = nodes[index];
                result.x += node.velocity.x / node.inv_mass;
                result.y += node.velocity.y / node.inv_mass;
                result.z += node.velocity.z / node.inv_mass;
            }
            return result;
        }
        void conserve_momentum(std::vector<Node> &nodes,
            const std::vector<std::uint32_t> &patch,
            Vec3 linear,
            Vec3 angular) {
            double total = 0;
            Vec3 centre{};
            for (const auto index : patch) {
                const auto &node = nodes[index];
                const double weight = 1 / node.inv_mass;
                total += weight;
                centre.x += node.x.x * weight;
                centre.y += node.x.y * weight;
                centre.z += node.x.z * weight;
            }
            centre.x /= total;
            centre.y /= total;
            centre.z /= total;
            const Vec3 impulse = sub(linear, patch_momentum(nodes, patch));
            const Vec3 torque =
                sub(sub(angular, angular_momentum(nodes, patch)), cross(centre, impulse));
            double matrix[3][4]{};
            for (const auto index : patch) {
                const auto &node = nodes[index];
                const auto r = sub(node.x, centre);
                const double coordinate[3]{r.x, r.y, r.z};
                const double rr = dot(r, r), weight = 1 / node.inv_mass;
                for (unsigned row = 0; row < 3; ++row)
                    for (unsigned col = 0; col < 3; ++col)
                        matrix[row][col] +=
                            weight * ((row == col ? rr : 0) - coordinate[row] * coordinate[col]);
            }
            matrix[0][3] = torque.x;
            matrix[1][3] = torque.y;
            matrix[2][3] = torque.z;
            // Pivoted local inertia solve. Null rotational axes of a collapsed line cannot
            // carry angular momentum; leave only those unobservable components at zero.
            double largest = 0;
            for (unsigned row = 0; row < 3; ++row)
                largest = std::max(largest, std::abs(matrix[row][row]));
            for (unsigned col = 0; col < 3; ++col) {
                unsigned pivot = col;
                for (unsigned row = col + 1; row < 3; ++row)
                    if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) pivot = row;
                if (std::abs(matrix[pivot][col]) <= largest * 1e-12) continue;
                for (unsigned j = 0; j < 4; ++j) std::swap(matrix[col][j], matrix[pivot][j]);
                const double divisor = matrix[col][col];
                for (unsigned j = col; j < 4; ++j) matrix[col][j] /= divisor;
                for (unsigned row = 0; row < 3; ++row)
                    if (row != col) {
                        const double factor = matrix[row][col];
                        for (unsigned j = col; j < 4; ++j)
                            matrix[row][j] -= factor * matrix[col][j];
                    }
            }
            const Vec3 omega{std::abs(matrix[0][0]) > 1e-12 ? matrix[0][3] : 0,
                std::abs(matrix[1][1]) > 1e-12 ? matrix[1][3] : 0,
                std::abs(matrix[2][2]) > 1e-12 ? matrix[2][3] : 0};
            for (const auto index : patch) {
                auto &node = nodes[index];
                const auto rotation = cross(omega, sub(node.x, centre));
                node.velocity.x += impulse.x / total + rotation.x;
                node.velocity.y += impulse.y / total + rotation.y;
                node.velocity.z += impulse.z / total + rotation.z;
            }
        }
        double triangle_quality(Vec2 a, Vec2 b, Vec2 c) {
            const double ab = std::hypot(a.x - b.x, a.y - b.y),
                         ac = std::hypot(a.x - c.x, a.y - c.y),
                         bc = std::hypot(b.x - c.x, b.y - c.y);
            return std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) /
                   std::max({ab * ac, ab * bc, ac * bc, 1e-20});
        }
        std::size_t capacity(PaperQuality quality) {
            switch (quality) {
                case PaperQuality::Low:
                    return 180;
                case PaperQuality::High:
                    return 520;
                default:
                    return 320;
            }
        }
        double angle(const Hinge &h, const std::vector<Node> &nodes) {
            if (h.d == UINT32_MAX) return 0;
            const auto e = sub(nodes[h.b].x, nodes[h.a].x);
            const auto n1 = cross(e, sub(nodes[h.c].x, nodes[h.a].x));
            const auto n2 = cross(sub(nodes[h.a].x, nodes[h.b].x), sub(nodes[h.d].x, nodes[h.b].x));
            const double length = norm(e);
            if (length < 1e-12 || norm(n1) * norm(n2) < 1e-18) return 0;
            return std::atan2(dot(e, cross(n1, n2)) / length, dot(n1, n2));
        }
        bool contains(const Face &f, std::uint32_t n) {
            return f.v[0] == n || f.v[1] == n || f.v[2] == n;
        }
        double compression(const Face &f, const std::vector<Node> &nodes, Vec2 size) {
            const auto &a = nodes[f.v[0]], &b = nodes[f.v[1]], &c = nodes[f.v[2]];
            const double ux = (b.uv.x - a.uv.x) * size.x, uy = (b.uv.y - a.uv.y) * size.y;
            const double vx = (c.uv.x - a.uv.x) * size.x, vy = (c.uv.y - a.uv.y) * size.y;
            const double determinant = ux * vy - uy * vx;
            if (std::abs(determinant) < 1e-18) return 1;
            const auto e = sub(b.x, a.x), g = sub(c.x, a.x);
            const Vec3 dx{(e.x * vy - g.x * uy) / determinant,
                (e.y * vy - g.y * uy) / determinant,
                (e.z * vy - g.z * uy) / determinant};
            const Vec3 dy{(g.x * ux - e.x * vx) / determinant,
                (g.y * ux - e.y * vx) / determinant,
                (g.z * ux - e.z * vx) / determinant};
            const double xx = dot(dx, dx), yy = dot(dy, dy), xy = dot(dx, dy);
            const double eigenvalue = 0.5 * (xx + yy - std::hypot(xx - yy, 2 * xy));
            return std::max(0.0, 1 - std::sqrt(std::max(0.0, eigenvalue)));
        }
    }  // namespace

    double Topology::rest_length(std::uint32_t a, std::uint32_t b) const {
        const auto &u = nodes[a].uv;
        const auto &v = nodes[b].uv;
        return std::hypot((u.x - v.x) * material_size.x, (u.y - v.y) * material_size.y);
    }

    void Topology::initialize(Vec2 size, std::uint64_t seed, const PaperParams &params) {
        (void)seed;  // Event entropy belongs in forcing, never in material tessellation.
        nodes.clear();
        faces.clear();
        hinges.clear();
        splits_.clear();
        next_id_ = 1;
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0)
            size = {1, 1};
        const double aspect = std::clamp(size.x / size.y, 1.0 / 16384, 16384.0);
        material_size = {std::sqrt(aspect), 1 / std::sqrt(aspect)};
        const unsigned cells = params.simulation_quality == PaperQuality::Low    ? 32
                               : params.simulation_quality == PaperQuality::High ? 128
                                                                                 : 72;
        const unsigned nx =
            std::clamp(static_cast<unsigned>(std::lround(std::sqrt(cells * aspect))), 1U, 128U);
        const unsigned ny =
            std::clamp(static_cast<unsigned>(std::lround(std::sqrt(cells / aspect))), 1U, 128U);
        base_length_ = std::max(material_size.x / nx, material_size.y / ny);
        nodes.reserve(static_cast<std::size_t>(nx + 1) * (ny + 1));
        faces.reserve(static_cast<std::size_t>(2) * nx * ny);
        for (unsigned y = 0; y <= ny; ++y)
            for (unsigned x = 0; x <= nx; ++x) {
                Node n;
                n.uv = {static_cast<double>(x) / nx, static_cast<double>(y) / ny};
                n.x = {(n.uv.x - 0.5) * material_size.x, (n.uv.y - 0.5) * material_size.y, 0};
                n.prev = n.x;
                n.id = next_id_++;
                nodes.push_back(n);
            }
        for (unsigned y = 0; y < ny; ++y)
            for (unsigned x = 0; x < nx; ++x) {
                const auto a = y * (nx + 1) + x, b = a + 1, c = a + nx + 1, d = c + 1;
                if ((x + y) % 2 == 0) {
                    faces.push_back({{a, b, d}});
                    faces.push_back({{a, d, c}});
                } else {
                    faces.push_back({{a, b, c}});
                    faces.push_back({{b, d, c}});
                }
            }
        rebuild({}, 1);
    }

    std::vector<Topology::EdgeState> Topology::edge_states() const {
        std::vector<EdgeState> result;
        result.reserve(hinges.size());
        for (const auto &h : hinges) {
            const auto [a, b] = key(nodes[h.a].id, nodes[h.b].id);
            result.push_back({a, b, h.rest_angle, h.memory});
        }
        return result;
    }

    std::uint32_t Topology::node_index(std::uint64_t id) const {
        const auto found = std::lower_bound(
            ids_.begin(), ids_.end(), id, [](const auto &entry, std::uint64_t value) {
                return entry.first < value;
            });
        return found != ids_.end() && found->first == id ? found->second : UINT32_MAX;
    }

    void Topology::rebuild() {
        rebuild({}, mass(nodes));
        splits_.clear();
        // UINT64_MAX marks exhausted generated identities; do not wrap to zero.
        const auto maximum_id = ids_.empty() ? 0 : ids_.back().first;
        next_id_ = maximum_id == UINT64_MAX ? UINT64_MAX : maximum_id + 1;
    }

    void Topology::rebuild(const std::vector<EdgeState> &states, double total_mass) {
        adjacency_valid_ = false;
        // Check the destinations before multiplying incidence counts or narrowing
        // a node index. UINT32_MAX is reserved for an absent hinge opposite.
        if (nodes.size() > UINT32_MAX || nodes.size() >= face_offsets_.max_size() ||
            nodes.size() >= hinge_offsets_.max_size() || nodes.size() > ids_.max_size() ||
            nodes.size() > area_scratch_.max_size() ||
            faces.size() > edge_scratch_.max_size() / 3 ||
            faces.size() > vertex_faces_.max_size() / 3 || faces.size() > face_edges_.max_size())
            throw std::length_error("Paper topology incidence count exceeds storage");
        for (const auto &face : faces) {
            const auto [a, b, c] = face.v;
            if (a >= nodes.size() || b >= nodes.size() || c >= nodes.size() || a == b || b == c ||
                c == a)
                throw std::invalid_argument("Paper topology has an invalid face");
        }
        // Flat retained storage replaces one tree node per material/mesh edge.
        // The order field preserves the old map's last-write-wins override rule.
        state_scratch_.clear();
        if (state_scratch_.capacity() < states.size())
            state_scratch_.reserve(std::max(states.size(), state_scratch_.capacity() * 2));
        for (std::size_t i = 0; i < states.size(); ++i) {
            auto state = states[i];
            const auto endpoints = key(state.a, state.b);
            state.a = endpoints.first;
            state.b = endpoints.second;
            state_scratch_.push_back({state, i});
        }
        std::sort(state_scratch_.begin(),
            state_scratch_.end(),
            [](const StoredState &left, const StoredState &right) {
                const auto lhs = key(left.state.a, left.state.b),
                           rhs = key(right.state.a, right.state.b);
                return lhs != rhs ? lhs < rhs : left.order > right.order;
            });
        auto &areas = area_scratch_;
        if (areas.capacity() < nodes.size())
            areas.reserve(std::max(nodes.size(), areas.capacity() * 2));
        areas.assign(nodes.size(), 0);
        ids_.clear();
        if (ids_.capacity() < nodes.size())
            ids_.reserve(std::max(nodes.size(), ids_.capacity() * 2));
        for (std::uint32_t i = 0; i < nodes.size(); ++i) ids_.emplace_back(nodes[i].id, i);
        std::sort(ids_.begin(), ids_.end());
        for (std::size_t i = 1; i < ids_.size(); ++i)
            if (ids_[i - 1].first == ids_[i].first)
                throw std::invalid_argument("Paper topology has duplicate node identities");

        face_offsets_.assign(nodes.size() + 1, 0);
        edge_scratch_.clear();
        const auto edge_count = faces.size() * 3;
        if (edge_scratch_.capacity() < edge_count)
            edge_scratch_.reserve(std::max(edge_count, edge_scratch_.capacity() * 2));
        for (std::size_t i = 0; i < faces.size(); ++i) {
            const auto &f = faces[i];
            const auto &a = nodes[f.v[0]].uv, &b = nodes[f.v[1]].uv, &c = nodes[f.v[2]].uv;
            const double area = std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 6;
            for (const auto vertex : f.v) {
                areas[vertex] += area;
                ++face_offsets_[vertex + 1];
            }
            for (unsigned side = 0; side < 3; ++side) {
                const auto aidx = f.v[side], bidx = f.v[(side + 1) % 3];
                const auto endpoints = std::minmax(aidx, bidx);
                edge_scratch_.push_back(
                    {endpoints.first, endpoints.second, f.v[(side + 2) % 3], i, side, aidx < bidx});
            }
        }
        std::sort(edge_scratch_.begin(),
            edge_scratch_.end(),
            [](const EdgeRecord &left, const EdgeRecord &right) {
                if (left.a != right.a) return left.a < right.a;
                if (left.b != right.b) return left.b < right.b;
                return left.face < right.face;
            });

        // Check all groups before publishing any adjacency, not just the group
        // currently being filled: a later invalid edge must not leave a prefix.
        std::size_t hinge_count = 0, hinge_incidence = 0;
        for (std::size_t first = 0; first < edge_scratch_.size();) {
            const auto &edge = edge_scratch_[first];
            std::size_t last = first + 1;
            while (last < edge_scratch_.size() && edge_scratch_[last].a == edge.a &&
                   edge_scratch_[last].b == edge.b)
                ++last;
            if (last - first > 2 ||
                (last - first == 2 && (edge.forward == edge_scratch_[first + 1].forward ||
                                          edge.opposite == edge_scratch_[first + 1].opposite)))
                throw std::invalid_argument("Paper topology has a non-manifold edge");
            const std::size_t incidence = last - first == 2 ? 4 : 3;
            if (hinge_count == hinges.max_size() || hinge_count == edge_faces_.max_size() ||
                incidence > vertex_hinges_.max_size() - hinge_incidence)
                throw std::length_error("Paper topology hinge incidence exceeds storage");
            ++hinge_count;
            hinge_incidence += incidence;
            first = last;
        }
        std::partial_sum(face_offsets_.begin(), face_offsets_.end(), face_offsets_.begin());
        vertex_faces_.resize(edge_count);
        incidence_cursor_.assign(face_offsets_.begin(), face_offsets_.end() - 1);
        for (std::size_t i = 0; i < faces.size(); ++i)
            for (const auto vertex : faces[i].v) vertex_faces_[incidence_cursor_[vertex]++] = i;
        hinges.clear();
        edge_faces_.clear();
        face_edges_.assign(faces.size(), {SIZE_MAX, SIZE_MAX, SIZE_MAX});
        for (std::size_t first = 0; first < edge_scratch_.size();) {
            const auto &edge = edge_scratch_[first];
            Hinge hinge{edge.a, edge.b, UINT32_MAX, UINT32_MAX};
            std::array<std::size_t, 2> adjacent{SIZE_MAX, SIZE_MAX};
            unsigned side = 0;
            std::size_t last = first;
            for (; last < edge_scratch_.size() && edge_scratch_[last].a == edge.a &&
                   edge_scratch_[last].b == edge.b;
                ++last) {
                const auto &record = edge_scratch_[last];
                if (record.forward)
                    hinge.c = record.opposite;
                else
                    hinge.d = record.opposite;
                adjacent[side++] = record.face;
                face_edges_[record.face][record.side] = hinges.size();
            }
            // Boundaries expose their sole opposite vertex through c.
            if (hinge.c == UINT32_MAX) {
                std::swap(hinge.a, hinge.b);
                std::swap(hinge.c, hinge.d);
            }
            hinge.length = rest_length(hinge.a, hinge.b);
            const auto endpoints = key(nodes[hinge.a].id, nodes[hinge.b].id);
            const auto found = std::lower_bound(state_scratch_.begin(),
                state_scratch_.end(),
                endpoints,
                [](const StoredState &entry, const Key &value) {
                    return key(entry.state.a, entry.state.b) < value;
                });
            if (found != state_scratch_.end() && key(found->state.a, found->state.b) == endpoints) {
                hinge.rest_angle = found->state.rest;
                hinge.memory = found->state.memory;
            }
            hinges.push_back(hinge);
            edge_faces_.push_back(adjacent);
            first = last;
        }
        hinge_offsets_.assign(nodes.size() + 1, 0);
        for (const auto &hinge : hinges)
            for (const auto vertex : {hinge.a, hinge.b, hinge.c, hinge.d})
                if (vertex != UINT32_MAX) ++hinge_offsets_[vertex + 1];
        std::partial_sum(hinge_offsets_.begin(), hinge_offsets_.end(), hinge_offsets_.begin());
        vertex_hinges_.resize(hinge_offsets_.back());
        incidence_cursor_.assign(hinge_offsets_.begin(), hinge_offsets_.end() - 1);
        for (std::size_t i = 0; i < hinges.size(); ++i) {
            const auto &hinge = hinges[i];
            for (const auto vertex : {hinge.a, hinge.b, hinge.c, hinge.d})
                if (vertex != UINT32_MAX) vertex_hinges_[incidence_cursor_[vertex]++] = i;
        }
        double area = 0;
        for (std::size_t i = 0; i < areas.size(); ++i)
            if (nodes[i].inv_mass > 0) area += areas[i];
        const double density = total_mass / std::max(area, 1e-18);
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].inv_mass > 0) nodes[i].inv_mass = 1 / std::max(areas[i] * density, 1e-18);
        adjacency_nodes_ = nodes.size();
        adjacency_valid_ = true;
    }

    std::size_t Topology::refinement_edge(std::size_t index, unsigned depth) const {
        if (depth > 8) return SIZE_MAX;
        const auto &hinge = hinges[index];
        for (const auto face : edge_faces_[index]) {
            if (face == SIZE_MAX) continue;
            for (const auto vertex : faces[face].v)
                if (nodes[vertex].contact_age || nodes[vertex].inv_mass == 0) return SIZE_MAX;
            const auto opposite = *std::find_if(faces[face].v.begin(),
                faces[face].v.end(),
                [&](auto vertex) { return vertex != hinge.a && vertex != hinge.b; });
            const auto material = [&](std::uint32_t vertex) {
                return Vec2{
                    nodes[vertex].uv.x * material_size.x, nodes[vertex].uv.y * material_size.y};
            };
            const auto a = material(hinge.a), b = material(hinge.b), c = material(opposite);
            const Vec2 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
            if (std::min(triangle_quality(a, mid, c), triangle_quality(mid, b, c)) >= 0.22)
                continue;
            const auto longest = *std::max_element(face_edges_[face].begin(),
                face_edges_[face].end(),
                [&](auto left, auto right) { return hinges[left].length < hinges[right].length; });
            if (longest == index) return SIZE_MAX;
            return refinement_edge(longest, depth + 1);
        }
        return index;
    }

    bool Topology::split_edge(std::size_t index, std::uint64_t tick) {
        if (next_id_ == UINT64_MAX) return false;
        const Hinge edge = hinges[index];
        std::vector<std::size_t> incident;
        for (const auto i : edge_faces_[index]) {
            if (i == SIZE_MAX) continue;
            for (const auto vertex : faces[i].v)
                if (nodes[vertex].contact_age || nodes[vertex].inv_mass == 0) return false;
            incident.push_back(i);
        }
        if (incident.empty()) return false;
        if (nodes.size() >= UINT32_MAX || nodes.size() == nodes.max_size() ||
            incident.size() > faces.max_size() - faces.size() ||
            faces.size() > edge_scratch_.max_size() / 3 - incident.size() ||
            faces.size() > vertex_faces_.max_size() / 3 - incident.size() ||
            incident.size() > face_edges_.max_size() - faces.size())
            return false;
        auto states = edge_states();
        const double total_mass = mass(nodes);
        std::vector<std::uint32_t> patch;
        for (const auto face : incident)
            for (const auto vertex : faces[face].v) patch.push_back(vertex);
        std::sort(patch.begin(), patch.end());
        patch.erase(std::unique(patch.begin(), patch.end()), patch.end());
        const Vec3 old_linear = patch_momentum(nodes, patch),
                   old_angular = angular_momentum(nodes, patch);
        const auto &a = nodes[edge.a], &b = nodes[edge.b];
        Node n;
        n.uv = {(a.uv.x + b.uv.x) * 0.5, (a.uv.y + b.uv.y) * 0.5};
        n.x = midpoint(a.x, b.x);
        n.prev = midpoint(a.prev, b.prev);
        n.velocity = midpoint(a.velocity, b.velocity);
        n.released = midpoint(a.released, b.released);
        n.id = next_id_++;
        const auto mid = static_cast<std::uint32_t>(nodes.size());
        Split record;
        record.a = a.id;
        record.b = b.id;
        record.midpoint = n.id;
        record.tick = tick;
        states.push_back({a.id, n.id, edge.rest_angle, edge.memory});
        states.push_back({b.id, n.id, edge.rest_angle, edge.memory});
        nodes.push_back(n);
        for (const auto i : incident) {
            const Face original = faces[i];
            record.parents.push_back(
                {nodes[original.v[0]].id, nodes[original.v[1]].id, nodes[original.v[2]].id});
            for (unsigned j = 0; j < 3; ++j) {
                const auto u = original.v[j], v = original.v[(j + 1) % 3],
                           w = original.v[(j + 2) % 3];
                if (key(u, v) != key(edge.a, edge.b)) continue;
                const Face first{{u, mid, w}}, second{{mid, v, w}};
                faces[i] = first;
                faces.push_back(second);
                for (const auto &f : {first, second})
                    record.children.push_back(
                        {nodes[f.v[0]].id, nodes[f.v[1]].id, nodes[f.v[2]].id});
                break;
            }
        }
        splits_.push_back(std::move(record));
        rebuild(states, total_mass);
        patch.push_back(mid);
        conserve_momentum(nodes, patch, old_linear, old_angular);
        return true;
    }

    bool Topology::collapse_split(std::size_t index) {
        const auto &record = splits_[index];
        const auto a = node_index(record.a), b = node_index(record.b),
                   m = node_index(record.midpoint);
        if (a == UINT32_MAX || b == UINT32_MAX || m == UINT32_MAX) return false;
        for (const auto &parent : record.parents)
            for (const auto id : parent)
                if (node_index(id) == UINT32_MAX) return false;
        const double scale = rest_length(a, b);
        const auto &mid = nodes[m];
        // Bound removed velocity detail to 0.05 edge lengths per simulation time
        // unit (<0.0003 edge lengths per 1/180 step), allowing spin transfer.
        if (norm(sub(mid.x, midpoint(nodes[a].x, nodes[b].x))) > scale * 1e-6 ||
            norm(sub(mid.prev, midpoint(nodes[a].prev, nodes[b].prev))) > scale * 1e-6 ||
            norm(sub(mid.velocity, midpoint(nodes[a].velocity, nodes[b].velocity))) > scale * 0.05)
            return false;
        std::vector<std::size_t> children;
        for (const auto i : incident_faces(m)) {
            const auto &f = faces[i];
            const std::array<std::uint64_t, 3> stable{
                nodes[f.v[0]].id, nodes[f.v[1]].id, nodes[f.v[2]].id};
            if (std::find(record.children.begin(), record.children.end(), stable) ==
                record.children.end())
                return false;
            for (const auto v : f.v)
                if (nodes[v].contact_age || nodes[v].inv_mass == 0) return false;
            if (compression(f, nodes, material_size) > 0.03) return false;
            children.push_back(i);
        }
        if (children.size() != record.children.size()) return false;
        // Protect high-memory creases and reject bending/compression, with lower thresholds
        // than refinement. Even a removable midpoint cannot erase an active physical fold.
        double rest = 0, memory = 0, weight = 0;
        double first_rest = 0, first_memory = 0;
        for (const auto hinge_index : incident_hinges(m)) {
            const auto &h = hinges[hinge_index];
            if (std::abs(h.memory) > 0.04 || std::abs(angle(h, nodes) - h.rest_angle) > 0.06 ||
                norm(sub(nodes[h.a].x, nodes[h.b].x)) < h.length * 0.97)
                return false;
            if ((h.a == m && (h.b == a || h.b == b)) || (h.b == m && (h.a == a || h.a == b))) {
                if (weight > 0 && (std::abs(h.rest_angle - first_rest) > 1e-8 ||
                                      std::abs(h.memory - first_memory) > 1e-8))
                    return false;
                first_rest = h.rest_angle;
                first_memory = h.memory;
                rest += h.rest_angle * h.length;
                memory += h.memory * h.length;
                weight += h.length;
            } else if ((h.a == m || h.b == m) &&
                       (std::abs(h.memory) > 1e-8 || std::abs(h.rest_angle) > 1e-8)) {
                // A removed spoke has no parent hinge to receive its plastic state.
                return false;
            }
        }
        auto states = edge_states();
        states.push_back(
            {record.a, record.b, weight > 0 ? rest / weight : 0, weight > 0 ? memory / weight : 0});
        const double total_mass = mass(nodes);
        std::vector<std::uint32_t> patch;
        for (const auto face : children)
            for (const auto vertex : faces[face].v) patch.push_back(vertex);
        std::sort(patch.begin(), patch.end());
        patch.erase(std::unique(patch.begin(), patch.end()), patch.end());
        const Vec3 old_linear = patch_momentum(nodes, patch),
                   old_angular = angular_momentum(nodes, patch);
        std::vector<Face> retained;
        retained.reserve(faces.size() - children.size() + record.parents.size());
        for (const auto &f : faces)
            if (!contains(f, m)) retained.push_back(f);
        for (const auto &f : record.parents)
            retained.push_back({{node_index(f[0]), node_index(f[1]), node_index(f[2])}});
        faces = std::move(retained);
        nodes.erase(nodes.begin() + m);
        for (auto &f : faces)
            for (auto &v : f.v)
                if (v > m) --v;
        splits_.erase(splits_.begin() + static_cast<std::ptrdiff_t>(index));
        rebuild(states, total_mass);
        patch.erase(std::remove(patch.begin(), patch.end(), m), patch.end());
        for (auto &vertex : patch)
            if (vertex > m) --vertex;
        conserve_momentum(nodes, patch, old_linear, old_angular);
        return true;
    }

    void Topology::adapt(const PaperParams &params, std::uint64_t tick) {
        if (!adjacency_valid_ || adjacency_nodes_ != nodes.size() ||
            face_edges_.size() != faces.size() || edge_faces_.size() != hinges.size())
            throw std::invalid_argument("Paper topology requires rebuild before adaptation");
        if (nodes.empty() || tick % 4 != 0) return;
        // Recently split patches must remain quiet for 32 ticks before inverse coarsening.
        unsigned collapsed = 0;
        for (std::size_t i = splits_.size(); i > 0 && collapsed < 2; --i)
            if (tick >= splits_[i - 1].tick && tick - splits_[i - 1].tick >= 32 &&
                collapse_split(i - 1))
                ++collapsed;
        for (unsigned budget = 0; budget < 4 && nodes.size() < capacity(params.simulation_quality);
            ++budget) {
            auto &distance = distance_scratch_;
            auto &strain = strain_scratch_;
            if (distance.capacity() < nodes.size())
                distance.reserve(std::max(nodes.size(), distance.capacity() * 2));
            if (strain.capacity() < nodes.size())
                strain.reserve(std::max(nodes.size(), strain.capacity() * 2));
            distance.assign(nodes.size(), 3);
            strain.assign(nodes.size(), 0);
            for (const auto &face : faces) {
                const double value = compression(face, nodes, material_size);
                for (const auto vertex : face.v) strain[vertex] = std::max(strain[vertex], value);
            }
            for (std::size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i].contact_age) distance[i] = 0;
            for (unsigned ring = 0; ring < 2; ++ring)
                for (const auto &h : hinges) {
                    if (distance[h.a] == ring) distance[h.b] = std::min(distance[h.b], ring + 1);
                    if (distance[h.b] == ring) distance[h.a] = std::min(distance[h.a], ring + 1);
                }
            auto &candidates = candidate_scratch_;
            candidates.clear();
            if (candidates.capacity() < hinges.size())
                candidates.reserve(std::max(hinges.size(), candidates.capacity() * 2));
            for (std::size_t i = 0; i < hinges.size(); ++i) {
                const auto &h = hinges[i];
                if (h.length < base_length_ * 0.24) continue;
                const double compressive_strain = std::max(strain[h.a], strain[h.b]);
                const double bend =
                    std::max(std::abs(angle(h, nodes) - h.rest_angle), std::abs(h.memory));
                const double contact =
                    std::min(distance[h.a], distance[h.b]) <= 2 && h.length > base_length_ * 0.55
                        ? 1.1
                        : 0;
                // Physical strain and curvature, not animation progress, drive resolution.
                const double score = std::max({compressive_strain / 0.12, bend / 0.3, contact});
                if (score > 1) {
                    const auto target = refinement_edge(i);
                    if (target != SIZE_MAX) candidates.push_back({score, target, i});
                }
            }
            // Source hinge order reproduces stable_sort ties even when distinct
            // physical features promote the same prerequisite quality edge.
            std::sort(
                candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
                    return a.score != b.score ? a.score > b.score : a.order < b.order;
                });
            bool changed = false;
            for (const auto &candidate : candidates) {
                if (split_edge(candidate.target, tick)) {
                    changed = true;
                    break;
                }
            }
            if (!changed) break;
        }
    }

}  // namespace hyprimate::paper
