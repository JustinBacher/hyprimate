#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>

#include "Check.hpp"
#include "window/cloth/ClothPocket.hpp"
#include "window/cloth/ClothSolver.hpp"
#include "window/shared/SheetFrame.hpp"

namespace {
    using namespace hyprimate;
    using namespace hyprimate::cloth;

    constexpr int GRID = SHEET_CELLS + 1;

    constexpr std::size_t index(int column, int row) {
        return (static_cast<std::size_t>(row) * static_cast<std::size_t>(GRID)) +
               static_cast<std::size_t>(column);
    }

    SheetGrid rest_grid() {
        SheetGrid rest{};
        for (int row = 0; row < GRID; ++row) {
            for (int column = 0; column < GRID; ++column) {
                rest[index(column, row)] = {
                    (static_cast<float>(column) / SHEET_CELLS) - .5F,
                    .5F - (static_cast<float>(row) / SHEET_CELLS),
                    0.0,
                };
            }
        }
        return rest;
    }

    std::array<Point, SHEET_GRID_VERTICES> point_grid(const SheetGrid &rest) {
        std::array<Point, SHEET_GRID_VERTICES> points{};
        for (std::size_t vertex = 0; vertex < points.size(); ++vertex) {
            points[vertex] = {static_cast<float>(rest[vertex].x),
                static_cast<float>(rest[vertex].y),
                static_cast<float>(rest[vertex].z)};
        }
        return points;
    }

    bool finite(Point point) {
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    }

    void pinned_motion_is_exact_and_unaffected_by_dynamic_settings() {
        const auto REST = rest_grid();
        auto positions = point_grid(REST);
        std::array<Point, SHEET_GRID_VERTICES> velocities{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        constexpr auto PIN = index(GRID / 2, 0);
        positions[PIN] = {.12F, .34F, -.08F};
        velocities[PIN] = {.75F, -1.25F, .5F};
        Solver solver(REST, positions, velocities, inverse_masses);
        const StepSettings SETTINGS{
            .gravity = 30.0F,
            .damping = 15.0F,
            .speedCap = .01F,
            .backing = {{0.0F, 0.0F, 1.0F}, 10.0F},
            .radius = .5F,
            .friction = 1.0F,
        };

        for (int tick = 1; tick <= 120; ++tick) {
            solver.step(SETTINGS);
            const float TIME = static_cast<float>(tick) * Solver::kTick;
            const auto ACTUAL = solver.positions()[PIN];
            const auto VELOCITY = solver.velocities()[PIN];
            CHECK(std::abs(ACTUAL.x - (positions[PIN].x + (velocities[PIN].x * TIME))) < 2e-5F);
            CHECK(std::abs(ACTUAL.y - (positions[PIN].y + (velocities[PIN].y * TIME))) < 2e-5F);
            CHECK(std::abs(ACTUAL.z - (positions[PIN].z + (velocities[PIN].z * TIME))) < 2e-5F);
            CHECK(VELOCITY.x == velocities[PIN].x);
            CHECK(VELOCITY.y == velocities[PIN].y);
            CHECK(VELOCITY.z == velocities[PIN].z);
        }
    }

    void deformation_stays_finite_with_bounded_stretch() {
        const auto REST = rest_grid();
        auto positions = point_grid(REST);
        std::array<Point, SHEET_GRID_VERTICES> velocities{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        inverse_masses.fill(1.0F);
        for (int column = 0; column < GRID; ++column) {
            inverse_masses[index(column, 0)] = 0.0F;
        }
        constexpr auto KICK = index(GRID / 2, GRID / 2);
        velocities[KICK].z = 1.0F;
        Solver solver(REST, positions, velocities, inverse_masses);
        const StepSettings SETTINGS{.gravity = -1.5F, .damping = .25F, .speedCap = 3.0F};

        for (int tick = 0; tick < 120; ++tick) {
            solver.step(SETTINGS);
        }

        float maximum_edge_ratio = 0.0F;
        const auto MEASURE_EDGE = [&](std::size_t first_vertex, std::size_t second_vertex) {
            const auto DELTA = solver.positions()[second_vertex] - solver.positions()[first_vertex];
            const float DISTANCE = std::sqrt(lengthSquared(DELTA));
            const Point REST_DELTA{static_cast<float>(REST[second_vertex].x - REST[first_vertex].x),
                static_cast<float>(REST[second_vertex].y - REST[first_vertex].y),
                static_cast<float>(REST[second_vertex].z - REST[first_vertex].z)};
            const float REST_DISTANCE = std::sqrt(lengthSquared(REST_DELTA));
            maximum_edge_ratio = std::max(maximum_edge_ratio, DISTANCE / REST_DISTANCE);
        };
        for (int row = 0; row < GRID; ++row) {
            for (int column = 0; column < GRID; ++column) {
                const auto VERTEX = index(column, row);
                CHECK(finite(solver.positions()[VERTEX]));
                CHECK(finite(solver.velocities()[VERTEX]));
                if (column + 1 < GRID) {
                    MEASURE_EDGE(VERTEX, index(column + 1, row));
                }
                if (row + 1 < GRID) {
                    MEASURE_EDGE(VERTEX, index(column, row + 1));
                }
            }
        }
        CHECK(maximum_edge_ratio < 2.0F);
        CHECK(std::abs(solver.positions()[KICK].z) < .75F);
    }

    void a_rigid_fold_relaxes_from_bending() {
        const auto REST = rest_grid();
        auto positions = point_grid(REST);
        const std::array<Point, SHEET_GRID_VERTICES> VELOCITIES{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        inverse_masses.fill(1.0F);
        // A right-angle fold preserves every rest length: only the hinges have
        // a meaningful correction on the first substep.
        for (int row = GRID / 2 + 1; row < GRID; ++row) {
            for (int column = 0; column < GRID; ++column) {
                auto &point = positions[index(column, row)];
                point.z = -point.y;
                point.y = 0;
            }
        }
        Solver solver(REST, positions, VELOCITIES, inverse_masses);
        solver.step({});
        constexpr auto FOLD = index(GRID / 2, GRID / 2);
        CHECK(lengthSquared(solver.positions()[FOLD] - positions[FOLD]) > 1e-8F);
    }

    void swept_box_contact_prevents_a_fast_vertex_from_tunneling() {
        const auto REST = rest_grid();
        auto positions = point_grid(REST);
        std::array<Point, SHEET_GRID_VERTICES> velocities{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        inverse_masses.fill(1.0F);
        for (auto &position : positions) {
            position.x -= .65F;
        }
        for (auto &velocity : velocities) {
            velocity.x = 60.0F;
        }
        const hyprimate::cloth::Obstacle OBSTACLE{{0, 0, 0}, {.05F, 2.0F, 2.0F}};
        Solver solver(REST, positions, velocities, inverse_masses);
        const StepSettings SETTINGS{
            .damping = 0.0F, .boxes = std::span(&OBSTACLE, 1), .radius = .01F, .friction = 0.0F};

        solver.step(SETTINGS);
        solver.step(SETTINGS);
        float maximum_x = -1e9F;
        for (const auto &position : solver.positions()) {
            CHECK(finite(position));
            maximum_x = std::max(maximum_x, position.x);
        }
        CHECK(solver.positions()[index(GRID / 2, GRID / 2)].x < .1F);
        CHECK(maximum_x < .2F);
    }

    void a_thin_box_cannot_be_crossed_in_two_substeps() {
        const auto REST = rest_grid();
        auto positions = point_grid(REST);
        std::array<Point, SHEET_GRID_VERTICES> velocities{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        inverse_masses.fill(1.0F);
        for (auto &point : positions) {
            point.x += .001F;
        }
        for (auto &velocity : velocities) {
            velocity.x = 9.6F;
        }
        // The current material bounds do not admit this obstacle to the first tick's
        // plane cache. The leading vertex enters it before exiting on the next substep.
        constexpr hyprimate::cloth::Obstacle THIN_BOX{{.509F, 0, 0}, {.005F, 2, 2}};
        Solver solver(REST, positions, velocities, inverse_masses);
        solver.step({.boxes = std::span(&THIN_BOX, 1), .radius = .002F});
        CHECK(solver.positions()[index(GRID - 1, GRID / 2)].x <= .502001F);
    }

    void adjacent_obstacles_do_not_trap_the_sheet_at_the_seam() {
        const auto REST = rest_grid();
        const auto POSITIONS = point_grid(REST);
        const std::array<Point, SHEET_GRID_VERTICES> VELOCITIES{};
        std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
        inverse_masses.fill(1.0F);
        constexpr std::array<hyprimate::cloth::Obstacle, 2> OBSTACLES{{
            {{-1, 0, -.485F}, {1, 2, .515F}},
            {{1, 0, -.485F}, {1, 2, .515F}},
        }};
        Solver solver(REST, POSITIONS, VELOCITIES, inverse_masses);
        solver.step({.boxes = OBSTACLES, .backing = {{0, 0, 1}, -.004F}, .radius = .002F});
        constexpr auto SEAM = index(GRID / 2, GRID / 2);
        CHECK(solver.positions()[SEAM].z >= .032F - 1e-6F);
    }

    void pocket_spacing_survives_a_direction_reversal() {
        ClothPocket::Samples positions{};
        ClothPocket::Samples velocities{};
        for (int column = 0; column < GRID; ++column) {
            positions[static_cast<std::size_t>(column)] =
                (static_cast<float>(column) / SHEET_CELLS) - .5F;
            velocities[static_cast<std::size_t>(column)] = 4.0F;
        }
        ClothPocket pocket(positions, velocities);
        for (int tick = 0; tick < 36; ++tick) {
            CHECK(pocket.update(0.0, .05, 0.0, 1.0));
        }
        for (int tick = 0; tick < 48; ++tick) {
            CHECK(pocket.update(0.0, -.05, 0.0, 1.0));
        }
        for (int column = 0; column < GRID; ++column) {
            const float POSITION = pocket.position(column);
            CHECK(std::isfinite(POSITION));
            CHECK(POSITION >= -.498F && POSITION <= .498F);
            if (column + 1 < GRID) {
                const float SPACING = pocket.position(column + 1) - POSITION;
                CHECK(SPACING >= .0039F);
                CHECK(SPACING <= 1.005F);
            }
        }
    }

    std::size_t g_allocations = 0;
    bool g_count_allocations = false;

    void *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        if (g_count_allocations) {
            ++g_allocations;
        }
        void *pointer = nullptr;
        if (alignment <= alignof(std::max_align_t)) {
            pointer = std::malloc(size == 0 ? 1 : size);
        } else if (posix_memalign(&pointer, alignment, size == 0 ? alignment : size) != 0) {
            pointer = nullptr;
        }
        if (pointer == nullptr) {
            throw std::bad_alloc{};
        }
        return pointer;
    }

    void deallocate(void *pointer) noexcept {
        std::free(pointer);
    }
}  // namespace

void *operator new(std::size_t size) {
    return allocate(size);
}
void *operator new[](std::size_t size) {
    return allocate(size);
}
void *operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void *pointer) noexcept {
    deallocate(pointer);
}
void operator delete[](void *pointer) noexcept {
    deallocate(pointer);
}
void operator delete(void *pointer, std::size_t) noexcept {
    deallocate(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept {
    deallocate(pointer);
}
void operator delete(void *pointer, std::align_val_t) noexcept {
    deallocate(pointer);
}
void operator delete[](void *pointer, std::align_val_t) noexcept {
    deallocate(pointer);
}
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    deallocate(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    deallocate(pointer);
}

int main() {
    pinned_motion_is_exact_and_unaffected_by_dynamic_settings();
    deformation_stays_finite_with_bounded_stretch();
    a_rigid_fold_relaxes_from_bending();
    swept_box_contact_prevents_a_fast_vertex_from_tunneling();
    a_thin_box_cannot_be_crossed_in_two_substeps();
    adjacent_obstacles_do_not_trap_the_sheet_at_the_seam();
    pocket_spacing_survives_a_direction_reversal();

    const auto REST = rest_grid();
    auto positions = point_grid(REST);
    std::array<Point, SHEET_GRID_VERTICES> velocities{};
    std::array<float, SHEET_GRID_VERTICES> inverse_masses{};
    inverse_masses.fill(1.0F);
    Solver solver(REST, positions, velocities, inverse_masses);
    const StepSettings SETTINGS{.gravity = -1.0F, .damping = .1F};
    g_allocations = 0;
    g_count_allocations = true;
    for (int tick = 0; tick < 8; ++tick) {
        solver.step(SETTINGS);
    }
    g_count_allocations = false;
    CHECK(g_allocations == 0);

    if (g_failures != 0) {
        return 1;
    }
    std::puts("cloth solver tests passed");
    return 0;
}
