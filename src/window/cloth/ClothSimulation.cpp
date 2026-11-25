#include "window/cloth/ClothSimulation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mdspan>
#include <new>
#include <numbers>
#include <optional>
#include <span>
#include <thread>

#include "perf/LiveProfiler.hpp"
#include "shared/Core.hpp"
#include "window/cloth/ClothPocket.hpp"
#include "window/cloth/ClothPrivate.hpp"
#include "window/cloth/ClothRecording.hpp"
#include "window/cloth/ClothSolver.hpp"
#include "window/cloth/ClothWorkers.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate::cloth_detail {
    namespace {
        constexpr double RADIUS = 0.002;
        constexpr float CLOTH_INV_MASS = 4.0F;
        // Air resists fabric across its face, damping the opening folds as the gathered header
        // spreads. Closes move their sheets in plane, so their gusts stay undamped.
        constexpr double OPEN_AIR_DRAG =
            4.0;  // per simulated second, on the velocity along the local normal

        cloth::Point to_point(Vec3 point) noexcept {
            return {static_cast<float>(point.x),
                static_cast<float>(point.y),
                static_cast<float>(point.z)};
        }
        std::size_t index(int column, int row) noexcept {
            return (static_cast<std::size_t>(row) * GRID) + column;
        }
        float cloth_inverse_mass(int row) noexcept {
            // A sewn hem carries extra inertia without turning the whole panel into a heavy rubber
            // membrane.
            return CLOTH_INV_MASS /
                   static_cast<float>(
                       1 + (2 * smoothstep(.92, 1, static_cast<double>(row) / SHEET_CELLS)));
        }
        cloth::Solver make_solver(const Run &run, const SheetGrid &rest) {
            std::array<cloth::Point, SHEET_GRID_VERTICES> positions, velocities;
            std::array<float, SHEET_GRID_VERTICES> inverse_masses;
            for (int j = 0; j < GRID; ++j) {
                const float INV_MASS = Run::pinned(j) ? 0 : cloth_inverse_mass(j);
                for (int i = 0; i < GRID; ++i) {
                    const auto VERTEX = index(i, j);
                    positions[VERTEX] = to_point(run.start[VERTEX]);
                    velocities[VERTEX] = to_point(run.velocity[VERTEX]);
                    inverse_masses[VERTEX] = INV_MASS;
                }
            }
            return cloth::Solver(rest, positions, velocities, inverse_masses);
        }

        ClothPocket make_pocket(const Run &run,
            std::span<const cloth::Point> positions = {},
            std::span<const cloth::Point> velocities = {}) noexcept {
            ClothPocket::Samples x, vx;
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = positions.empty() ? static_cast<float>(run.start[i].x) : positions[i].x;
                vx[i] = velocities.empty()
                            ? static_cast<float>(run.velocity[i].x * run.movement.springiness)
                            : velocities[i].x;
            }
            return ClothPocket{x, vx};
        }

        TerminalReason publish_movement(Run &run,
            double time,
            std::span<const cloth::Point> positions,
            std::span<const cloth::Point> velocities) {
            const unsigned HEAD = run.output_head.load(std::memory_order_relaxed);
            if (HEAD - run.output_tail.load(std::memory_order_acquire) == Run::OUTPUT_CAPACITY) {
                run.overflow.store(true, std::memory_order_release);
                return TerminalReason::QueueOverflow;
            }
            auto &snapshot = run.snapshots.at(HEAD % Run::OUTPUT_CAPACITY);
            snapshot.time = time;
            snapshot.rigid_velocity = run.snapshot_rigid_velocity;
            for (std::size_t i = 0; i < positions.size(); ++i) {
                const auto POSITION = positions[i];
                const auto VELOCITY = velocities[i];
                // Header vertices follow the rod directly; only the free panel uses the partial
                // inertial rebase.
                const auto &frame_velocity =
                    i < GRID ? run.snapshot_rigid_velocity : run.publish_velocity;
                if (!std::isfinite(POSITION.x) || !std::isfinite(POSITION.y) ||
                    !std::isfinite(POSITION.z) || !std::isfinite(VELOCITY.x) ||
                    !std::isfinite(VELOCITY.y) || !std::isfinite(VELOCITY.z)) {
                    return TerminalReason::RecordingFailure;
                }
                snapshot.values[i] = {
                    .position = {POSITION.x, POSITION.y, POSITION.z},
                    .velocity =
                        {
                            static_cast<float>(VELOCITY.x + frame_velocity.x),
                            static_cast<float>(VELOCITY.y + frame_velocity.y),
                            static_cast<float>(VELOCITY.z + frame_velocity.z),
                        },
                };
            }
            run.output_head.store(HEAD + 1, std::memory_order_release);
            run.recorded.fetch_add(1, std::memory_order_release);
            return TerminalReason::Running;
        }
    }  // namespace

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Splitting risks per-vertex
    // calls and publication ordering.
    TerminalReason simulate_movement(Run &run, double phase) {
        std::optional<perf::CpuScope> setup_profile{std::in_place,
            perf::Key{perf::Domain::Window, "cloth", "move"},
            perf::Phase::Worker};
        cloth::StepSettings settings{.speedCap = 20};
        const auto STOPPED = [&] { return run.stop.stop_requested(); };
        if (STOPPED()) {
            return TerminalReason::Cancelled;
        }
        const SheetGrid REST = rest_grid();
        std::array<double, GRID> pleats{};
        int column = 0;
        for (auto &pleat : pleats) {
            pleat = std::sin((8 * std::numbers::pi * column++ / SHEET_CELLS) + phase);
        }
        auto solver = make_solver(run, REST);
        const auto positions = solver.positions();
        const auto velocities = solver.velocities();
        std::optional<ClothPocket> pocket;
        if (run.movement.springiness > 0) {
            pocket.emplace(make_pocket(run));
        }
        if (const auto FIRST = publish_movement(run, -STEP_SECONDS, positions, velocities);
            FIRST != TerminalReason::Running) {
            return FIRST;
        }
        if (const auto SECOND = publish_movement(run, 0, positions, velocities);
            SECOND != TerminalReason::Running) {
            return SECOND;
        }
        Run::Target previous{.time = 0, .box = run.window};
        Box box = run.window;
        double time = 0;
        double last_motion = 0;
        double previous_rod_velocity = 0;
        setup_profile.reset();
        while (!STOPPED()) {
            if (s_pause.load(std::memory_order_acquire) &&
                run.recorded.load(std::memory_order_relaxed) >=
                    s_pause_after.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const unsigned TAIL = run.input_tail.load(std::memory_order_relaxed);
            if (TAIL == run.input_head.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const Run::Target TARGET = run.targets.at(TAIL % Run::INPUT_CAPACITY);
            const perf::CpuScope PROFILE{
                {perf::Domain::Window, "cloth", "move"}, perf::Phase::Worker};
            run.input_tail.store(TAIL + 1, std::memory_order_release);
            const ClothMovementParams &movement = TARGET.movement;
            settings.gravity = static_cast<float>(-GRAVITY * movement.gravity);
            if (!pocket && movement.springiness > 0) {
                pocket.emplace(make_pocket(run, positions, velocities));
            }
            while (time + STEP_SECONDS <= TARGET.time + 1e-10 && !STOPPED()) {
                const double NEXT_TIME = time + STEP_SECONDS;
                const double BLEND = std::clamp(
                    (NEXT_TIME - previous.time) / (TARGET.time - previous.time), 0.0, 1.0);
                const Box NEXT{
                    .x = previous.box.x + (BLEND * (TARGET.box.x - previous.box.x)),
                    .y = previous.box.y + (BLEND * (TARGET.box.y - previous.box.y)),
                    .w = previous.box.w + (BLEND * (TARGET.box.w - previous.box.w)),
                    .h = previous.box.h + (BLEND * (TARGET.box.h - previous.box.h)),
                };
                const double DELTA_X = NEXT.x - box.x;
                const double DELTA_Y = NEXT.y - box.y;
                run.snapshot_rigid_velocity = {
                    .x = DELTA_X / STEP_SECONDS / NEXT.w,
                    .y = -DELTA_Y / STEP_SECONDS / NEXT.h,
                    .z = 0,
                };
                const double GAIN = std::min(movement.strength, 1.5);
                run.publish_velocity = {
                    .x = (1 - GAIN) * DELTA_X / STEP_SECONDS / NEXT.w,
                    .y = -(1 - GAIN) * DELTA_Y / STEP_SECONDS / NEXT.h,
                    .z = 0,
                };
                const double SPEED =
                    std::hypot(DELTA_X, DELTA_Y) / STEP_SECONDS / std::min(NEXT.w, NEXT.h);
                if (SPEED > 1e-5 ||
                    std::abs(NEXT.w - box.w) + std::abs(NEXT.h - box.h) > 1e-5) {
                    last_motion = NEXT_TIME;
                }
                const double QUIET = NEXT_TIME - last_motion;
                const double SETTLE = smoothstep(0.05, 0.5, QUIET);
                const double INHERITED = std::exp(-6 * NEXT_TIME);
                const double ROD_VELOCITY = DELTA_X / STEP_SECONDS / NEXT.w;
                const double ACCELERATION =
                    GAIN * (ROD_VELOCITY - previous_rod_velocity) / STEP_SECONDS;
                previous_rod_velocity = ROD_VELOCITY;
                if (pocket &&
                    !pocket->update(ACCELERATION, ROD_VELOCITY, SETTLE, movement.springiness)) {
                    return TerminalReason::SolverFailure;
                }
                const double OLD_DEPTH = std::sqrt(box.w * box.h);
                const double NEW_DEPTH = std::sqrt(NEXT.w * NEXT.h);
                settings.damping = static_cast<float>(2 + (9 * SETTLE));
                const double GUST = std::sin(phase + (NEXT_TIME * 7));
                const auto *rest_position = REST.begin();
                const auto *start_position = run.start.begin();
                for (int j = 0; j < GRID; ++j) {
                    const double DOWN = static_cast<double>(j) / SHEET_CELLS;
                    const double RESPONSE = cloth_inverse_mass(j) / CLOTH_INV_MASS;
                    const double SPRING = 4 + (30 * (1 - DOWN) * (1 - DOWN)) + (120 * SETTLE);
                    const auto *pleat = pleats.begin();
                    for (int i = 0; i < GRID; ++i) {
                        const auto VERTEX = index(i, j);
                        auto &position = positions[VERTEX];
                        auto &velocity = velocities[VERTEX];
                        if (j != 0) {
                            position = {
                                static_cast<float>(
                                    (box.x + (box.w / 2) + (position.x * box.w) - NEXT.x -
                                        (NEXT.w / 2) + ((1 - GAIN) * DELTA_X)) /
                                    NEXT.w),
                                static_cast<float>(
                                    ((GAIN * DELTA_Y) + (position.y * box.h)) / NEXT.h),
                                static_cast<float>(position.z * OLD_DEPTH / NEW_DEPTH),
                            };
                        }
                        velocity = {
                            static_cast<float>(velocity.x * box.w / NEXT.w),
                            static_cast<float>(velocity.y * box.h / NEXT.h),
                            static_cast<float>(velocity.z * OLD_DEPTH / NEW_DEPTH),
                        };
                        const auto REST_POS = *rest_position++;
                        const Vec3 OLD_OFFSET = sub(*start_position++, REST_POS);
                        const auto DESIRED =
                            to_point(add(REST_POS, mul(OLD_OFFSET, INHERITED)));
                        if (Run::pinned(j)) {
                            const float POS_X = pocket ? pocket->position(i) : DESIRED.x;
                            velocity =
                                (cloth::Point{POS_X, 0, 0} - position) / cloth::Solver::kTick;
                        } else {
                            velocity += (DESIRED - position) *
                                        static_cast<float>(SPRING * RESPONSE * STEP_SECONDS);
                            const double DRIVE = std::min(SPEED, 3.0) * movement.strength;
                            // Alternating pressure excites long folds instead of inflating the
                            // entire sheet forward.
                            const double PRESSURE = *pleat *
                                                    (16 + (8 * movement.flutter * GUST)) *
                                                    (.3 + (.7 * DOWN));
                            velocity.z +=
                                static_cast<float>(RESPONSE * DRIVE * PRESSURE * STEP_SECONDS);
                        }
                        ++pleat;
                    }
                }
                solver.step(settings);
                // Preserve the solved pose and velocity together to retain upward folds.
                box = NEXT;
                time = NEXT_TIME;
                if (const auto PUBLISHED = publish_movement(run, time, positions, velocities);
                    PUBLISHED != TerminalReason::Running) {
                    return PUBLISHED;
                }
            }
            previous = TARGET;
        }
        return TerminalReason::Cancelled;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Splitting risks per-vertex
    // calls and neighbor update ordering.
    TerminalReason simulate(Run &run, const SimInput &input) {
        const perf::Key KEY{
            perf::Domain::Window, "cloth", run.kind == Transition::Close ? "close" : "open"};
        std::optional<perf::CpuScope> setup_profile{std::in_place, KEY, perf::Phase::Worker};
        const auto STOPPED = [&] { return run.stop.stop_requested(); };
        const auto WAIT_PUBLICATION = [&] {
            while (s_pause.load(std::memory_order_acquire) &&
                   run.recorded.load(std::memory_order_acquire) >=
                       s_pause_after.load(std::memory_order_relaxed) &&
                   !STOPPED()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return !STOPPED();
        };
        if (!WAIT_PUBLICATION()) {
            return TerminalReason::Cancelled;
        }
        try {
            allocate_recording(run);
        } catch (const std::bad_alloc &) {
            return TerminalReason::RecordingAllocationFailure;
        }
        if (setup_profile->session() != 0) {
            perf::resource_sample(KEY,
                sizeof(Run) + static_cast<std::uint64_t>(run.frames) * sizeof(Run::Frame),
                0,
                setup_profile->session());
        }
        if (STOPPED()) {
            return TerminalReason::Cancelled;
        }
        std::array<cloth::Obstacle, MAX_OBSTACLES> boxes;
        for (std::size_t i = 0; i < input.obstacles.size(); ++i) {
            const auto &obstacle = input.obstacles[i];
            boxes[i] = {
                to_point(
                    {obstacle.x + (obstacle.w / 2), obstacle.y + (obstacle.h / 2), -.485F}),
                to_point({obstacle.w / 2, obstacle.h / 2, .515F})};
        }
        cloth::StepSettings settings{
            .gravity = static_cast<float>(-GRAVITY),
            .damping = 1.5F,
            .speedCap = static_cast<float>(std::max(500.0,
                (1.5 * run.max_initial_speed) +
                    (8 * std::hypot(run.travel.x, run.travel.y) / run.length))),
            .boxes = std::span(boxes).first(input.obstacles.size()),
            // The initially flat sheet must not start embedded by its radius.
            .backing = {{0, 0, 1}, 1.F + static_cast<float>(-1 - (2 * RADIUS))},
            .radius = static_cast<float>(RADIUS),
            .friction = std::sqrt(0.3F * 0.2F),
        };
        const SheetGrid REST = rest_grid();
        auto solver = make_solver(run, REST);
        const auto positions = solver.positions();
        const auto velocities = solver.velocities();
        std::array<double, GRID> gust_columns{};
        if (input.flutter > 0) {
            int column = 0;
            for (auto &gust : gust_columns) {
                gust =
                    std::sin((8 * std::numbers::pi * column++ / SHEET_CELLS) + input.phase_a);
            }
        }
        if (STOPPED()) {
            return TerminalReason::Cancelled;
        }
        setup_profile.reset();
        if (run.kind == Transition::Open) {
            // A level, gathered sleeve compresses the first material rows. Relax that sewn
            // pocket on the worker before publishing frame zero, instead of displaying its
            // initial constraint correction.
            settings.damping = 8;
            for (int step = 0; step < 24; ++step) {
                const perf::CpuScope PROFILE{
                    {perf::Domain::Window, "cloth", "open"}, perf::Phase::Worker};
                if (!WAIT_PUBLICATION()) {
                    return TerminalReason::Cancelled;
                }
                solver.step(settings);
            }
            std::ranges::fill(velocities, cloth::Point{});
            settings.damping = 1.5F;
        }
        setup_profile.emplace(KEY, perf::Phase::Worker);
        std::optional<ClothPocket> pocket;
        if (run.kind == Transition::Close && run.movement.springiness > 0) {
            pocket.emplace(make_pocket(run));
        }
        if (!record(run, 0, positions, velocities)) {
            return TerminalReason::RecordingFailure;
        }
        const auto OPEN_AIR_DRAG_FACTOR =
            static_cast<float>(1 - std::exp(-OPEN_AIR_DRAG * STEP_SECONDS));
        setup_profile.reset();
        for (int step = 1; step < run.frames; ++step) {
            if (!WAIT_PUBLICATION()) {
                return TerminalReason::Cancelled;
            }
            const perf::CpuScope PROFILE{{perf::Domain::Window,
                                             "cloth",
                                             run.kind == Transition::Close ? "close" : "open"},
                perf::Phase::Worker};
            const double TIME = (step - 1) * STEP_SECONDS;
            if (pocket) {
                const double PROGRESS =
                    std::clamp((TIME + STEP_SECONDS) / run.length, 0.0, 1.0);
                const double ROD_VELOCITY = run.travel.x * 30 * PROGRESS * PROGRESS *
                                            (1 - PROGRESS) * (1 - PROGRESS) / run.length;
                if (!pocket->update(0, ROD_VELOCITY, 0, run.movement.springiness)) {
                    return TerminalReason::SolverFailure;
                }
            }
            if (run.kind == Transition::Open) {
                settings.damping =
                    static_cast<float>(1.5 + (8 * smoothstep(0.65, 0.95, TIME / run.length)));
            }
            const double HEADER_SPREAD =
                run.kind == Transition::Open
                    ? smoothstep(0, .72, (TIME + STEP_SECONDS) / run.length)
                    : 0;
            const std::mdspan<const cloth::Point, std::extents<std::size_t, GRID, GRID>>
                GRID_POS{positions.data()};
            const auto *step_start = run.start.begin();
            const auto *step_rest = REST.begin();
            for (int j = 0; j < GRID; ++j) {
                const double ROW_GUST =
                    input.flutter > 0
                        ? std::sin((input.freq_a * TIME) +
                                   (.8 * static_cast<double>(j) / SHEET_CELLS) + input.phase_b)
                        : 0;
                const auto *gust = gust_columns.begin();
                for (int i = 0; i < GRID; ++i) {
                    const auto VERTEX = index(i, j);
                    auto &velocity = velocities[VERTEX];
                    const Vec3 &start = *step_start++;
                    const Vec3 &rest = *step_rest++;
                    if (Run::pinned(j)) {
                        Vec3 target = start;
                        if (run.kind == Transition::Open) {
                            target = lerp(target, rest, HEADER_SPREAD);
                        }
                        if (run.kind == Transition::Close) {
                            const double CLOSE_PROGRESS =
                                std::clamp((TIME + STEP_SECONDS) / run.length, 0.0, 1.0);
                            const double DISTANCE =
                                CLOSE_PROGRESS * CLOSE_PROGRESS * CLOSE_PROGRESS *
                                (10 + (CLOSE_PROGRESS * (-15 + (6 * CLOSE_PROGRESS))));
                            const double INHERITED_TIME = (TIME + STEP_SECONDS) *
                                                          (1 - CLOSE_PROGRESS) *
                                                          (1 - CLOSE_PROGRESS);
                            target.x = (pocket ? pocket->position(i) : target.x) +
                                       (run.travel.x * DISTANCE);
                            target.y += (run.travel.y * DISTANCE) +
                                        (run.velocity.front().y * INHERITED_TIME);
                            target.z = 0;
                        }
                        velocity =
                            (to_point(target) - positions[VERTEX]) / cloth::Solver::kTick;
                    } else if (run.kind == Transition::Open || input.flutter > 0) {
                        const auto VERTEX_AT = [&](int column, int row) {
                            return GRID_POS[row, column];
                        };
                        const auto TANGENT_X = VERTEX_AT(std::min(i + 1, SHEET_CELLS), j) -
                                               VERTEX_AT(std::max(i - 1, 0), j);
                        const auto TANGENT_Y = VERTEX_AT(i, std::min(j + 1, SHEET_CELLS)) -
                                               VERTEX_AT(i, std::max(j - 1, 0));
                        const auto CROSS = cloth::cross(TANGENT_X, TANGENT_Y);
                        const float LENGTH_SQUARED = cloth::lengthSquared(CROSS);
                        const auto NORMAL = LENGTH_SQUARED > 0
                                                ? CROSS / std::sqrt(LENGTH_SQUARED)
                                                : cloth::Point{0, 0, 1};
                        if (run.kind == Transition::Open) {
                            velocity -=
                                NORMAL * (cloth::dot(velocity, NORMAL) * OPEN_AIR_DRAG_FACTOR);
                        }
                        if (input.flutter > 0) {
                            const double WAVE = *gust * ROW_GUST;
                            velocity += NORMAL *
                                        static_cast<float>(
                                            5 * input.flutter * WAVE * STEP_SECONDS * NORMAL.z *
                                            (cloth_inverse_mass(j) / CLOTH_INV_MASS));
                        }
                    }
                    ++gust;
                }
            }
            solver.step(settings);
            if (!record(run, step, positions, velocities)) {
                return TerminalReason::RecordingFailure;
            }
        }
        return TerminalReason::Completed;
    }
}  // namespace hyprimate::cloth_detail
