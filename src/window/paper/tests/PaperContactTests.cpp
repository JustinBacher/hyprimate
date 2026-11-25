#include <algorithm>
#include <cmath>

#include "Check.hpp"
#include "window/paper/PaperContact.hpp"
#include "window/paper/PaperTopology.hpp"

using namespace hyprimate;
using namespace hyprimate::paper;

namespace {
    Topology layers(double travel, double slide = 0) {
        Topology mesh;
        const Vec3 base[] = {
            {-2, -2, 0}, {2, -2, 0}, {0, 2, 0}, {-0.2, -0.2, 0.1}, {0.2, -0.2, 0.1}, {0, 0.2, 0.1}};
        for (unsigned i = 0; i < 6; ++i) {
            Node node{};
            node.uv = {static_cast<double>(i), static_cast<double>(i % 2)};
            node.prev = base[i];
            node.x = base[i];
            node.inv_mass = i < 3 ? 0 : 1;
            if (i >= 3) {
                node.x.z += travel;
                node.x.x += slide;
            }
            mesh.nodes.push_back(node);
        }
        mesh.faces = {{{0, 1, 2}}, {{3, 4, 5}}};
        return mesh;
    }

    Vec3 difference(Vec3 lhs, Vec3 rhs) {
        return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    Vec3 cross_product(Vec3 lhs, Vec3 rhs) {
        return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
    }

    double scalar_product(Vec3 lhs, Vec3 rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    bool tip_pierces(const Topology &mesh, std::uint32_t endpoint) {
        const auto origin = mesh.nodes[3].x;
        const auto direction = difference(mesh.nodes[endpoint].x, origin);
        const auto first = mesh.nodes[0].x;
        const auto edge1 = difference(mesh.nodes[1].x, first);
        const auto edge2 = difference(mesh.nodes[2].x, first);
        const auto normal = cross_product(direction, edge2);
        const double determinant = scalar_product(edge1, normal);
        if (std::abs(determinant) < 1e-12) return false;
        const auto offset = difference(origin, first);
        const auto side = cross_product(offset, edge1);
        const double u = scalar_product(offset, normal) / determinant;
        const double v = scalar_product(direction, side) / determinant;
        const double time = scalar_product(edge2, side) / determinant;
        return u > 1e-5 && v > 1e-5 && u + v < 1 - 1e-5 && time > 1e-5 && time < 1 - 1e-5;
    }

    Topology converged_tip(bool movable, Vec3 translation = {}) {
        // The nearest tip is already exactly one thickness away. Its zero
        // normal correction must not hide a later swing of the other vertices.
        const std::array<Vec3, 9> positions = {{{-1, 0, 0},
            {1, 0, 0},
            {-1, -1, 0},
            {0, 0.007, 0.007},
            {0, 1, -0.1},
            {0.001, 1, -0.1},
            {4, 0, 0},
            {5, 0, 0},
            {4, 1, 0}}};
        Topology mesh;
        for (unsigned i = 0; i < positions.size(); ++i) {
            Node node{};
            node.prev = positions[i];
            node.x = {positions[i].x + translation.x,
                positions[i].y + translation.y,
                positions[i].z + translation.z};
            if (i == 4 || i == 5) node.x.y -= 2;
            node.uv = {static_cast<double>(i), static_cast<double>(i % 2)};
            node.inv_mass = movable ? static_cast<double>(i + 1) : (i < 3 ? 0 : 1);
            mesh.nodes.push_back(node);
        }
        mesh.faces = {{{0, 1, 2}}, {{3, 4, 5}}, {{6, 7, 8}}};
        return mesh;
    }
}  // namespace

int main() {
    ContactSolver contact;
    auto mesh = layers(-0.4);
    contact.solve(mesh, 0.01, 0);
    for (unsigned i = 3; i < 6; ++i) {
        CHECK(mesh.nodes[i].x.z >= 0.00999);
        CHECK(mesh.nodes[i].contact_age > 0);
    }
    for (unsigned i = 0; i < 3; ++i) CHECK(mesh.nodes[i].x.z == 0);

    // Oblique VF sweeps exercise the additional diagonal separating axes.
    // Padding must retain contact even when no sheet normal is axis-aligned.
    auto oblique = layers(-0.4);
    const auto rotate = [](Vec3 p) -> Vec3 {
        const double u = 1 / std::sqrt(2.0), v = 1 / std::sqrt(6.0), n = 1 / std::sqrt(3.0);
        return {p.x * u + p.y * v + p.z * n, -p.x * u + p.y * v + p.z * n, -2 * p.y * v + p.z * n};
    };
    for (auto &node : oblique.nodes) {
        node.prev = rotate(node.prev);
        node.x = rotate(node.x);
    }
    contact.solve(oblique, 0.01, 0);
    for (unsigned i = 3; i < 6; ++i) {
        const auto p = oblique.nodes[i].x;
        CHECK((p.x + p.y + p.z) / std::sqrt(3.0) >= 0.00999);
    }

    auto free = layers(-0.11, 0.005);
    for (unsigned i = 3; i < 6; ++i) {
        free.nodes[i].prev.z = 0.01;
        free.nodes[i].x.z = -0.01;
    }
    auto stuck = free;
    contact.reset();
    contact.solve(free, 0.01, 0);
    contact.solve(stuck, 0.01, 1);
    CHECK(std::abs(free.nodes[3].x.x - free.nodes[3].prev.x - 0.005) < 1e-7);
    CHECK(std::abs(stuck.nodes[3].x.x - stuck.nodes[3].prev.x) < 1e-6);

    auto sliding = layers(-0.11, 0.3);
    contact.solve(sliding, 0.01, 0.1);
    const double slide = sliding.nodes[3].x.x - sliding.nodes[3].prev.x;
    CHECK(slide > 0.25);
    CHECK(slide < 0.3);

    // The long free slide before a near-end impact cannot be cancelled by
    // friction. Only the small post-impact displacement may stick.
    auto late = layers(-0.0909, 0.05);
    contact.solve(late, 0.01, 1);
    const double free_slide = 0.05 * (0.1 - 0.01) / 0.0909;
    for (unsigned i = 3; i < 6; ++i) {
        CHECK(std::abs(late.nodes[i].x.x - late.nodes[i].prev.x - free_slide) < 2e-6);
        CHECK(late.nodes[i].x.z >= 0.00999);
    }

    // Intrinsically sub-thickness material neighborhoods are one continuous
    // sheet, not two bodies that must repel. The entire feature envelopes,
    // not just the first closest witness, must be local for this exemption.
    auto local = layers(-0.4);
    for (unsigned i = 0; i < local.nodes.size(); ++i)
        local.nodes[i].uv = {0.5 + i * 0.0001, 0.5 + (i % 2) * 0.0001};
    contact.solve(local, 0.01, 0);
    CHECK(std::abs(local.nodes[3].x.z + 0.3) < 1e-12);
    CHECK(!local.nodes[3].contact);

    // Adjacent wings of a closed book still collide away from their hinge.
    Topology book;
    const std::array<Vec3, 4> book_positions = {{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0.02}}};
    const std::array<Vec2, 4> book_uv = {{{0, 0.5}, {1, 0.5}, {0.5, 1}, {0.5, 0}}};
    for (unsigned i = 0; i < 4; ++i) {
        Node node{};
        node.prev = node.x = book_positions[i];
        node.uv = book_uv[i];
        node.inv_mass = i == 3 ? 1 : 0;
        book.nodes.push_back(node);
    }
    book.nodes[3].x.z = -0.02;
    book.faces = {{{0, 1, 2}}, {{1, 0, 3}}};
    contact.solve(book, 0.01, 0);
    CHECK(book.nodes[3].x.z >= 0.00999);

    // Reverse side, without resetting scratch: contact cannot choose a global
    // +z normal or carry an old event's vertex indices into a new mesh.
    auto underside = layers(-0.4);
    for (auto &node : underside.nodes) {
        node.prev.z = -node.prev.z;
        node.x.z = -node.x.z;
    }
    contact.solve(underside, 0.01, 0);
    for (unsigned i = 3; i < 6; ++i) CHECK(underside.nodes[i].x.z <= -0.00999);

    // Disjoint, crossing edges: every vertex stays outside the other face.
    // An endpoint-only or vertex/face-only detector misses this crossing.
    auto edge_crossing = layers(-0.4);
    const std::array<Vec3, 6> crossing = {
        {{-1, 0, 0}, {1, 0, 0}, {-1, -0.1, 0}, {0, -1, 0.1}, {0, 1, 0.1}, {0.1, 1, 0.1}}};
    for (unsigned i = 0; i < 6; ++i) {
        edge_crossing.nodes[i].prev = crossing[i];
        edge_crossing.nodes[i].x = crossing[i];
        if (i >= 3) edge_crossing.nodes[i].x.z -= 0.4;
    }
    contact.solve(edge_crossing, 0.01, 0);
    CHECK((edge_crossing.nodes[3].x.z + edge_crossing.nodes[4].x.z) * 0.5 >= 0.0099);

    // The closest pair lies in the interiors of long, nearly parallel edges.
    // An endpoint witness is farther away than the whole remaining approach,
    // even though the true interior gap closes below the requested thickness.
    for (const double scale : {0.1, 1.0, 10.0}) {
        Topology parallel;
        const std::array<Vec3, 6> positions = {{{-1000000, 0, 0},
            {1000000, 0, 0},
            {-1000000, -0.0001, 0},
            {-800000, -0.05, 0.02},
            {1200000, 0.05, 0.02},
            {1200000, 0.0501, 0.02}}};
        for (unsigned i = 0; i < positions.size(); ++i) {
            Node node{};
            node.uv = {static_cast<double>(i), static_cast<double>(i % 2)};
            node.prev =
                node.x = {positions[i].x * scale, positions[i].y * scale, positions[i].z * scale};
            node.inv_mass = i < 3 ? 0 : 1;
            if (i >= 3) node.x.z = 0.005 * scale;
            parallel.nodes.push_back(node);
        }
        parallel.faces = {{{0, 1, 2}}, {{3, 4, 5}}};
        contact.solve(parallel, 0.01 * scale, 0);
        // The moving edge midpoint lies on the fixed edge at x = 200000 * scale.
        CHECK((parallel.nodes[3].x.z + parallel.nodes[4].x.z) * 0.5 >= 0.0099 * scale);
    }

    // Offset parallel edges start inside the contact shell but move apart.
    // A nonseparating endpoint witness must not freeze their free motion.
    {
        Topology separating;
        const std::array<Vec3, 6> positions = {{{-2, 0, 0},
            {2, 0, 0},
            {-2, -0.1, 0},
            {-1, 0, 0.000005},
            {3, 0, 0.000005},
            {3, 0.1, 0.000005}}};
        for (unsigned i = 0; i < positions.size(); ++i) {
            Node node{};
            node.uv = {static_cast<double>(i), static_cast<double>(i % 2)};
            node.prev = node.x = positions[i];
            node.inv_mass = i < 3 ? 0 : 1;
            if (i >= 3) node.x.z = 0.02;
            separating.nodes.push_back(node);
        }
        separating.faces = {{{0, 1, 2}}, {{3, 4, 5}}};
        contact.solve(separating, 0.01, 0);
        for (unsigned i = 3; i < 6; ++i) {
            CHECK(std::abs(separating.nodes[i].x.x - positions[i].x) < 1e-12);
            CHECK(std::abs(separating.nodes[i].x.y - positions[i].y) < 1e-12);
            CHECK(std::abs(separating.nodes[i].x.z - 0.02) < 1e-12);
        }
    }

    // The two prescribed vertices are driven through each other, so normal
    // refinement cannot resolve their impact. Neither prescribed endpoint may
    // be moved when the remaining movable vertex's component is limited.
    for (const bool moving_first : {false, true}) {
        auto blocked = layers(-0.4);
        blocked.nodes[3].inv_mass = 0;
        blocked.nodes[4].inv_mass = 0;
        if (moving_first) {
            std::swap(blocked.nodes[0], blocked.nodes[3]);
            std::swap(blocked.nodes[1], blocked.nodes[4]);
            for (auto &face : blocked.faces)
                for (auto &vertex : face.v) {
                    if (vertex < 2)
                        vertex += 3;
                    else if (vertex == 3 || vertex == 4)
                        vertex -= 3;
                }
        }
        const auto prescribed = blocked.nodes;
        contact.solve(blocked, 0.01, 0);
        for (unsigned i = 0; i < blocked.nodes.size(); ++i) {
            if (prescribed[i].inv_mass > 0) continue;
            CHECK(blocked.nodes[i].x.x == prescribed[i].x.x);
            CHECK(blocked.nodes[i].x.y == prescribed[i].x.y);
            CHECK(blocked.nodes[i].x.z == prescribed[i].x.z);
        }
    }

    // A common translation does not create relative normal or friction forces.
    auto translating = layers(0);
    for (unsigned i = 3; i < 6; ++i) {
        translating.nodes[i].prev.z = 0.01;
        translating.nodes[i].x.z = 0.005;
    }
    for (auto &node : translating.nodes) {
        node.inv_mass = 1;
        node.x.x += 5;
    }
    contact.solve(translating, 0.01, 1);
    for (const auto &node : translating.nodes) CHECK(std::abs(node.x.x - node.prev.x - 5) < 1e-10);

    // Neighboring triangles are protected too: a split on their common edge
    // must not change the material witness of a contacting face.
    auto neighboring = layers(-0.4);
    for (const Vec3 position : {Vec3{-4, -4, 0}, Vec3{-4, -3, 0}}) {
        Node node{};
        node.prev = node.x = position;
        node.inv_mass = 0;
        neighboring.nodes.push_back(node);
    }
    neighboring.faces.push_back({{0, 6, 7}});
    contact.solve(neighboring, 0.01, 0);
    CHECK(neighboring.nodes[6].contact_age > 0);
    CHECK(neighboring.nodes[7].contact_age > 0);
    CHECK(neighboring.nodes[3].contact);
    CHECK(!neighboring.nodes[6].contact);
    CHECK(!neighboring.nodes[7].contact);

    // Compaction invalidates all former contact feature indices.
    neighboring.faces.resize(1);
    neighboring.nodes.resize(3);
    contact.solve(neighboring, 0.01, 1);
    CHECK(neighboring.nodes.size() == 3);
    CHECK(!neighboring.nodes[0].contact);
    CHECK(neighboring.nodes[0].contact_age > 0);

    // Index identities are deliberately reused by a different event/remesh.
    mesh = layers(0.3);
    contact.reset();
    contact.solve(mesh, 0.01, 1);
    CHECK(std::abs(mesh.nodes[3].x.z - 0.4) < 1e-12);

    // Eight moving layers: all vertices are interior witnesses of neighboring
    // grid faces. Packed contact must preserve ordering and physical clearance,
    // not merely stop one isolated vertex from crossing one pinned triangle.
    Topology packed;
    constexpr unsigned side = 5, layer_nodes = side * side;
    constexpr double thickness = 0.004;
    for (unsigned layer = 0; layer < 8; ++layer) {
        for (unsigned y = 0; y < side; ++y) {
            for (unsigned x = 0; x < side; ++x) {
                Node node{};
                node.uv = {static_cast<double>(layer * side + x), static_cast<double>(y)};
                node.prev = {x * 0.04, y * 0.04, layer * 0.0042};
                node.x = node.prev;
                node.x.z -= layer * 0.0003;
                node.x.x += layer * 0.0002;
                node.inv_mass = layer == 0 ? 0 : 1;
                packed.nodes.push_back(node);
            }
        }
        for (unsigned y = 0; y + 1 < side; ++y) {
            for (unsigned x = 0; x + 1 < side; ++x) {
                const auto a = layer * layer_nodes + y * side + x;
                packed.faces.push_back({{a, a + 1, a + side + 1}});
                packed.faces.push_back({{a, a + side + 1, a + side}});
            }
        }
    }
    double release_height = 0;
    for (unsigned tick = 0; tick < 24; ++tick) {
        if (tick > 0) {
            for (unsigned i = 0; i < packed.nodes.size(); ++i) {
                auto &node = packed.nodes[i];
                node.prev = node.x;
                const auto layer = i / layer_nodes;
                if (tick < 16) {
                    node.x.z -= layer * 0.00001;
                    node.x.x += layer * (tick < 8 ? 0.00001 : -0.00001);
                } else {
                    node.x.z += layer * 0.00005;
                }
            }
        }
        contact.solve(packed, thickness, 0.5);
        if (tick == 15) release_height = packed.nodes.back().x.z;
        for (unsigned i = layer_nodes; i < packed.nodes.size(); ++i) {
            CHECK(std::isfinite(packed.nodes[i].x.z));
            CHECK(packed.nodes[i].x.z - packed.nodes[i - layer_nodes].x.z >= thickness * 0.95);
        }
    }
    CHECK(packed.nodes.back().x.z > release_height + 0.001);

    // Elasticity can create a contact outside the original swept boxes.
    // Coupled passes must rediscover it and finish with contact, not elasticity.
    auto coupled = layers(-0.4);
    for (unsigned i = 0; i < 3; ++i) {
        auto node = coupled.nodes[i + 3];
        node.prev = {node.prev.x + 0.8, node.prev.y, 0.5};
        node.x = node.prev;
        coupled.nodes.push_back(node);
    }
    coupled.faces.push_back({{6, 7, 8}});
    struct ElasticContext {
        Topology *mesh;
        unsigned calls = 0;
    };
    ElasticContext context{&coupled};
    const auto elastic = [](void *opaque) {
        auto &state = *static_cast<ElasticContext *>(opaque);
        ++state.calls;
        for (unsigned i = 6; i < 9; ++i) state.mesh->nodes[i].x.z = -0.2;
    };
    contact.solve(coupled, 0.01, 0, elastic, &context);
    CHECK(context.calls == 2);
    for (unsigned i = 6; i < 9; ++i) {
        CHECK(coupled.nodes[i].x.z >= 0.00999);
        CHECK(coupled.nodes[i].prev.z == 0.5);
    }

    auto tip = converged_tip(false);
    contact.solve(tip, std::hypot(0.007, 0.007), 0);
    CHECK(!tip_pierces(tip, 4));
    CHECK(!tip_pierces(tip, 5));

    auto transported = converged_tip(true, {0.2, 0.3, 0.4});
    const std::array<Vec3, 3> wing_goal = {
        transported.nodes[6].x, transported.nodes[7].x, transported.nodes[8].x};
    Vec3 intended_center{}, actual_center{};
    double total_mass = 0;
    for (const auto &node : transported.nodes) {
        const double mass = 1 / node.inv_mass;
        intended_center.x += node.x.x * mass;
        intended_center.y += node.x.y * mass;
        intended_center.z += node.x.z * mass;
        total_mass += mass;
    }
    contact.solve(transported, std::hypot(0.007, 0.007), 0);
    CHECK(!tip_pierces(transported, 4));
    CHECK(!tip_pierces(transported, 5));
    for (const auto &node : transported.nodes) {
        const double mass = 1 / node.inv_mass;
        actual_center.x += node.x.x * mass;
        actual_center.y += node.x.y * mass;
        actual_center.z += node.x.z * mass;
    }
    CHECK(std::abs(actual_center.x - intended_center.x) / total_mass < 1e-12);
    CHECK(std::abs(actual_center.y - intended_center.y) / total_mass < 1e-12);
    CHECK(std::abs(actual_center.z - intended_center.z) / total_mass < 1e-12);
    for (unsigned i = 0; i < wing_goal.size(); ++i) {
        CHECK(std::abs(transported.nodes[i + 6].x.x - wing_goal[i].x) < 1e-12);
        CHECK(std::abs(transported.nodes[i + 6].x.y - wing_goal[i].y) < 1e-12);
        CHECK(std::abs(transported.nodes[i + 6].x.z - wing_goal[i].z) < 1e-12);
    }
    return g_failures != 0;
}
