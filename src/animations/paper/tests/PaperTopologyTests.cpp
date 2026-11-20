#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include "Check.hpp"
#include "animations/paper/PaperContact.hpp"
#include "animations/paper/PaperTopology.hpp"

using namespace hyprimate;
using namespace hyprimate::paper;

namespace {
    bool near(double a, double b, double tolerance = 1e-10) {
        return std::abs(a - b) <= tolerance;
    }
    Vec3 material(const Topology &mesh, Vec2 uv) {
        return {(uv.x - 0.5) * mesh.material_size.x, (uv.y - 0.5) * mesh.material_size.y, 0};
    }
    Vec3 momentum(const Topology &mesh) {
        Vec3 result{};
        for (const auto &n : mesh.nodes) {
            result.x += n.velocity.x / n.inv_mass;
            result.y += n.velocity.y / n.inv_mass;
            result.z += n.velocity.z / n.inv_mass;
        }
        return result;
    }
    Vec3 angular_momentum(const Topology &mesh) {
        Vec3 result{};
        for (const auto &n : mesh.nodes) {
            result.x += (n.x.y * n.velocity.z - n.x.z * n.velocity.y) / n.inv_mass;
            result.y += (n.x.z * n.velocity.x - n.x.x * n.velocity.z) / n.inv_mass;
            result.z += (n.x.x * n.velocity.y - n.x.y * n.velocity.x) / n.inv_mass;
        }
        return result;
    }
    double mass(const Topology &mesh) {
        double total = 0;
        for (const auto &n : mesh.nodes) total += 1 / n.inv_mass;
        return total;
    }
    void check_mesh(const Topology &mesh) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> edges;
        std::set<std::uint64_t> ids;
        for (const auto &n : mesh.nodes) CHECK(ids.insert(n.id).second);
        double area = 0;
        for (const auto &f : mesh.faces) {
            for (const auto i : f.v) CHECK(i < mesh.nodes.size());
            const auto &a = mesh.nodes[f.v[0]].uv, &b = mesh.nodes[f.v[1]].uv,
                       &c = mesh.nodes[f.v[2]].uv;
            const double twice_area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            CHECK(twice_area > 1e-12);
            const auto length = [&](Vec2 u, Vec2 v) {
                return std::hypot(
                    (u.x - v.x) * mesh.material_size.x, (u.y - v.y) * mesh.material_size.y);
            };
            const double ab = length(a, b), ac = length(a, c), bc = length(b, c);
            CHECK(twice_area * mesh.material_size.x * mesh.material_size.y /
                      std::max({ab * ac, ab * bc, ac * bc}) >=
                  0.22 - 1e-10);
            area += twice_area / 2;
            for (unsigned j = 0; j < 3; ++j) ++edges[std::minmax(f.v[j], f.v[(j + 1) % 3])];
        }
        CHECK(near(area, 1));
        CHECK(edges.size() == mesh.hinges.size());
        for (const auto &[edge, count] : edges) {
            CHECK(count == 1 || count == 2);
            const auto &a = mesh.nodes[edge.first].uv, &b = mesh.nodes[edge.second].uv;
            // A conforming triangulation has no material point strictly inside an edge.
            for (std::uint32_t i = 0; i < mesh.nodes.size(); ++i) {
                if (i == edge.first || i == edge.second) continue;
                const auto &p = mesh.nodes[i].uv;
                const double cross = (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
                const double along = (p.x - a.x) * (p.x - b.x) + (p.y - a.y) * (p.y - b.y);
                CHECK(std::abs(cross) > 1e-12 || along >= -1e-12);
            }
        }
        for (const auto &h : mesh.hinges) {
            const auto &a = mesh.nodes[h.a].uv, &b = mesh.nodes[h.b].uv;
            CHECK(near(h.length,
                std::hypot(
                    (a.x - b.x) * mesh.material_size.x, (a.y - b.y) * mesh.material_size.y)));
            CHECK(h.c < mesh.nodes.size());
            CHECK(h.d == UINT32_MAX || h.d < mesh.nodes.size());
        }
        CHECK(near(mass(mesh), 1));
    }
    void set_affine_state(Topology &mesh, double stretch) {
        for (auto &n : mesh.nodes) {
            n.x = material(mesh, n.uv);
            n.x.x *= stretch;
            n.velocity = {n.uv.x + 2 * n.uv.y, 3 * n.uv.x - n.uv.y, 0.4};
            n.prev = {n.x.x - n.velocity.x * 0.001,
                n.x.y - n.velocity.y * 0.001,
                n.x.z - n.velocity.z * 0.001};
        }
    }
}  // namespace

int main() {
    PaperParams params;
    params.simulation_quality = PaperQuality::Medium;
    Topology mesh;
    mesh.initialize({900, 100}, 17, params);
    check_mesh(mesh);
    CHECK(near(mesh.material_size.x, 3));
    CHECK(near(mesh.material_size.y, 1.0 / 3));
    for (const auto &f : mesh.faces) {
        double shortest = 1e9, longest = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const auto &a = mesh.nodes[f.v[j]].x, &b = mesh.nodes[f.v[(j + 1) % 3]].x;
            const double length = std::hypot(a.x - b.x, a.y - b.y);
            shortest = std::min(shortest, length);
            longest = std::max(longest, length);
        }
        CHECK(longest / shortest < 1.7);
    }
    Topology other;
    other.initialize({900, 100}, 999, params);
    CHECK(other.nodes.size() == mesh.nodes.size());
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        CHECK(other.nodes[i].x.x == mesh.nodes[i].x.x);
        CHECK(other.nodes[i].x.y == mesh.nodes[i].x.y);
    }
    const auto medium_count = mesh.nodes.size();
    params.simulation_quality = PaperQuality::High;
    other.initialize({900, 100}, 17, params);
    CHECK(other.nodes.size() > medium_count);
    params.simulation_quality = PaperQuality::Low;
    other.initialize({900, 100}, 17, params);
    CHECK(other.nodes.size() < medium_count);
    params.simulation_quality = PaperQuality::Medium;

    mesh.initialize({400, 300}, 17, params);
    const auto original_nodes = mesh.nodes.size(), original_faces = mesh.faces.size();
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() == original_nodes);  // No progress-only or gratuitous refinement.
    set_affine_state(mesh, 0.7);
    const auto before = mesh.nodes;
    const auto initial_momentum = momentum(mesh);
    const auto initial_angular = angular_momentum(mesh);
    mesh.adapt(params, 8);
    CHECK(mesh.nodes.size() > original_nodes);
    check_mesh(mesh);
    CHECK(near(momentum(mesh).x, initial_momentum.x));
    CHECK(near(momentum(mesh).y, initial_momentum.y));
    CHECK(near(momentum(mesh).z, initial_momentum.z));
    CHECK(near(angular_momentum(mesh).x, initial_angular.x));
    CHECK(near(angular_momentum(mesh).y, initial_angular.y));
    CHECK(near(angular_momentum(mesh).z, initial_angular.z));
    for (std::size_t i = 0; i < original_nodes; ++i) {
        CHECK(mesh.nodes[i].id == before[i].id);
        CHECK(mesh.nodes[i].x.x == before[i].x.x);
        CHECK(mesh.nodes[i].prev.x == before[i].prev.x);
    }
    for (const auto &n : mesh.nodes) {
        CHECK(near(n.x.x, material(mesh, n.uv).x * 0.7));
        CHECK(std::isfinite(n.velocity.x));
        CHECK(near(n.prev.x, n.x.x - (n.uv.x + 2 * n.uv.y) * 0.001));
    }
    const auto refined_count = mesh.nodes.size();
    set_affine_state(mesh, 1);
    // Near-affine coarsening must conserve impulse, not merely interpolate velocity.
    for (std::size_t i = original_nodes; i < mesh.nodes.size(); ++i)
        mesh.nodes[i].velocity.z += 1e-7;
    const auto coarsening_momentum = momentum(mesh);
    const auto coarsening_angular = angular_momentum(mesh);
    mesh.adapt(params, 12);
    CHECK(mesh.nodes.size() == refined_count);  // Coarsening hysteresis.
    for (auto &n : mesh.nodes) n.contact_age = 4;
    mesh.adapt(params, 48);
    CHECK(mesh.nodes.size() == refined_count);  // Contact veto includes coarsening.
    for (auto &n : mesh.nodes) n.contact_age = 0;
    for (std::uint64_t tick = 52; tick <= 80; tick += 4) mesh.adapt(params, tick);
    CHECK(mesh.nodes.size() == original_nodes);
    CHECK(near(momentum(mesh).x, coarsening_momentum.x));
    CHECK(near(momentum(mesh).y, coarsening_momentum.y));
    CHECK(near(momentum(mesh).z, coarsening_momentum.z));
    CHECK(near(angular_momentum(mesh).x, coarsening_angular.x));
    CHECK(near(angular_momentum(mesh).y, coarsening_angular.y));
    CHECK(near(angular_momentum(mesh).z, coarsening_angular.z));
    CHECK(mesh.faces.size() == original_faces);
    check_mesh(mesh);
    for (std::size_t i = 0; i < original_nodes; ++i) {
        CHECK(mesh.nodes[i].id == before[i].id);
        CHECK(near(mesh.nodes[i].x.x, material(mesh, mesh.nodes[i].uv).x));
    }

    // Full contact protection preserves exact faces even under compressive load.
    set_affine_state(mesh, 0.6);
    const auto protected_faces = mesh.faces;
    for (auto &n : mesh.nodes) n.contact_age = 4;
    mesh.adapt(params, 84);
    CHECK(mesh.nodes.size() == original_nodes);
    for (std::size_t i = 0; i < mesh.faces.size(); ++i)
        CHECK(mesh.faces[i].v == protected_faces[i].v);

    // Unprotected neighbors of contact refine without changing witness triangles.
    mesh.initialize({400, 300}, 17, params);
    const auto centre = static_cast<std::uint32_t>(mesh.nodes.size() / 2);
    mesh.nodes[centre].contact_age = 4;
    std::vector<Face> witness;
    for (const auto &f : mesh.faces)
        if (std::find(f.v.begin(), f.v.end(), centre) != f.v.end()) witness.push_back(f);
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() > original_nodes);
    for (const auto &f : witness)
        CHECK(std::any_of(mesh.faces.begin(), mesh.faces.end(), [&](const Face &candidate) {
            return candidate.v == f.v;
        }));
    check_mesh(mesh);

    // A piecewise rigid fold is isometric: curvature alone must refine it.
    mesh.initialize({400, 300}, 17, params);
    for (auto &n : mesh.nodes) {
        if (n.x.x > 0) {
            n.x.z = n.x.x * std::sin(0.8);
            n.x.x *= std::cos(0.8);
        }
        n.prev = n.x;
    }
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() > original_nodes);
    check_mesh(mesh);

    // Signed plastic memory and rest angle survive physical edge subdivision.
    mesh.initialize({400, 300}, 17, params);
    const auto hinge = std::find_if(
        mesh.hinges.begin(), mesh.hinges.end(), [](const Hinge &h) { return h.d != UINT32_MAX; });
    CHECK(hinge != mesh.hinges.end());
    const auto a = mesh.nodes[hinge->a].uv, b = mesh.nodes[hinge->b].uv;
    const double original_length = hinge->length;
    hinge->memory = -0.8;
    hinge->rest_angle = -0.02;
    // Equivalent reversal of the directed hinge must not reverse plastic memory.
    std::swap(hinge->a, hinge->b);
    std::swap(hinge->c, hinge->d);
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() > original_nodes);
    double remembered_length = 0;
    unsigned remembered_edges = 0;
    for (const auto &h : mesh.hinges)
        if (h.memory < -0.7) {
            CHECK(near(h.memory, -0.8));
            CHECK(near(h.rest_angle, -0.02));
            for (const auto i : {h.a, h.b}) {
                const auto &p = mesh.nodes[i].uv;
                CHECK(near((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x), 0));
            }
            remembered_length += h.length;
            ++remembered_edges;
        }
    CHECK(remembered_edges >= 2);
    CHECK(near(remembered_length, original_length));
    check_mesh(mesh);

    const auto crease_count = mesh.nodes.size();
    // Flattening must not destroy stored high-memory material creases.
    for (auto &n : mesh.nodes) {
        n.x = material(mesh, n.uv);
        n.prev = n.x;
    }
    mesh.adapt(params, 40);
    CHECK(mesh.nodes.size() >= crease_count);
    double retained_memory_length = 0;
    for (const auto &h : mesh.hinges)
        if (h.memory < -0.7) retained_memory_length += h.length;
    CHECK(near(retained_memory_length, original_length));

    // An incompatible velocity field must not be averaged away by coarsening.
    mesh.initialize({400, 300}, 17, params);
    set_affine_state(mesh, 0.7);
    mesh.adapt(params, 4);
    set_affine_state(mesh, 1);
    for (std::size_t i = original_nodes; i < mesh.nodes.size(); ++i)
        mesh.nodes[i].velocity.z = static_cast<double>(mesh.nodes[i].id * mesh.nodes[i].id);
    const auto incompatible_count = mesh.nodes.size();
    mesh.adapt(params, 40);
    CHECK(mesh.nodes.size() == incompatible_count);
    check_mesh(mesh);
    // The horizontal grid edge is shorter than its diagonal by 1.47x. A
    // curvature-only horizontal fold must still obtain conforming refinement.
    mesh.initialize({400, 300}, 17, params);
    const double fold_y = mesh.nodes[33].x.y;
    for (auto &node : mesh.nodes) {
        const double y = node.x.y - fold_y;
        if (y > 0) {
            node.x.y = fold_y + y * std::cos(0.8);
            node.x.z = y * std::sin(0.8);
        }
        node.prev = node.x;
    }
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() > original_nodes);
    check_mesh(mesh);

    // Non-affine rotating velocities exercise all three angular components.
    mesh.initialize({400, 300}, 17, params);
    set_affine_state(mesh, 0.7);
    for (auto &node : mesh.nodes)
        node.velocity = {-node.x.y + 0.3 * node.uv.x * node.uv.x,
            node.x.x + 0.2 * node.uv.y * node.uv.y,
            node.uv.x * node.uv.y};
    const auto nonlinear_linear = momentum(mesh), nonlinear_angular = angular_momentum(mesh);
    mesh.adapt(params, 4);
    CHECK(mesh.nodes.size() > original_nodes);
    CHECK(near(momentum(mesh).x, nonlinear_linear.x));
    CHECK(near(momentum(mesh).y, nonlinear_linear.y));
    CHECK(near(momentum(mesh).z, nonlinear_linear.z));
    CHECK(near(angular_momentum(mesh).x, nonlinear_angular.x));
    CHECK(near(angular_momentum(mesh).y, nonlinear_angular.y));
    CHECK(near(angular_momentum(mesh).z, nonlinear_angular.z));
    set_affine_state(mesh, 1);
    for (auto &node : mesh.nodes)
        node.velocity = {-node.x.y + 0.001 * node.uv.x * node.uv.x,
            node.x.x + 0.002 * node.uv.y * node.uv.y,
            0.001 * node.uv.x * node.uv.y};
    const auto rotating_linear = momentum(mesh), rotating_angular = angular_momentum(mesh);
    const auto rotating_count = mesh.nodes.size();
    for (std::uint64_t tick = 40; tick <= 64; tick += 4) mesh.adapt(params, tick);
    CHECK(mesh.nodes.size() < rotating_count);
    CHECK(near(momentum(mesh).x, rotating_linear.x));
    CHECK(near(momentum(mesh).y, rotating_linear.y));
    CHECK(near(momentum(mesh).z, rotating_linear.z));
    CHECK(near(angular_momentum(mesh).x, rotating_angular.x));
    CHECK(near(angular_momentum(mesh).y, rotating_angular.y));
    CHECK(near(angular_momentum(mesh).z, rotating_angular.z));
    check_mesh(mesh);
    // Protected witness geometry survives unrelated inverse edits that compact
    // earlier vertex indices. Stable IDs identify the same material features.
    mesh.initialize({400, 300}, 17, params);
    const auto refine_corner = [&](bool far_corner, std::uint64_t tick) {
        const auto selected =
            std::find_if(mesh.hinges.begin(), mesh.hinges.end(), [&](const Hinge &hinge) {
                if (hinge.d == UINT32_MAX) return false;
                const auto &uv = mesh.nodes[hinge.a].uv;
                return far_corner ? uv.x > 0.75 && uv.y > 0.75 : uv.x < 0.25 && uv.y < 0.25;
            });
        CHECK(selected != mesh.hinges.end());
        selected->memory = 0.8;
        mesh.adapt(params, tick);
        for (auto &hinge : mesh.hinges) hinge.memory = 0;
        for (auto &node : mesh.nodes) {
            node.x = material(mesh, node.uv);
            node.prev = node.x;
            node.velocity = {};
        }
    };
    refine_corner(false, 4);
    const auto first_region_count = mesh.nodes.size();
    CHECK(first_region_count > original_nodes);
    std::vector<std::uint64_t> earlier_midpoints;
    for (std::size_t i = original_nodes; i < first_region_count; ++i)
        earlier_midpoints.push_back(mesh.nodes[i].id);
    refine_corner(true, 8);
    CHECK(mesh.nodes.size() > first_region_count);
    const auto witness_vertex = static_cast<std::uint32_t>(mesh.nodes.size() - 1);
    const auto witness_id = mesh.nodes[witness_vertex].id;
    const auto witness_rest = material(mesh, mesh.nodes[witness_vertex].uv);
    const double fold_plane = witness_rest.x + witness_rest.y - 0.15;
    ContactSolver contact_solver;
    const auto solve_fold_contact = [&](bool first_prediction) {
        std::vector<double> inverse_mass;
        inverse_mass.reserve(mesh.nodes.size());
        for (auto &node : mesh.nodes) {
            inverse_mass.push_back(node.inv_mass);
            const auto rest = material(mesh, node.uv);
            const double distance = rest.x + rest.y - fold_plane;
            const bool upper_wing = distance > 0;
            if (first_prediction) {
                node.x = upper_wing ? Vec3{fold_plane - rest.y, fold_plane - rest.x, 0} : rest;
                node.prev = node.x;
                if (upper_wing) node.prev.z = 0.05 * std::min(1.0, distance / 0.1);
            } else {
                node.prev = node.x;
            }
            if (upper_wing)
                node.x.z = -0.05 * std::min(1.0, distance / 0.1);
            else
                node.inv_mass = 0;
            node.contact = false;
        }
        contact_solver.solve(mesh, 0.003, 0);
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i)
            mesh.nodes[i].inv_mass = inverse_mass[i];
    };
    solve_fold_contact(true);
    CHECK(mesh.nodes[witness_vertex].contact);
    CHECK(mesh.nodes[witness_vertex].contact_age >= 4);
    CHECK(mesh.nodes[witness_vertex].x.z > 0);
    std::map<std::uint64_t, std::pair<std::uint32_t, Node>> protected_nodes;
    for (std::uint32_t i = 0; i < mesh.nodes.size(); ++i)
        if (mesh.nodes[i].contact_age > 0)
            protected_nodes.emplace(mesh.nodes[i].id, std::pair{i, mesh.nodes[i]});
    std::vector<std::array<std::uint64_t, 3>> protected_triangles;
    for (const auto &face : mesh.faces) {
        if (!std::any_of(face.v.begin(), face.v.end(), [&](auto vertex) {
                return mesh.nodes[vertex].contact_age > 0;
            }))
            continue;
        protected_triangles.push_back(
            {mesh.nodes[face.v[0]].id, mesh.nodes[face.v[1]].id, mesh.nodes[face.v[2]].id});
    }
    for (std::uint64_t tick = 40; tick <= 64; tick += 4) mesh.adapt(params, tick);
    // Contact-neighbor refinement may add vertices in the same call; identify
    // actual coarsening by removed material identities, not the net node count.
    CHECK(std::any_of(earlier_midpoints.begin(), earlier_midpoints.end(), [&](std::uint64_t id) {
        return std::none_of(
            mesh.nodes.begin(), mesh.nodes.end(), [&](const Node &node) { return node.id == id; });
    }));
    bool index_moved = false;
    for (const auto &[id, snapshot] : protected_nodes) {
        const auto found = std::find_if(
            mesh.nodes.begin(), mesh.nodes.end(), [&](const Node &node) { return node.id == id; });
        CHECK(found != mesh.nodes.end());
        if (found == mesh.nodes.end()) continue;
        index_moved |= static_cast<std::size_t>(found - mesh.nodes.begin()) != snapshot.first;
        CHECK(found->contact == snapshot.second.contact);
        CHECK(found->contact_age == snapshot.second.contact_age);
        CHECK(found->uv.x == snapshot.second.uv.x && found->uv.y == snapshot.second.uv.y);
        CHECK(found->x.x == snapshot.second.x.x && found->x.y == snapshot.second.x.y &&
              found->x.z == snapshot.second.x.z);
        CHECK(found->prev.x == snapshot.second.prev.x && found->prev.y == snapshot.second.prev.y &&
              found->prev.z == snapshot.second.prev.z);
        CHECK(found->velocity.x == snapshot.second.velocity.x &&
              found->velocity.y == snapshot.second.velocity.y &&
              found->velocity.z == snapshot.second.velocity.z);
    }
    CHECK(index_moved);
    for (const auto &triangle : protected_triangles) {
        CHECK(std::any_of(mesh.faces.begin(), mesh.faces.end(), [&](const Face &face) {
            return triangle == std::array<std::uint64_t, 3>{mesh.nodes[face.v[0]].id,
                                   mesh.nodes[face.v[1]].id,
                                   mesh.nodes[face.v[2]].id};
        }));
    }
    check_mesh(mesh);
    // Reuse the same solver after compaction. Indices from its first call must
    // not redirect a new swept prediction to unrelated material vertices.
    solve_fold_contact(false);
    const auto reacquired = std::find_if(mesh.nodes.begin(),
        mesh.nodes.end(),
        [&](const Node &node) { return node.id == witness_id; });
    CHECK(reacquired != mesh.nodes.end());
    if (reacquired != mesh.nodes.end()) {
        CHECK(reacquired->contact);
        CHECK(reacquired->contact_age >= 4);
        CHECK(reacquired->prev.z > 0);
        CHECK(reacquired->x.z > 0);
    }
    return g_failures == 0 ? 0 : 1;
}
