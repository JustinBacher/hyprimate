#include <algorithm>
#include <cmath>
#include <limits>

#include "animations/paper/PaperContact.hpp"
#include "animations/paper/PaperTopology.hpp"

namespace hyprimate::paper {
    namespace {
        Vec3 add(Vec3 a, Vec3 b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        Vec3 sub(Vec3 a, Vec3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec3 mul(Vec3 a, double s) {
            return {a.x * s, a.y * s, a.z * s};
        }
        double dot(Vec3 a, Vec3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        double length(Vec3 a) {
            return std::sqrt(dot(a, a));
        }

        struct Witness {
            std::array<double, 4> weight{};
            Vec3 delta{}, fallback{};
        };

        // Closest points of finite segments, including zero-length and parallel edges.
        Witness edges(const std::array<Vec3, 4> &x) {
            const Vec3 u = sub(x[1], x[0]), v = sub(x[3], x[2]), r = sub(x[0], x[2]);
            const double a = dot(u, u), b = dot(u, v), c = dot(v, v);
            const double d = dot(u, r), e = dot(v, r);
            double s = 0, t = 0;
            if (a > 1e-28 && c > 1e-28) {
                const double determinant = a * c - b * b;
                if (determinant > 1e-14 * a * c)
                    s = std::clamp((b * e - c * d) / determinant, 0.0, 1.0);
                t = (b * s + e) / c;
                if (t < 0) {
                    t = 0;
                    s = std::clamp(-d / a, 0.0, 1.0);
                } else if (t > 1) {
                    t = 1;
                    s = std::clamp((b - d) / a, 0.0, 1.0);
                }
            } else if (a > 1e-28) {
                s = std::clamp(-d / a, 0.0, 1.0);
            } else if (c > 1e-28) {
                t = std::clamp(e / c, 0.0, 1.0);
            }
            return {{1 - s, s, t - 1, -t},
                sub(add(x[0], mul(u, s)), add(x[2], mul(v, t))),
                cross(u, v)};
        }

        Witness vertex_face(const std::array<Vec3, 4> &x) {
            const Vec3 p = x[0], a = x[1], b = x[2], c = x[3];
            const Vec3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
            const Vec3 normal = cross(ab, ac);
            const double d1 = dot(ab, ap), d2 = dot(ac, ap);
            double u = 1, v = 0, w = 0;
            if (dot(normal, normal) < 1e-24 * std::max(1e-28, dot(ab, ab) * dot(ac, ac))) {
                // Collapsed triangles still have collidable boundary segments.
                Witness best{};
                double distance = std::numeric_limits<double>::infinity();
                for (unsigned edge = 0; edge < 3; ++edge) {
                    const unsigned j = 1 + edge, k = 1 + (edge + 1) % 3;
                    const auto closest = edges({p, p, x[j], x[k]});
                    if (dot(closest.delta, closest.delta) < distance) {
                        distance = dot(closest.delta, closest.delta);
                        best.weight = {1, 0, 0, 0};
                        best.weight[j] = closest.weight[2];
                        best.weight[k] = closest.weight[3];
                        best.delta = closest.delta;
                    }
                }
                return best;
            }
            if (d1 <= 0 && d2 <= 0) {
                // Vertex a.
            } else {
                const Vec3 bp = sub(p, b);
                const double d3 = dot(ab, bp), d4 = dot(ac, bp);
                const double vc = d1 * d4 - d3 * d2;
                if (d3 >= 0 && d4 <= d3) {
                    u = 0;
                    v = 1;
                } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
                    v = d1 / (d1 - d3);
                    u = 1 - v;
                } else {
                    const Vec3 cp = sub(p, c);
                    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
                    const double vb = d5 * d2 - d1 * d6;
                    const double va = d3 * d6 - d5 * d4;
                    if (d6 >= 0 && d5 <= d6) {
                        u = 0;
                        w = 1;
                    } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
                        w = d2 / (d2 - d6);
                        u = 1 - w;
                    } else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
                        w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                        v = 1 - w;
                        u = 0;
                    } else {
                        const double inverse = 1 / (va + vb + vc);
                        v = vb * inverse;
                        w = vc * inverse;
                        u = 1 - v - w;
                    }
                }
            }
            return {{1, -u, -v, -w}, sub(p, add(add(mul(a, u), mul(b, v)), mul(c, w))), normal};
        }

        // A witness-local exemption cannot discard a whole swept feature: another,
        // nonlocal material point may collide later. Exempt only features whose entire
        // convex material envelopes are intrinsically within one sheet thickness.
        bool intrinsically_local(const Topology &mesh,
            const std::array<std::uint32_t, 4> &index,
            bool vf,
            double thickness) {
            const unsigned a_count = vf ? 1U : 2U, b_begin = vf ? 1U : 2U;
            for (unsigned a = 0; a < a_count; ++a) {
                for (unsigned b = b_begin; b < 4; ++b) {
                    const auto u = mesh.nodes[index[a]].uv, v = mesh.nodes[index[b]].uv;
                    const double x = (u.x - v.x) * mesh.material_size.x;
                    const double y = (u.y - v.y) * mesh.material_size.y;
                    if (x * x + y * y > thickness * thickness) return false;
                }
            }
            return true;
        }

        // Distance is Lipschitz with bound max vertex speed(A)+max vertex speed(B).
        // Conservative advancement therefore cannot jump over a thin contact interval,
        // unlike uniform temporal sampling or a test only at the predicted endpoint.
        bool swept(const Topology &mesh,
            const std::array<std::uint32_t, 4> &index,
            bool vf,
            double thickness,
            Witness &witness,
            Vec3 &normal,
            double &impact) {
            std::array<Vec3, 4> x{}, movement{}, start{};
            for (unsigned i = 0; i < 4; ++i) {
                start[i] = mesh.nodes[index[i]].prev;
                movement[i] = sub(mesh.nodes[index[i]].x, start[i]);
            }
            // Remove common transport before bounding relative feature motion. This
            // keeps fast, co-moving folds from exhausting conservative advancement.
            const double s1 = length(sub(movement[1], movement[0]));
            const double s2 = length(sub(movement[2], movement[0]));
            const double s3 = length(sub(movement[3], movement[0]));
            const double speed = vf ? std::max({s1, s2, s3}) : s1 + std::max(s2, s3);
            const double tolerance = std::max(1e-10, thickness * 1e-5);
            double t = 0;
            for (unsigned iteration = 0; iteration < 160; ++iteration) {
                for (unsigned i = 0; i < 4; ++i) x[i] = add(start[i], mul(movement[i], t));
                witness = vf ? vertex_face(x) : edges(x);
                const double distance = length(witness.delta);
                if (distance <= thickness + tolerance || iteration == 159) {
                    // Exhausted advancement remains conservative: retain the last safe
                    // witness rather than silently accept a possible grazing crossing.
                    if (distance > 1e-14)
                        normal = mul(witness.delta, 1 / distance);
                    else {
                        Vec3 motion{};
                        for (unsigned i = 0; i < 4; ++i)
                            motion = add(motion, mul(movement[i], witness.weight[i]));
                        normal = witness.fallback;
                        if (length(normal) < 1e-14) normal = mul(motion, -1);
                        if (length(normal) < 1e-14) normal = {0, 0, 1};
                        normal = mul(normal, 1 / length(normal));
                        if (dot(normal, motion) > 0) normal = mul(normal, -1);
                    }
                    impact = t;
                    return true;
                }
                if (speed < 1e-15 || distance - thickness > speed * (1 - t)) return false;
                // Use the closest-feature separating plane, not the magnitude of
                // tangential motion, to bound the next possible impact. All projected
                // vertex-pair gaps evolve linearly even when the triangles rotate.
                // This is conservative for the entire convex features and rejects
                // fast co-moving/sliding layers without hundreds of tiny time steps.
                const Vec3 axis = mul(witness.delta, 1 / distance);
                double advance = std::numeric_limits<double>::infinity();
                const unsigned a_count = vf ? 1U : 2U;
                const unsigned b_begin = vf ? 1U : 2U;
                for (unsigned a = 0; a < a_count; ++a) {
                    for (unsigned b = b_begin; b < 4; ++b) {
                        const double closing = -dot(sub(movement[a], movement[b]), axis);
                        if (closing <= 0) continue;
                        const double gap = dot(sub(x[a], x[b]), axis);
                        advance = std::min(advance, std::max(0.0, gap - thickness) / closing);
                    }
                }
                if (advance > 1 - t) return false;
                t += 0.9 * advance;
            }
            return false;
        }

        bool residual_impact(const Topology &mesh,
            const std::array<std::uint32_t, 4> &index,
            bool vf,
            double thickness,
            Witness &witness,
            Vec3 &normal,
            double &radius,
            double &impact) {
            if (intrinsically_local(mesh, index, vf, thickness)) return false;
            std::array<Vec3, 4> before{};
            for (unsigned i = 0; i < 4; ++i) before[i] = mesh.nodes[index[i]].prev;
            const Witness initial = vf ? vertex_face(before) : edges(before);
            const double distance = length(initial.delta);
            Vec3 axis = distance > 1e-14 ? mul(initial.delta, 1 / distance) : initial.fallback;
            const double axis_length = length(axis);
            if (axis_length > 1e-14) {
                // This fixed plane separates the entire features for the
                // whole substep, including already-close separating contacts.
                axis = mul(axis, 1 / axis_length);
                double minimum_start = std::numeric_limits<double>::infinity();
                double minimum_end = minimum_start;
                const unsigned a_count = vf ? 1U : 2U, b_begin = vf ? 1U : 2U;
                for (unsigned a = 0; a < a_count; ++a) {
                    for (unsigned b = b_begin; b < 4; ++b) {
                        minimum_start =
                            std::min(minimum_start, dot(sub(before[a], before[b]), axis));
                        minimum_end = std::min(minimum_end,
                            dot(sub(mesh.nodes[index[a]].x, mesh.nodes[index[b]].x), axis));
                    }
                }
                if (minimum_start >= 0 && minimum_end > 0) return false;
            }
            // Start strictly outside the guard even for a valid narrow gap.
            radius = std::max(1e-14, std::min(thickness * 0.001, distance * 0.25));
            return swept(mesh, index, vf, radius, witness, normal, impact);
        }
    }  // namespace

    void ContactSolver::reset() {
        bounds_.clear();
        node_bounds_.clear();
        features_.clear();
        active_.clear();
        edges_.clear();
        contacts_.clear();
        protected_.clear();
        node_diagonal_.clear();
        face_diagonal_.clear();
        edge_diagonal_.clear();
        impact_goal_.clear();
        impact_transport_.clear();
        impact_parent_.clear();
        impact_fraction_.clear();
        impact_mass_.clear();
        impact_fixed_.clear();
    }

    void ContactSolver::broadphase(const Topology &mesh, double thickness) {
        bounds_.clear();
        features_.clear();
        node_bounds_.resize(mesh.nodes.size());
        node_diagonal_.resize(mesh.nodes.size());
        face_diagonal_.resize(mesh.faces.size());
        edge_diagonal_.resize(edges_.size());
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
            const auto &node = mesh.nodes[i];
            const double padding = thickness * 0.5;
            node_bounds_[i].lo = {std::min(node.prev.x, node.x.x) - padding,
                std::min(node.prev.y, node.x.y) - padding,
                std::min(node.prev.z, node.x.z) - padding};
            node_bounds_[i].hi = {std::max(node.prev.x, node.x.x) + padding,
                std::max(node.prev.y, node.x.y) + padding,
                std::max(node.prev.z, node.x.z) + padding};
            const auto project = [](Vec3 p) {
                return std::array<double, 6>{
                    p.x + p.y, p.x - p.y, p.x + p.z, p.x - p.z, p.y + p.z, p.y - p.z};
            };
            const auto before = project(node.prev), after = project(node.x);
            // Each feature receives half the separation radius along length-sqrt2
            // axes. Padding is outward, including roundoff at normalized scales.
            const double diagonal_padding = thickness * 0.7071067811865476 + 1e-12;
            for (unsigned axis = 0; axis < 6; ++axis) {
                node_diagonal_[i].lo[axis] = std::min(before[axis], after[axis]) - diagonal_padding;
                node_diagonal_[i].hi[axis] = std::max(before[axis], after[axis]) + diagonal_padding;
            }
        }
        const auto extend = [](Bounds &box, const Bounds &other) {
            box.lo = {std::min(box.lo.x, other.lo.x),
                std::min(box.lo.y, other.lo.y),
                std::min(box.lo.z, other.lo.z)};
            box.hi = {std::max(box.hi.x, other.hi.x),
                std::max(box.hi.y, other.hi.y),
                std::max(box.hi.z, other.hi.z)};
        };
        const auto extend_diagonal = [](DiagonalBounds &box, const DiagonalBounds &other) {
            for (unsigned axis = 0; axis < 6; ++axis) {
                box.lo[axis] = std::min(box.lo[axis], other.lo[axis]);
                box.hi[axis] = std::max(box.hi[axis], other.hi[axis]);
            }
        };
        constexpr std::uint32_t vertex_bit = 1U << 31U;
        for (std::uint32_t f = 0; f < mesh.faces.size(); ++f) {
            const auto &face = mesh.faces[f].v;
            auto box = node_bounds_[face[0]];
            extend(box, node_bounds_[face[1]]);
            extend(box, node_bounds_[face[2]]);
            box.face = f;
            bounds_.push_back(box);
            auto diagonal = node_diagonal_[face[0]];
            extend_diagonal(diagonal, node_diagonal_[face[1]]);
            extend_diagonal(diagonal, node_diagonal_[face[2]]);
            face_diagonal_[f] = diagonal;
        }
        for (std::uint32_t i = 0; i < node_bounds_.size(); ++i) {
            auto box = node_bounds_[i];
            box.face = i | vertex_bit;
            bounds_.push_back(box);
        }
        // Sweep actual features, not face pairs expanded into 15 redundant tests.
        // Each VF and each unordered EE candidate is emitted exactly once.
        const auto sweep = [&](bool vf) {
            if (bounds_.empty()) return;
            Vec3 span{}, lo = bounds_[0].lo, hi = bounds_[0].hi;
            for (const auto &box : bounds_) {
                span = add(span, sub(box.hi, box.lo));
                lo = {std::min(lo.x, box.lo.x), std::min(lo.y, box.lo.y), std::min(lo.z, box.lo.z)};
                hi = {std::max(hi.x, box.hi.x), std::max(hi.y, box.hi.y), std::max(hi.z, box.hi.z)};
            }
            const Vec3 extent = sub(hi, lo);
            const Vec3 score{extent.x / std::max(span.x, 1e-30),
                extent.y / std::max(span.y, 1e-30),
                extent.z / std::max(span.z, 1e-30)};
            const unsigned axis =
                score.z > score.x && score.z > score.y ? 2U : (score.y > score.x ? 1U : 0U);
            const auto coordinate = [axis](Vec3 p) {
                return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
            };
            std::sort(bounds_.begin(), bounds_.end(), [&](const Bounds &a, const Bounds &b) {
                const double x = coordinate(a.lo), y = coordinate(b.lo);
                return x == y ? a.face < b.face : x < y;
            });
            active_.clear();
            for (std::uint32_t i = 0; i < bounds_.size(); ++i) {
                const auto &a = bounds_[i];
                std::erase_if(
                    active_, [&](auto j) { return coordinate(bounds_[j].hi) < coordinate(a.lo); });
                for (const auto j : active_) {
                    const auto &b = bounds_[j];
                    if (vf && ((a.face ^ b.face) & vertex_bit) == 0) continue;
                    if (a.lo.x > b.hi.x || b.lo.x > a.hi.x || a.lo.y > b.hi.y || b.lo.y > a.hi.y ||
                        a.lo.z > b.hi.z || b.lo.z > a.hi.z)
                        continue;
                    const auto diagonal_for = [&](const Bounds &box) -> const DiagonalBounds & {
                        if (!vf) return edge_diagonal_[box.face];
                        return (box.face & vertex_bit) ? node_diagonal_[box.face & ~vertex_bit]
                                                       : face_diagonal_[box.face];
                    };
                    const auto &ad = diagonal_for(a);
                    const auto &bd = diagonal_for(b);
                    bool separated = false;
                    for (unsigned diagonal = 0; diagonal < 6; ++diagonal) {
                        if (ad.lo[diagonal] > bd.hi[diagonal] ||
                            bd.lo[diagonal] > ad.hi[diagonal]) {
                            separated = true;
                            break;
                        }
                    }
                    if (separated) continue;
                    if (vf) {
                        const auto vertex =
                            (a.face & vertex_bit) ? a.face & ~vertex_bit : b.face & ~vertex_bit;
                        const auto face = mesh.faces[(a.face & vertex_bit) ? b.face : a.face].v;
                        if (vertex == face[0] || vertex == face[1] || vertex == face[2]) continue;
                        features_.push_back({0, vertex, face[0], face[1], face[2]});
                    } else {
                        const auto &u = edges_[a.face];
                        const auto &v = edges_[b.face];
                        if (u[0] == v[0] || u[0] == v[1] || u[1] == v[0] || u[1] == v[1]) continue;
                        features_.push_back({1, u[0], u[1], v[0], v[1]});
                    }
                }
                active_.push_back(i);
            }
        };
        sweep(true);
        bounds_.clear();
        for (std::uint32_t e = 0; e < edges_.size(); ++e) {
            auto box = node_bounds_[edges_[e][0]];
            extend(box, node_bounds_[edges_[e][1]]);
            box.face = e;
            bounds_.push_back(box);
            auto diagonal = node_diagonal_[edges_[e][0]];
            extend_diagonal(diagonal, node_diagonal_[edges_[e][1]]);
            edge_diagonal_[e] = diagonal;
        }
        sweep(false);
    }

    void ContactSolver::narrowphase(const Topology &mesh, double thickness) {
        contacts_.clear();
        for (const auto &feature : features_) {
            const std::array<std::uint32_t, 4> index{
                feature[1], feature[2], feature[3], feature[4]};
            if (intrinsically_local(mesh, index, feature[0] == 0, thickness)) continue;
            Witness witness;
            Vec3 normal;
            double impact = 0;
            if (!swept(mesh, index, feature[0] == 0, thickness, witness, normal, impact)) continue;
            Vec3 previous{};
            double mass = 0;
            for (unsigned i = 0; i < 4; ++i) {
                const auto &node = mesh.nodes[index[i]];
                const Vec3 at_impact = add(node.prev, mul(sub(node.x, node.prev), impact));
                previous = add(previous, mul(at_impact, witness.weight[i]));
                mass += node.inv_mass * witness.weight[i] * witness.weight[i];
            }
            if (mass > 1e-24)
                contacts_.push_back({index, witness.weight, normal, {}, 0, previous, 1 / mass});
            for (const auto i : index) protected_[i] = 1;
        }
    }

    void ContactSolver::resolve_impacts(Topology &mesh, double thickness) {
        const double core = thickness * 0.001;
        // Resolve newly discovered hard-core impacts by removing only closing
        // normal motion. Tangential and separating wing motion remains free.
        for (unsigned refinement = 0; refinement < 8; ++refinement) {
            broadphase(mesh, core);
            bool found = false, corrected = false;
            for (const auto &feature : features_) {
                const std::array<std::uint32_t, 4> index{
                    feature[1], feature[2], feature[3], feature[4]};
                Witness witness;
                Vec3 normal;
                double radius = 0, impact = 1;
                if (!residual_impact(
                        mesh, index, feature[0] == 0, thickness, witness, normal, radius, impact))
                    continue;
                found = true;
                Vec3 relative{};
                double mass = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    const auto &node = mesh.nodes[index[i]];
                    relative = add(relative, mul(node.x, witness.weight[i]));
                    mass += node.inv_mass * witness.weight[i] * witness.weight[i];
                    protected_[index[i]] = 1;
                }
                if (mass <= 1e-24) continue;
                const double target = radius + std::max(1e-10, radius * 1e-5);
                const double impulse = (target - dot(relative, normal)) / mass;
                if (impulse <= 0) continue;
                for (unsigned i = 0; i < 4; ++i) {
                    auto &node = mesh.nodes[index[i]];
                    node.x = add(node.x, mul(normal, impulse * node.inv_mass * witness.weight[i]));
                }
                corrected = true;
            }
            if (!found) return;
            if (!corrected) break;
        }

        // Bounded normal refinement can leave mutually blocking impacts.
        // Limit only their connected material zones, not the entire sheet.
        const std::size_t count = mesh.nodes.size();
        auto &goal = impact_goal_;
        auto &transport = impact_transport_;
        auto &parent = impact_parent_;
        auto &fraction = impact_fraction_;
        auto &mass = impact_mass_;
        auto &fixed = impact_fixed_;
        goal.resize(count);
        transport.resize(count);
        parent.resize(count);
        fraction.assign(count, 1);
        mass.resize(count);
        fixed.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            parent[i] = i;
            goal[i] = mesh.nodes[i].x;
        }
        const auto root = [&](std::uint32_t i) {
            while (parent[i] != i) {
                parent[i] = parent[parent[i]];
                i = parent[i];
            }
            return i;
        };
        // Each nonterminal iteration merges components or rigidifies a zone.
        // At most N-1 merges and N rigidifications are possible. Redetection
        // after every update catches impacts introduced at the zone boundary.
        for (std::size_t iteration = 0; iteration <= count * 2; ++iteration) {
            broadphase(mesh, core);
            bool changed = false;
            for (const auto &feature : features_) {
                const std::array<std::uint32_t, 4> index{
                    feature[1], feature[2], feature[3], feature[4]};
                const auto first = root(index[0]);
                bool internal = fraction[first] == 0;
                for (const auto i : index) internal = internal && root(i) == first;
                // A rigidly translated feature pair has exactly its initial
                // relative trajectory; it cannot introduce an intersection.
                if (internal) continue;
                Witness witness;
                Vec3 normal;
                double radius = 0, impact = 1;
                if (!residual_impact(
                        mesh, index, feature[0] == 0, thickness, witness, normal, radius, impact))
                    continue;
                auto merged = first;
                bool previously_limited = false;
                for (const auto i : index) {
                    const auto component = root(i);
                    merged = std::min(merged, component);
                    previously_limited = previously_limited || fraction[component] < 1;
                }
                for (const auto i : index) parent[root(i)] = merged;
                fraction[merged] = previously_limited ? 0 : impact * 0.9999;
                for (const auto i : index) protected_[i] = 1;
                changed = true;
            }
            if (!changed) return;
            std::fill(transport.begin(), transport.end(), Vec3{});
            std::fill(mass.begin(), mass.end(), 0);
            std::fill(fixed.begin(), fixed.end(), 0);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto component = root(i);
                if (fraction[component] == 1) continue;
                const auto movement = sub(goal[i], mesh.nodes[i].prev);
                if (mesh.nodes[i].inv_mass <= 0) {
                    // A prescribed/static node fixes this zone's frame.
                    transport[component] = movement;
                    fixed[component] = 1;
                } else if (!fixed[component]) {
                    const double weight = 1 / mesh.nodes[i].inv_mass;
                    transport[component] = add(transport[component], mul(movement, weight));
                    mass[component] += weight;
                }
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                if (parent[i] == i && fraction[i] < 1 && !fixed[i] && mass[i] > 0)
                    transport[i] = mul(transport[i], 1 / mass[i]);
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto component = root(i);
                const auto common = transport[component];
                // Restoring common transport changes no relative contact
                // trajectory, while preserving intended mass-weighted COM.
                mesh.nodes[i].x = fraction[component] == 1
                                      ? goal[i]
                                      : add(mesh.nodes[i].prev,
                                            add(common,
                                                mul(sub(sub(goal[i], mesh.nodes[i].prev), common),
                                                    fraction[component])));
            }
        }
    }

    void ContactSolver::solve(Topology &mesh,
        double thickness,
        double friction,
        void (*elasticity)(void *),
        void *context) {
        if (!(thickness > 0) || !std::isfinite(thickness)) {
            for (auto &node : mesh.nodes) node.contact = false;
            return;
        }
        friction = std::isfinite(friction) ? std::max(0.0, friction) : 0;
        // Topology is invariant during a solve, including elasticity callbacks.
        // Canonicalize its edge list once, not once per contact rediscovery pass.
        edges_.clear();
        for (const auto &face : mesh.faces) {
            for (unsigned i = 0; i < 3; ++i) {
                const auto a = face.v[i], b = face.v[(i + 1) % 3];
                edges_.push_back({std::min(a, b), std::max(a, b)});
            }
        }
        std::sort(edges_.begin(), edges_.end());
        edges_.erase(std::unique(edges_.begin(), edges_.end()), edges_.end());
        protected_.assign(mesh.nodes.size(), 0);
        // Refresh acceleration after projection too: corrections can introduce a
        // contact outside the original swept boxes. No all-pairs solver sweep.
        const unsigned passes = elasticity ? 5U : 3U;
        bool saw_contacts = false;
        for (unsigned pass = 0; pass < passes; ++pass) {
            broadphase(mesh, thickness);
            narrowphase(mesh, thickness);
            saw_contacts = saw_contacts || !contacts_.empty();
            if (contacts_.empty()) break;
            bool changed = false;
            const double tolerance_squared = thickness * thickness * 1e-6;
            for (unsigned iteration = 0; iteration < 16; ++iteration) {
                double largest = 0;
                for (auto &contact : contacts_) {
                    Vec3 relative{};
                    for (unsigned i = 0; i < 4; ++i) {
                        const auto &node = mesh.nodes[contact.node[i]];
                        const double w = contact.weight[i];
                        relative = add(relative, mul(node.x, w));
                    }
                    const Vec3 displacement = sub(relative, contact.previous);
                    const double next = std::max(0.0,
                        contact.impulse +
                            (thickness - dot(relative, contact.normal)) * contact.inverse_mass);
                    Vec3 correction = mul(contact.normal, next - contact.impulse);
                    contact.impulse = next;
                    const Vec3 tangent =
                        sub(displacement, mul(contact.normal, dot(displacement, contact.normal)));
                    Vec3 target = sub(contact.tangent, mul(tangent, contact.inverse_mass));
                    const double limit = friction * next;
                    const double magnitude = length(target);
                    if (magnitude > limit) target = mul(target, limit / magnitude);
                    correction = add(correction, sub(target, contact.tangent));
                    contact.tangent = target;
                    for (unsigned i = 0; i < 4; ++i) {
                        auto &node = mesh.nodes[contact.node[i]];
                        const Vec3 delta = mul(correction, node.inv_mass * contact.weight[i]);
                        largest = std::max(largest, dot(delta, delta));
                        node.x = add(node.x, delta);
                    }
                }
                changed = changed || largest > tolerance_squared;
                if (largest <= tolerance_squared) break;
            }
            if (!changed) break;
            if (elasticity && pass < 2) elasticity(context);
        }
        // A converged nearest tip can hide a later swing of the other
        // vertices. Guard every discovered contact, not only large corrections.
        if (saw_contacts) resolve_impacts(mesh, thickness);
        // Unique edge witnesses do not carry an incident face's third vertex.
        // Recover the entire contacting patch before protecting its neighbors.
        for (const auto &face : mesh.faces) {
            if ((protected_[face.v[0]] & 1U) || (protected_[face.v[1]] & 1U) ||
                (protected_[face.v[2]] & 1U))
                for (const auto i : face.v) protected_[i] |= 2U;
        }
        // Snapshot flags first: propagation is exactly one ring, not an accidental
        // flood through all faces. Topology vetoes edits touching these witnesses.
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
            mesh.nodes[i].contact = (protected_[i] & 1U) != 0;
            if (protected_[i]) mesh.nodes[i].contact_age = std::max(mesh.nodes[i].contact_age, 4U);
        }
        for (const auto &face : mesh.faces) {
            if (protected_[face.v[0]] || protected_[face.v[1]] || protected_[face.v[2]]) {
                for (const auto i : face.v)
                    mesh.nodes[i].contact_age = std::max(mesh.nodes[i].contact_age, 4U);
            }
        }
        // Deliberately retain capacities, never index relationships across calls.
        contacts_.clear();
        features_.clear();
        active_.clear();
        bounds_.clear();
    }
}  // namespace hyprimate::paper
