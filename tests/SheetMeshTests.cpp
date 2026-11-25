#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "window/liquid/LiquidPose.hpp"
#include "window/shared/SheetMesh.hpp"

namespace {
    bool near(float first, double second) {
        return std::abs(static_cast<double>(first) - second) < 1e-6;
    }

    void check_vertex(const hyprimate::animation::MeshVertex &vertex,
        double pos_x,
        double pos_y,
        double tex_u,
        double tex_v) {
        CHECK(near(vertex.x, pos_x));
        CHECK(near(vertex.y, pos_y));
        CHECK(near(vertex.u, tex_u));
        CHECK(near(vertex.v, tex_v));
    }
}  // namespace

int main() {
    using namespace hyprimate;
    animation::MeshBuffers<animation::MeshVertex> buffers;
    const animation::MeshTarget<animation::MeshVertex> TARGET{
        .source = {.x = 10, .y = 20, .w = 112, .h = 56},
        .output = {.x = 0, .y = 0, .w = 224, .h = 112},
        .image_origin = {.x = 0, .y = 0},
        .image_size = {.x = 224, .y = 112},
        .scale = 1,
        .buffers = &buffers};
    SheetFrame frame;
    flat_sheet(frame);
    const auto FIRST_UNSHADED = fill_sheet_mesh(frame, TARGET, false, 1.F);
    CHECK(FIRST_UNSHADED.shade == nullptr);
    CHECK(buffers.shade.empty());
    CHECK(buffers.shade.capacity() == 0);
    std::swap(frame.order[0], frame.order[1]);
    frame.shade[1] = {-1.F, 0.F, 1.F};
    const auto SHEET = fill_sheet_mesh(frame, TARGET, true, 0.75F);
    CHECK(SHEET.window == &buffers.window);
    CHECK(SHEET.shade == &buffers.shade);
    CHECK(SHEET.opacity == 0.75F);
    CHECK(buffers.window.size() == SHEET_VERTEX_COUNT);
    CHECK(buffers.shade.size() == SHEET_VERTEX_COUNT);
    // Triangle 1 precedes triangle 0; its corners are (1,0), (0,1), (1,1).
    check_vertex(buffers.window[0], 12.0 / 224, 20.0 / 112, 12.0 / 224, 20.0 / 112);
    check_vertex(buffers.window[1], 10.0 / 224, 21.0 / 112, 10.0 / 224, 21.0 / 112);
    check_vertex(buffers.window[2], 12.0 / 224, 21.0 / 112, 12.0 / 224, 21.0 / 112);
    check_vertex(buffers.window[3], 10.0 / 224, 20.0 / 112, 10.0 / 224, 20.0 / 112);
    CHECK(near(buffers.shade[0].u, 1.5 / 256));
    CHECK(near(buffers.shade[1].u, 128.5 / 256));
    CHECK(near(buffers.shade[2].u, 255.5 / 256));
    const auto *window_data = buffers.window.data();
    const auto *shade_data = buffers.shade.data();
    const auto SHADE_BEFORE = buffers.shade[0];
    frame.shade[1][0] = 1.F;
    const auto UNSHADED = fill_sheet_mesh(frame, TARGET, false, 1.F);
    CHECK(UNSHADED.shade == nullptr);
    CHECK(buffers.window.data() == window_data);
    CHECK(buffers.shade.data() == shade_data);
    check_vertex(buffers.shade[0], SHADE_BEFORE.x, SHADE_BEFORE.y, SHADE_BEFORE.u, SHADE_BEFORE.v);

    SheetGrid world{};
    for (std::size_t vertex = 0; vertex < world.size(); ++vertex) {
        world[vertex].z = static_cast<double>(static_cast<int>((vertex * 17) % 11) - 5);
    }
    auto expected = frame.order;
    std::sort(expected.begin(), expected.end(), [&](auto first, auto second) {
        const auto depth = [&](auto triangle) {
            const auto &corners = sheet_triangle(triangle);
            return (world[corners[0]].z + world[corners[1]].z + world[corners[2]].z) / 3.0;
        };
        const double a = depth(first), b = depth(second);
        return a < b || (!(b < a) && first < second);
    });
    std::reverse(frame.order.begin(), frame.order.end());
    project_sheet(world, {112, 56}, 0, frame);
    CHECK(frame.order == expected);
    CHECK(!frame.lit);
    CHECK(std::all_of(frame.shade.begin(), frame.shade.end(), [](const auto &shade) {
        return shade[0] == 0 && shade[1] == 0 && shade[2] == 0;
    }));

    const LiquidMesh LIQUID_MESH;
    const EdgeTransition TRANSITION;
    LiquidPoseGrid grid;
    const LiquidPose POSE{.mesh = LIQUID_MESH, .transition = TRANSITION};
    const auto LIQUID = fill_liquid_mesh(POSE, TARGET, grid, std::nullopt, {}, std::nullopt);
    CHECK(LIQUID.window == &buffers.window);
    CHECK(LIQUID.under == nullptr);
    CHECK(!LIQUID.clip);
    CHECK(buffers.window.size() == LIQUID_RENDER_VERTEX_COUNT);
    check_vertex(buffers.window[0], 10.0 / 224, 20.0 / 112, 10.0 / 224, 20.0 / 112);
    check_vertex(buffers.window[1], 10.0 / 224, 21.75 / 112, 10.0 / 224, 21.75 / 112);
    check_vertex(buffers.window[2], 13.5 / 224, 20.0 / 112, 13.5 / 224, 20.0 / 112);
    window_data = buffers.window.data();
    fill_liquid_mesh(POSE, TARGET, grid, std::nullopt, {}, std::nullopt);
    CHECK(buffers.window.data() == window_data);
    const Box UNDER_BOX{.x = 5, .y = 6, .w = 70, .h = 80};
    const Box CLIP{.x = 1, .y = 2, .w = 3, .h = 4};
    const auto WITH_UNDER = fill_liquid_mesh(
        POSE, TARGET, grid, UNDER_BOX, {.r = 0.2F, .g = 0.3F, .b = 0.4F, .a = 0.5F}, CLIP);
    CHECK(WITH_UNDER.under == &buffers.under);
    CHECK(buffers.under.size() == LIQUID_MENISCUS_VERTEX_COUNT);
    CHECK(WITH_UNDER.under_output.x == 5 && WITH_UNDER.under_output.h == 80);
    CHECK(WITH_UNDER.under_color.r == 0.2F && WITH_UNDER.under_color.a == 0.5F);
    CHECK(WITH_UNDER.clip && WITH_UNDER.clip->x == 1 && WITH_UNDER.clip->h == 4);
    const auto *under_data = buffers.under.data();
    fill_liquid_mesh(POSE, TARGET, grid, UNDER_BOX, {}, CLIP);
    CHECK(buffers.under.data() == under_data);

    // An indexed target must be the expanded triangle list with its duplicates folded away:
    // gathering the unique vertices through the indices reproduces the expanded mesh exactly.
    {
        const auto SAME_VERTICES = [](const auto &expanded,
                                       const auto &unique,
                                       const std::vector<std::uint16_t> &indices) {
            if (expanded.size() != indices.size()) return false;
            for (std::size_t i = 0; i < indices.size(); ++i) {
                if (indices[i] >= unique.size()) return false;
                const auto &a = expanded[i];
                const auto &b = unique[indices[i]];
                if (a.x != b.x || a.y != b.y || a.u != b.u || a.v != b.v) return false;
            }
            return true;
        };
        animation::MeshBuffers<animation::MeshVertex> flat_buffers, indexed_buffers;
        auto flat_target = TARGET;
        flat_target.buffers = &flat_buffers;
        auto indexed_target = TARGET;
        indexed_target.buffers = &indexed_buffers;
        indexed_target.indexed = true;

        const auto FLAT = fill_sheet_mesh(frame, flat_target, true, 1.F);
        const auto INDEXED = fill_sheet_mesh(frame, indexed_target, true, 1.F);
        CHECK(FLAT.window_indices == nullptr);
        CHECK(INDEXED.window_indices == &indexed_buffers.window_indices);
        CHECK(indexed_buffers.window.size() == SHEET_GRID_VERTICES);
        CHECK(indexed_buffers.window_indices.size() == SHEET_VERTEX_COUNT);
        CHECK(SAME_VERTICES(flat_buffers.window, indexed_buffers.window, indexed_buffers.window_indices));
        // The shade mesh is not shared per vertex and stays expanded in both forms.
        CHECK(indexed_buffers.shade.size() == SHEET_VERTEX_COUNT);
        for (std::size_t i = 0; i < SHEET_VERTEX_COUNT; ++i) {
            CHECK(flat_buffers.shade[i].u == indexed_buffers.shade[i].u);
        }

        animation::MeshBuffers<animation::MeshVertex> liquid_flat, liquid_indexed;
        auto liquid_flat_target = TARGET;
        liquid_flat_target.buffers = &liquid_flat;
        auto liquid_indexed_target = TARGET;
        liquid_indexed_target.buffers = &liquid_indexed;
        liquid_indexed_target.indexed = true;
        const auto LIQUID_FLAT =
            fill_liquid_mesh(POSE, liquid_flat_target, grid, std::nullopt, {}, std::nullopt);
        const auto LIQUID_INDEXED =
            fill_liquid_mesh(POSE, liquid_indexed_target, grid, std::nullopt, {}, std::nullopt);
        CHECK(LIQUID_FLAT.window_indices == nullptr);
        CHECK(LIQUID_INDEXED.window_indices == &liquid_indexed.window_indices);
        CHECK(liquid_indexed.window.size() ==
              static_cast<std::size_t>(LIQUID_POSE_SAMPLES) * LIQUID_POSE_SAMPLES);
        CHECK(SAME_VERTICES(liquid_flat.window, liquid_indexed.window, liquid_indexed.window_indices));
        // The lattice indices are built once and survive refills.
        const auto *index_data = liquid_indexed.window_indices.data();
        fill_liquid_mesh(POSE, liquid_indexed_target, grid, std::nullopt, {}, std::nullopt);
        CHECK(liquid_indexed.window_indices.data() == index_data);

        // Reuse one buffer across representation and style changes, including retained indices.
        animation::MeshBuffers<animation::MeshVertex> reused;
        auto switching_target = TARGET;
        switching_target.buffers = &reused;
        const auto CHECK_SWITCHES = [&](auto fill, std::size_t expected_indices) {
            std::vector<animation::MeshVertex> expanded;
            for (bool indexed : {false, true, false, true}) {
                switching_target.indexed = indexed;
                const auto mesh = fill();
                CHECK(mesh.window == &reused.window);
                if (!indexed) {
                    CHECK(mesh.window_indices == nullptr);
                    expanded = reused.window;
                } else {
                    CHECK(mesh.window_indices == &reused.window_indices);
                    CHECK(reused.window_indices.size() == expected_indices);
                    CHECK(reused.window_indices.size() % 3 == 0);
                    CHECK(std::all_of(reused.window_indices.begin(), reused.window_indices.end(),
                        [&](std::uint16_t index) { return index < reused.window.size(); }));
                    CHECK(SAME_VERTICES(expanded, reused.window, reused.window_indices));
                }
            }
        };
        CHECK_SWITCHES([&] { return fill_sheet_mesh(frame, switching_target, true, 1.F); },
            SHEET_VERTEX_COUNT);
        CHECK_SWITCHES([&] {
            return fill_liquid_mesh(POSE, switching_target, grid, std::nullopt, {}, std::nullopt);
        }, LIQUID_RENDER_VERTEX_COUNT);
        CHECK_SWITCHES([&] { return fill_sheet_mesh(frame, switching_target, false, 1.F); },
            SHEET_VERTEX_COUNT);
        CHECK_SWITCHES([&] {
            return fill_liquid_mesh(POSE, switching_target, grid, std::nullopt, {}, std::nullopt);
        }, LIQUID_RENDER_VERTEX_COUNT);
    }
    return g_failures == 0 ? 0 : 1;
}
