#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <vector>

#include "Check.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {
    using namespace hyprimate;
    constexpr Box WINDOW{.x = 100, .y = 50, .w = 1000, .h = 700};
    constexpr Box SCREEN{.x = -300, .y = -200, .w = 1920, .h = 1200};
    constexpr int GRID = PAPER_CELLS + 1;
    ClothParams params() {
        return {.duration = 0.8, .flutter = 0.5, .shading = 0.7, .variation = 0.5};
    }
    struct Pose {
        SheetGrid p{}, v{};
    };
    Pose pose(const ClothFlutter &cloth) {
        Pose result;
        cloth.sample_vertices(result.p, result.v);
        const auto FINITE = [](const Vec3 &vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
        };
        CHECK(std::ranges::all_of(result.p, FINITE));
        CHECK(std::ranges::all_of(result.v, FINITE));
        return result;
    }
    double delta(const SheetGrid &first, const SheetGrid &second) {
        double max_delta = 0;
        const auto *other = second.begin();
        for (const auto &vertex : first) {
            const auto &other_vertex = *other++;
            max_delta = std::max({
                max_delta,
                std::abs(vertex.x - other_vertex.x),
                std::abs(vertex.y - other_vertex.y),
                std::abs(vertex.z - other_vertex.z),
            });
        }
        return max_delta;
    }
    void advance(ClothFlutter &cloth, double seconds, double step = 1.0 / 144) {
        while (seconds > 1e-12 && cloth.active()) {
            const double STEP_SECONDS = std::min(step, seconds);
            cloth.advance(STEP_SECONDS);
            seconds -= STEP_SECONDS;
        }
    }
    ClothFlutter open(ClothParams options = params(), std::uint64_t seed = 7) {
        ClothFlutter cloth;
        cloth.begin(Transition::Open, options, seed, WINDOW, {});
        CHECK(cloth.wait_for_recording());
        return cloth;
    }
    ClothFlutter close(Edge edge, ClothParams options = params()) {
        ClothFlutter cloth;
        cloth.begin(Transition::Close,
            options,
            3,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = edge, .screen = SCREEN});
        CHECK(cloth.wait_for_recording());
        return cloth;
    }
    void needs_physics() {
        ClothFlutter cloth;
        cloth.begin(Transition::Open, params(), 1, WINDOW, {});
        CHECK(!cloth.active());
    }
    void opening_settles_flat() {
        auto material = params();
        material.flutter = 0;
        auto cloth = open(material);
        CHECK(cloth.opacity() == 0);
        advance(cloth, .85);
        CHECK(!cloth.active() && !cloth.closed() && cloth.opacity() == 1);
        hyprimate::SheetFrame frame;
        cloth.frame({.x = WINDOW.w, .y = WINDOW.h}, frame);
        std::size_t vertex = 0;
        for (const auto &position : frame.position) {
            const auto TEX_COORD = hyprimate::sheet_uv(vertex++);
            CHECK(position.x == TEX_COORD.x - 0.5 && position.y == TEX_COORD.y - 0.5);
        }
        for (const auto &shade : frame.shade) {
            for (const float SHADE : shade) {
                CHECK(SHADE == 0.0F);
            }
        }
    }
    // Gathering a curtain makes folds by spending its width in depth, not by stretching or
    // shrinking its material.
    void curtain_drape() {
        auto material = params();
        material.flutter = material.variation = 0;
        const auto CLOTH = open(material);
        const auto START = pose(CLOTH);
        double contour = 0;
        double near = 1e9;
        double far = -1e9;
        constexpr int ROW = PAPER_CELLS / 2;
        const auto ROW_POINTS =
            std::span(START.p).subspan(static_cast<std::size_t>(ROW) * GRID, GRID);
        auto previous_vertex = ROW_POINTS.begin();
        int vertex_index = 0;
        for (const auto &vertex : ROW_POINTS) {
            near = std::min(near, vertex.z);
            far = std::max(far, vertex.z);
            if (vertex_index != 0) {
                const auto &previous = *previous_vertex++;
                contour +=
                    std::hypot(vertex.x - previous.x, vertex.y - previous.y, vertex.z - previous.z);
            }
            ++vertex_index;
        }
        CHECK(far - near > .03);
        CHECK(std::get<(std::size_t(ROW) * GRID) + PAPER_CELLS>(START.p).x -
                  std::get<std::size_t(ROW) * GRID>(START.p).x <
              .95);
        CHECK(std::abs(contour - 1) < .01);
        // It hangs predominantly downward, rather than presenting one flat horizontal flap.
        CHECK(std::get<PAPER_CELLS / 2>(START.p).y -
                  std::get<(std::size_t(PAPER_CELLS) * GRID) + (PAPER_CELLS / 2)>(START.p).y >
              .65);
    }

    // Folds must spread and settle without a one-frame jolt at the free edge or weighted hem.
    void soft_landing() {
        const double LENGTH_SCALE = std::sqrt(WINDOW.w * WINDOW.h);
        for (const double FLUTTER : {0.0, 0.5}) {
            auto material = params();
            material.flutter = FLUTTER;
            auto cloth = open(material);
            std::vector<SheetGrid> frames;
            while (cloth.active()) {
                cloth.advance(1.0 / 120);
                frames.push_back(pose(cloth).p);
            }
            double worst = 0;
            for (std::size_t frame_index = 2; frame_index < frames.size(); ++frame_index) {
                const auto CURRENT_FRAME = std::next(frames.begin(),
                    static_cast<std::vector<SheetGrid>::difference_type>(frame_index));
                const auto &earlier = *std::prev(CURRENT_FRAME, 2);
                const auto &previous = *std::prev(CURRENT_FRAME);
                const auto *earlier_vertex = earlier.begin();
                const auto *previous_vertex = previous.begin();
                for (const Vec3 &current_vertex : *CURRENT_FRAME) {
                    const Vec3 EARLIER = *earlier_vertex++;
                    const Vec3 PREVIOUS = *previous_vertex++;
                    worst = std::max(worst,
                        std::hypot((current_vertex.x - (2 * PREVIOUS.x) + EARLIER.x) * WINDOW.w,
                            (current_vertex.y - (2 * PREVIOUS.y) + EARLIER.y) * WINDOW.h,
                            (current_vertex.z - (2 * PREVIOUS.z) + EARLIER.z) * LENGTH_SCALE));
                }
            }
            // Per-frame velocity change in pixels at 120 Hz; the undamped swing peaked near 60
            // here.
            CHECK(worst < 30);
        }
    }
    void exits() {
        auto material = params();
        material.flutter = 0;
        auto falling = close(Edge::Bottom, material);
        const auto START = pose(falling);
        advance(falling, 0.25);
        const auto DROP = pose(falling);
        double average_yvelocity = 0;
        const auto *speed = DROP.v.begin();
        const auto *start = START.p.begin();
        for (const auto &vertex : std::span(DROP.p).first(GRID)) {
            CHECK(vertex.y < start++->y - 0.1);
            average_yvelocity += speed++->y;
        }
        CHECK(average_yvelocity < 0);
        CHECK(falling.opacity() == 1);  // visible sheet cannot fade prematurely
        advance(falling, 0.45);
        const auto LATE = pose(falling);
        for (const auto &vertex_pos : LATE.p) {
            CHECK(WINDOW.y + (WINDOW.h / 2) + ((-vertex_pos.y * WINDOW.h) - (WINDOW.h / 2)) >
                  SCREEN.y + SCREEN.h);
        }
        advance(falling, 1);
        CHECK(falling.closed() && falling.opacity() == 0);
        for (const Edge EDGE : {Edge::Top, Edge::Left, Edge::Right}) {
            auto pulling = close(EDGE, material);
            const auto INITIAL = pose(pulling);
            advance(pulling, 0.4);
            const auto MID = pose(pulling);
            const auto *initial = INITIAL.p.begin();
            for (const auto &vertex : std::span(MID.p).first(GRID)) {
                if (EDGE == Edge::Top) {
                    CHECK(vertex.y > initial->y + 0.5);
                }
                if (EDGE == Edge::Left) {
                    CHECK(vertex.x < initial->x - 0.5);
                }
                if (EDGE == Edge::Right) {
                    CHECK(vertex.x > initial->x + 0.5);
                }
                ++initial;
            }
            advance(pulling, 0.35);
            const auto END = pose(pulling);
            for (const auto &vertex : END.p) {
                if (EDGE == Edge::Top) {
                    CHECK(WINDOW.y - (vertex.y * WINDOW.h) < SCREEN.y);
                }
                if (EDGE == Edge::Left) {
                    CHECK(WINDOW.x + (WINDOW.w / 2) + (vertex.x * WINDOW.w) < SCREEN.x);
                }
                if (EDGE == Edge::Right) {
                    CHECK(WINDOW.x + (WINDOW.w / 2) + (vertex.x * WINDOW.w) > SCREEN.x + SCREEN.w);
                }
            }
            advance(pulling, 1);
            CHECK(pulling.closed());
        }
    }
    void exit_corridor() {
        const Box WINDOW{.x = 100, .y = 50, .w = 1000, .h = 400};
        const Box SCREEN{.x = 0, .y = 0, .w = 1920, .h = 1080};
        const Box BELOW{.x = 100, .y = 460, .w = 1000, .h = 500};
        const Box RIGHT{.x = 1110, .y = 50, .w = 600, .h = 400};
        auto material = params();
        material.flutter = material.variation = 0;
        material.duration = .9;
        for (const auto EDGE : {Edge::Bottom, Edge::Right}) {
            const Box OBSTACLE = EDGE == Edge::Bottom ? BELOW : RIGHT;
            ClothFlutter clear;
            ClothFlutter blocked;
            clear.begin(Transition::Close,
                material,
                1,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = EDGE, .screen = SCREEN});
            blocked.begin(Transition::Close,
                material,
                1,
                WINDOW,
                std::span<const Box>(&OBSTACLE, 1),
                ClothCloseGeometry{.edge = EDGE, .screen = SCREEN});
            CHECK(clear.wait_for_recording());
            CHECK(blocked.wait_for_recording());
            advance(clear, .7);
            advance(blocked, .7);
            CHECK(delta(pose(clear).p, pose(blocked).p) == 0);
            const auto FINAL = pose(blocked);
            for (const auto &vertex : FINAL.p) {
                CHECK(
                    std::abs(vertex.x) < 20 && std::abs(vertex.y) < 20 && std::abs(vertex.z) < 20);
                if (EDGE == Edge::Bottom) {
                    CHECK(WINDOW.y - (vertex.y * WINDOW.h) > SCREEN.y + SCREEN.h);
                } else {
                    CHECK(WINDOW.x + (WINDOW.w / 2) + (vertex.x * WINDOW.w) > SCREEN.x + SCREEN.w);
                }
            }
        }
    }
    void handoff() {
        for (const double WHEN : {0.015, 0.25, 0.73}) {
            auto cloth = open();
            advance(cloth, WHEN);
            const auto BEFORE = pose(cloth);
            const double ALPHA = cloth.opacity();
            auto material = params();
            material.duration = 1.6;
            cloth.begin(Transition::Close,
                material,
                12,
                WINDOW,
                {},
                ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
            const auto AFTER = pose(cloth);
            CHECK(delta(BEFORE.p, AFTER.p) < 1e-12);
            CHECK(delta(BEFORE.v, AFTER.v) < 1e-12);
            CHECK(cloth.opacity() == ALPHA);
            CHECK(cloth.wait_for_recording());
            const auto READY = pose(cloth);
            CHECK(delta(BEFORE.p, READY.p) < 2e-6);
            CHECK(delta(BEFORE.v, READY.v) < 2e-5);
        }
    }
    void extreme_duration_handoff() {
        auto material = params();
        material.duration = .1;
        material.flutter = 1;
        material.variation = 0;
        auto cloth = open(material);
        advance(cloth, .03, .001);
        const auto BEFORE = pose(cloth);
        const double ALPHA = cloth.opacity();
        material.duration = 3.430429;
        cloth.begin(Transition::Close,
            material,
            2,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Left, .screen = SCREEN});
        CHECK(cloth.wait_for_recording());
        const auto AFTER = pose(cloth);
        CHECK(delta(BEFORE.p, AFTER.p) < 2e-6);
        CHECK(delta(BEFORE.v, AFTER.v) < 2e-4);
        CHECK(cloth.opacity() == ALPHA);
    }
    void cadence() {
        const auto SOURCE = open();
        auto reference = SOURCE;
        advance(reference, 0.63, 0.001);
        const auto EXPECTED = pose(reference);
        for (const double REFRESH_HZ : {60., 120., 144., 240.}) {
            auto cloth = SOURCE;
            advance(cloth, 0.63, 1 / REFRESH_HZ);
            CHECK(delta(EXPECTED.p, pose(cloth).p) < 1e-10);
        }
        auto irregular = SOURCE;
        double time = 0;
        const std::array<double, 6> STEPS{.003, .027, .011, .007, .045, .016};
        const auto *step = STEPS.begin();
        while (time < .63 - 1e-12) {
            const double STEP_SECONDS = std::min(*step, .63 - time);
            if (++step == STEPS.end()) {
                step = STEPS.begin();
            }
            irregular.advance(STEP_SECONDS);
            time += STEP_SECONDS;
        }
        CHECK(delta(EXPECTED.p, pose(irregular).p) < 1e-10);
    }
    void deterministic() {
        auto material = params();
        material.variation = 0;
        auto first = open(material, 5);
        auto second = open(material, 900);
        advance(first, .43);
        advance(second, .43);
        CHECK(delta(pose(first).p, pose(second).p) == 0);
    }
    void invalid() {
        auto material = params();
        material.duration = std::numeric_limits<double>::quiet_NaN();
        ClothFlutter cloth;
        cloth.begin(Transition::Open, material, 1, WINDOW, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Open, params(), 1, Box{.x = 0, .y = 0, .w = 0, .h = 500}, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Open,
            params(),
            1,
            Box{.x = 0, .y = 0, .w = std::numeric_limits<double>::max(), .h = 500},
            {});
        CHECK(!cloth.active());
        cloth.begin(static_cast<Transition>(99), params(), 1, WINDOW, {});
        CHECK(!cloth.active());
        cloth.begin(Transition::Close,
            params(),
            1,
            WINDOW,
            {},
            ClothCloseGeometry{
                .edge = Edge::Left,
                .screen = Box{.x = 0, .y = 0, .w = -1, .h = 500},
            });
        CHECK(!cloth.active());
        cloth = open();
        cloth.advance(-1);
        CHECK(!cloth.active() && !cloth.closed());
        cloth = close(Edge::Top);
        cloth.advance(std::numeric_limits<double>::infinity());
        CHECK(cloth.closed());
    }
    void starvation() {
        ClothPhysics::pause_workers_for_testing(true);
        ClothFlutter opening;
        opening.begin(Transition::Open, params(), 1, WINDOW, {});
        advance(opening, 0.55);
        CHECK(!opening.active() && opening.opacity() == 1);
        ClothFlutter closing;
        closing.begin(Transition::Close,
            params(),
            1,
            WINDOW,
            {},
            ClothCloseGeometry{.edge = Edge::Bottom, .screen = SCREEN});
        advance(closing, 0.55);
        CHECK(closing.closed() && closing.opacity() == 0);
        ClothPhysics::pause_workers_for_testing(false);
        // Exhaust a partial recording by pausing publication after the worker has started.
        ClothPhysics::pause_workers_for_testing(true, 2);
        ClothFlutter partial;
        partial.begin(Transition::Open, params(), 7, WINDOW, {});
        CHECK(partial.wait_for_snapshots(2));
        advance(partial, 1);
        CHECK(!partial.active());
        ClothPhysics::pause_workers_for_testing(false);
    }
    // A recoverable publication gap must retain the same recording and resume it.
    void publication_recovery() {
        constexpr double STEP_SECONDS = 1.0 / 120;
        for (const auto KIND : {Transition::Open, Transition::Close}) {
            ClothPhysics::pause_workers_for_testing(true, 4);
            ClothFlutter cloth;
            cloth.begin(KIND,
                params(),
                7,
                WINDOW,
                {},
                KIND == Transition::Close ? std::optional<ClothCloseGeometry>{ClothCloseGeometry{
                                                .edge = Edge::Bottom,
                                                .screen = SCREEN,
                                            }}
                                          : std::optional<ClothCloseGeometry>{});
            CHECK(cloth.wait_for_snapshots(4));
            const ClothFlutter WITNESS = cloth;
            advance(cloth, .1, STEP_SECONDS);
            const auto HELD = pose(cloth);
            const double ALPHA = cloth.opacity();
            advance(cloth, .1, STEP_SECONDS);
            CHECK(cloth.active());
            CHECK(delta(pose(cloth).p, HELD.p) < 1e-9);
            CHECK(std::abs(cloth.opacity() - ALPHA) < 1e-9);
            ClothPhysics::pause_workers_for_testing(false);
            CHECK(WITNESS.wait_for_recording());
            const auto BEFORE = pose(cloth);
            advance(cloth, .1, STEP_SECONDS);
            CHECK(delta(pose(cloth).p, BEFORE.p) > 1e-9);
            advance(cloth, 2, STEP_SECONDS);
            CHECK(KIND == Transition::Open ? !cloth.active() && !cloth.closed() : cloth.closed());
        }
    }

    void movement_publication_recovery() {
        // A gap shorter than the stall bound must recover even when the compositor posts targets
        // faster than Jolt.
        for (const int REFRESH_RATE : {120, 360, 1000}) {
            const double STEP_SECONDS = 1.0 / REFRESH_RATE;
            ClothFlutter cloth;
            cloth.begin_movement({}, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            const ClothFlutter WITNESS = cloth;
            ClothPhysics::pause_workers_for_testing(true, 2);
            Box box = WINDOW;
            for (int i = 0; i < REFRESH_RATE / 5; ++i) {
                box.x += 1;
                cloth.update_movement(box, STEP_SECONDS);
                cloth.advance(STEP_SECONDS);
            }
            CHECK(cloth.moving());
            ClothPhysics::pause_workers_for_testing(false);
            CHECK(WITNESS.wait_for_snapshots(26, 2));
            cloth.advance(0);
            CHECK(cloth.moving());
            CHECK(delta(pose(cloth).p, pose(WITNESS).p) > 1e-7);
        }
    }

    void cloth_shine() {
        ClothFlutter cloth;
        auto material = params();
        material.shine = std::numeric_limits<double>::quiet_NaN();
        cloth.begin(Transition::Open, material, 1, WINDOW, {});
        CHECK(!cloth.active());
        ClothMovementParams movement;
        movement.shine = std::numeric_limits<double>::infinity();
        cloth.begin_movement(movement, 1, WINDOW);
        CHECK(!cloth.active());

        auto source = open();
        advance(source, .25);
        auto dark = source;
        auto bright = source;
        movement.shine = 0;
        dark.begin_movement(movement, 1, WINDOW);
        movement.shine = 1;
        bright.begin_movement(movement, 1, WINDOW);
        SheetFrame first;
        SheetFrame second;
        dark.frame({.x = WINDOW.w, .y = WINDOW.h}, first);
        bright.frame({.x = WINDOW.w, .y = WINDOW.h}, second);
        bool highlight = false;
        const auto *bright_triangle = second.shade.begin();
        for (const auto &triangle : first.shade) {
            const auto *bright_corner = bright_triangle++->begin();
            for (const float SHADE : triangle) {
                CHECK(SHADE <= 0);
                if (*bright_corner++ > 0) {
                    highlight = true;
                }
            }
        }
        CHECK(highlight);
    }

    void cancel_and_unload() {
        ClothPhysics::pause_workers_for_testing(true);
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) {
            ClothFlutter cloth;
            cloth.begin(Transition::Open, params(), i, WINDOW, {});
            cloth.reset();
        }
        CHECK(std::chrono::steady_clock::now() - START < std::chrono::seconds(1));
        ClothPhysics::pause_workers_for_testing(false);
    }

    void movement_step(ClothFlutter &cloth, const Box &box, double seconds, double &time) {
        cloth.update_movement(box, seconds);
        cloth.advance(seconds);
        time += seconds;
        if (cloth.moving()) {
            CHECK(cloth.wait_for_snapshots(2 + int(std::floor((time * 120) + 1e-7))));
            cloth.advance(0);
        }
    }

    // A curtain's rod is its entire top edge, irrespective of where it travels or where a drag
    // began.
    void pocket_header_stays_horizontal() {
        auto cloth = open();
        advance(cloth, .2);
        const auto HORIZONTAL = [](const ClothFlutter &sheet) {
            SheetFrame frame;
            sheet.frame({.x = WINDOW.w, .y = WINDOW.h}, frame);
            double low = frame.position.front().y;
            double high = low;
            for (const auto &vertex : std::span(frame.position).first(GRID).subspan(1)) {
                low = std::min(low, vertex.y);
                high = std::max(high, vertex.y);
            }
            return high - low < 1e-6;
        };
        CHECK(HORIZONTAL(cloth));
        cloth.begin_movement({}, 7, WINDOW);
        CHECK(HORIZONTAL(cloth));
        CHECK(cloth.wait_for_snapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int k = 0; k < 24; ++k) {
            box.x += 4;
            movement_step(cloth, box, 1.0 / 120, time);
            CHECK(HORIZONTAL(cloth));
        }
    }

    // The displayed sleeve must stay in the real window bounds, including reversal, resize and
    // worker recovery.
    void pocket_stays_within_window_bounds() {
        constexpr double STEP_SECONDS = 1.0 / 120;
        for (const double SPRINGINESS : {0., .5, 1.}) {
            for (const double DIRECTION : {-1., 1.}) {
                auto material = params();
                material.flutter = material.variation = 0;
                auto cloth = open(material);
                cloth.begin_movement({.springiness = SPRINGINESS}, 7, WINDOW);
                CHECK(cloth.wait_for_snapshots(2));
                Box box = WINDOW;
                double time = 0;
                double overhang = 0;
                bool ordered = true;
                bool level = true;
                const auto VERIFY = [&] {
                    const auto SAMPLED = pose(cloth);
                    SheetFrame frame;
                    cloth.frame({.x = box.w, .y = box.h}, frame);
                    const auto *sampled = SAMPLED.p.begin();
                    double previous_x = sampled->x;
                    const auto *rendered = frame.position.begin();
                    for (int i = 0; i < GRID; ++i, ++sampled, ++rendered) {
                        overhang = std::max(overhang, (std::abs(rendered->x) - .5) * box.w);
                        ordered &= i == 0 || sampled->x >= previous_x;
                        level &= std::abs(sampled->y) < 1e-6 && std::abs(sampled->z) < 1e-6;
                        previous_x = sampled->x;
                    }
                };
                VERIFY();
                for (int k = 0; k < 64; ++k) {
                    box.x += (k < 32 ? DIRECTION : -DIRECTION) * 12;
                    if (k == 32) {
                        box.w *= .7;
                        box.h *= 1.1;
                    }
                    movement_step(cloth, box, STEP_SECONDS, time);
                    VERIFY();
                }
                ClothPhysics::pause_workers_for_testing(true);
                for (int k = 0; k < 72; ++k) {
                    box.x -= DIRECTION * 12;
                    cloth.update_movement(box, STEP_SECONDS);
                    cloth.advance(STEP_SECONDS);
                    VERIFY();
                }
                ClothPhysics::pause_workers_for_testing(false);
                CHECK(overhang <= 1e-6);
                CHECK(ordered);
                CHECK(level);
            }
        }
    }

    // A gently travelling gathered sleeve spreads faster at the end opposite its travel direction.
    void pocket_spring_is_stiffer_behind_travel() {
        for (const double DIRECTION : {-1., 1.}) {
            auto material = params();
            material.flutter = material.variation = 0;
            auto cloth = open(material);
            cloth.begin_movement({.springiness = 1}, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            const auto START = pose(cloth);
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 24; ++k) {
                box.x += DIRECTION * .25;
                movement_step(cloth, box, 1.0 / 120, time);
            }
            const auto SPREAD = pose(cloth);
            const Vec3 &trailing =
                DIRECTION > 0 ? SPREAD.p.front() : std::get<PAPER_CELLS>(SPREAD.p);
            const Vec3 &trailing_start =
                DIRECTION > 0 ? START.p.front() : std::get<PAPER_CELLS>(START.p);
            const Vec3 &leading =
                DIRECTION > 0 ? std::get<PAPER_CELLS>(SPREAD.p) : SPREAD.p.front();
            const Vec3 &leading_start =
                DIRECTION > 0 ? std::get<PAPER_CELLS>(START.p) : START.p.front();
            const double TRAILING_SPREAD = -DIRECTION * (trailing.x - trailing_start.x);
            const double LEADING_SPREAD = DIRECTION * (leading.x - leading_start.x);
            CHECK(LEADING_SPREAD > .005);
            CHECK(TRAILING_SPREAD > LEADING_SPREAD * 1.35);
        }
    }

    void pocket_springiness_control() {
        std::array<double, 3> peaks{};
        auto *peak = peaks.begin();
        for (const double SPRINGINESS : {0., .25, 1.}) {
            ClothMovementParams material;
            if (SPRINGINESS > 0) {
                material.springiness = SPRINGINESS;
            }
            ClothFlutter cloth;
            cloth.begin_movement(material, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            double lower_drift = 0;
            for (int k = 0; k < 48; ++k) {
                box.x += k < 24 ? 5 : -5;
                movement_step(cloth, box, 1.0 / 120, time);
                const auto MOVED = pose(cloth);
                const auto *upper = MOVED.p.begin();
                auto lower = std::span(MOVED.p).last(GRID).begin();
                for (int i = 0; i < GRID; ++i, ++upper, ++lower) {
                    const double REST = (static_cast<double>(i) / PAPER_CELLS) - .5;
                    *peak = std::max(*peak, std::abs(upper->x - REST) * box.w);
                    lower_drift = std::max(lower_drift, std::abs(lower->x - REST) * box.w);
                }
            }
            CHECK(lower_drift > 1);
            ++peak;
        }
        CHECK(std::get<0>(peaks) < .001);
        CHECK(std::get<1>(peaks) > .25);
        CHECK(std::get<2>(peaks) > std::get<1>(peaks) * 1.5);
        ClothFlutter invalid;
        invalid.begin_movement(
            {.springiness = std::numeric_limits<double>::quiet_NaN()}, 7, WINDOW);
        CHECK(!invalid.active());
    }

    void pocket_slides_inward() {
        for (const double DIRECTION : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter cloth;
            cloth.begin_movement(material, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int k = 0; k < 30; ++k) {
                box.x += DIRECTION * 5;
                movement_step(cloth, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(cloth);
            bool on_rod = true;
            double previous_x = MOVED.p.front().x;
            int vertex_index = 0;
            for (const Vec3 &point : std::span(MOVED.p).first(GRID)) {
                on_rod &= point.x >= -.50001 && point.x <= .50001 &&
                          (vertex_index == 0 || point.x >= previous_x);
                previous_x = point.x;
                ++vertex_index;
            }
            CHECK(on_rod);
            CHECK(DIRECTION > 0 ? std::get<PAPER_CELLS>(MOVED.p).x < .495
                                : std::get<0>(MOVED.p).x > -.495);
        }
    }

    // The lengthwise spring must restore the pocket even while the window keeps travelling.
    void pocket_spring_restores_during_travel() {
        for (const double DIRECTION : {-1., 1.}) {
            ClothFlutter cloth;
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            cloth.begin_movement(material, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            double peak = 0;
            double offset = 0;
            for (int frame = 0; frame < 120; ++frame) {
                box.x += DIRECTION * 5;
                movement_step(cloth, box, 1.0 / 120, time);
                const auto MOVED = pose(cloth);
                offset = 0;
                int vertex_index = 0;
                for (const auto &vertex : std::span(MOVED.p).first(GRID)) {
                    offset += vertex.x - ((static_cast<double>(vertex_index++) / PAPER_CELLS) - .5);
                }
                offset /= GRID;
                peak = std::max(peak, std::abs(offset));
            }
            CHECK(cloth.moving());
            CHECK(peak > .005);
            CHECK(std::abs(offset) < peak * .5);
        }
    }

    // Direction changes and capture resizing must preserve the bounded horizontal sleeve.
    void pocket_spring_reversal_and_resize() {
        for (const double DIRECTION : {-1., 1.}) {
            ClothMovementParams material;
            material.variation = 0;
            material.springiness = 1;
            ClothFlutter cloth;
            cloth.begin_movement(material, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            const auto STEP = [&](double displacement) {
                box.x += displacement;
                movement_step(cloth, box, 1.0 / 120, time);
            };
            const auto CENTER = [&] {
                const auto POSE = pose(cloth);
                double sum = 0;
                for (const auto &vertex : std::span(POSE.p).first(GRID)) {
                    sum += vertex.x;
                }
                return sum / GRID;
            };
            const double START = CENTER();
            for (int i = 0; i < 8; ++i) {
                STEP(DIRECTION * 5);
            }
            const double EARLY = CENTER();
            for (int i = 0; i < 40; ++i) {
                STEP(DIRECTION * 5);
            }
            const double LATER = CENTER();
            CHECK(DIRECTION * (EARLY - START) < -1e-4);

            for (int i = 0; i < 30; ++i) {
                STEP(-DIRECTION * 5);
            }
            const auto REVERSED = pose(cloth);
            double reversed_center = 0;
            bool on_rod = true;
            double previous_x = REVERSED.p.front().x;
            int vertex_index = 0;
            for (const Vec3 &vertex : std::span(REVERSED.p).first(GRID)) {
                reversed_center += vertex.x;
                on_rod &= vertex.x >= -.50001 && vertex.x <= .50001 &&
                          (vertex_index == 0 || vertex.x >= previous_x);
                previous_x = vertex.x;
                ++vertex_index;
            }
            reversed_center /= GRID;
            CHECK(on_rod);
            CHECK(DIRECTION * (reversed_center - LATER) > 1e-4);

            box.w *= .9;
            box.h *= 1.1;
            movement_step(cloth, box, 1.0 / 120, time);
            const auto RESIZED = pose(cloth);
            int resized_vertex_index = 0;
            const auto *previous = RESIZED.p.begin();
            for (const auto *current = RESIZED.p.begin(); resized_vertex_index < GRID;
                ++resized_vertex_index, ++current) {
                CHECK(current->x >= -.50001 && current->x <= .50001);
                CHECK(resized_vertex_index == 0 || current->x >= previous->x);
                CHECK(std::abs(current->y) < 1e-5 && std::abs(current->z) < 1e-5);
                previous = current;
            }
        }
    }

    void curtain_rod_movement() {
        for (const Vec2 DIRECTION : {
                 Vec2{.x = 1, .y = 0},
                 Vec2{.x = -1, .y = 0},
                 Vec2{.x = 0, .y = 1},
                 Vec2{.x = 0, .y = -1},
             }) {
            ClothFlutter cloth;
            ClothMovementParams material;
            material.variation = 0;
            cloth.begin_movement(material, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 24; ++frame) {
                box.x += 4 * DIRECTION.x;
                box.y += 4 * DIRECTION.y;
                movement_step(cloth, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(cloth);
            bool top_attached = true;
            double previous_x = MOVED.p.front().x;
            int vertex_index = 0;
            for (const auto &vertex_pos : std::span(MOVED.p).first(GRID)) {
                top_attached &= vertex_pos.x >= -.50001 && vertex_pos.x <= .50001 &&
                                (vertex_index == 0 || vertex_pos.x >= previous_x) &&
                                std::abs(vertex_pos.y) < 1e-5 && std::abs(vertex_pos.z) < 1e-5;
                previous_x = vertex_pos.x;
                ++vertex_index;
            }
            CHECK(top_attached);
            if (DIRECTION.x != 0) {
                const auto MOTION_AT = [&](int row) {
                    double sum = 0;
                    int vertex_index = 0;
                    const auto ROW_POINTS =
                        std::span(MOVED.p).subspan(static_cast<std::size_t>(row) * GRID, GRID);
                    for (const auto &vertex : ROW_POINTS) {
                        sum +=
                            std::pow((vertex.x - (static_cast<double>(vertex_index) / PAPER_CELLS) +
                                         .5) *
                                         box.w,
                                2) +
                            std::pow(
                                (vertex.y + (static_cast<double>(row) / PAPER_CELLS)) * box.h, 2) +
                            (vertex.z * vertex.z * box.w * box.h);
                        ++vertex_index;
                    }
                    return std::sqrt(sum / GRID);
                };
                CHECK(MOTION_AT(PAPER_CELLS) > MOTION_AT(PAPER_CELLS / 4) + 1);
            }
        }
    }

    void curtain_rod_close() {
        for (const Edge EDGE : {Edge::Top, Edge::Bottom, Edge::Left, Edge::Right}) {
            auto cloth = close(EDGE);
            advance(cloth, .2);
            const auto MOVED = pose(cloth);
            bool straight_rod = true;
            const auto ROW_POINTS = std::span(MOVED.p).first(GRID);
            auto previous = ROW_POINTS.begin();
            for (const Vec3 &point : ROW_POINTS.subspan(1)) {
                straight_rod &= point.x >= previous++->x &&
                                point.x - ROW_POINTS.front().x <= 1.00001 &&
                                std::abs(point.y - ROW_POINTS.front().y) < 1e-5 &&
                                std::abs(point.z - ROW_POINTS.front().z) < 1e-5;
            }
            CHECK(straight_rod);
        }
    }

    void movement() {
        ClothMovementParams params;
        params.flutter = 0;
        params.variation = 0;
        params.springiness = 1;
        ClothFlutter cloth;
        cloth.begin_movement(params, 1, WINDOW);
        CHECK(cloth.moving() && cloth.wait_for_snapshots(2));
        Box box = WINDOW;
        double time = 0;
        for (int i = 0; i < 24; ++i) {
            box.x += 4;
            movement_step(cloth, box, 1.0 / 120, time);
        }
        auto moved = pose(cloth);
        CHECK(std::ranges::any_of(moved.p, [](const Vec3 &point) { return point.z > .001; }));
        for (int i = 0; i < 12; ++i) {
            box.x -= 4;
            movement_step(cloth, box, 1.0 / 120, time);
        }
        moved = pose(cloth);
        // A retained copy freezes the displayed pose; closing consumes it without sharing mutable
        // playback state.
        ClothFlutter closing = cloth;
        const double ALPHA = cloth.opacity();
        closing.begin(Transition::Close,
            ::params(),
            4,
            box,
            {},
            ClothCloseGeometry{.edge = Edge::Right, .screen = SCREEN});
        CHECK(closing.wait_for_snapshots(2));
        CHECK(delta(pose(closing).p, moved.p) < 1e-6);
        CHECK(delta(pose(closing).v, moved.v) < 1e-5);
        CHECK(std::abs(closing.opacity() - ALPHA) < 1e-8);
        box.w *= .9;
        box.h *= 1.1;
        movement_step(cloth, box, 1.0 / 120, time);
        for (int i = 0; i < 55 && cloth.moving(); ++i) {
            movement_step(cloth, box, 1.0 / 120, time);
        }
        advance(cloth, .25);
        CHECK(!cloth.active() && cloth.opacity() == 1);
        hyprimate::SheetFrame frame;
        cloth.frame({.x = box.w, .y = box.h}, frame);
        std::size_t vertex = 0;
        for (const auto &position : frame.position) {
            const auto TEX_COORD = hyprimate::sheet_uv(vertex++);
            CHECK(position.x == TEX_COORD.x - .5 && position.y == TEX_COORD.y - .5);
        }
    }

    void movement_handoff() {
        auto cloth = open();
        advance(cloth, .3);
        const auto BEFORE = pose(cloth);
        const double ALPHA = cloth.opacity();
        cloth.begin_movement({}, 2, WINDOW);
        CHECK(delta(pose(cloth).p, BEFORE.p) < 1e-6);
        CHECK(delta(pose(cloth).v, BEFORE.v) < 1e-6);
        CHECK(std::abs(cloth.opacity() - ALPHA) < 1e-8);
        CHECK(cloth.wait_for_snapshots(2));
        Box box = WINDOW;
        box.x += 10;
        double time = 0;
        movement_step(cloth, box, 1.0 / 120, time);
        CHECK(cloth.moving());
        // Bounded starvation flattens, and a new move during that flattening continues from its
        // visible pose.
        ClothPhysics::pause_workers_for_testing(true, 2);
        advance(cloth, .12);
        const auto FALLBACK = pose(cloth);
        box.y += 20;
        cloth.update_movement(box, .01);
        CHECK(cloth.moving());
        CHECK(delta(pose(cloth).p, FALLBACK.p) < 1e-6);
        ClothPhysics::pause_workers_for_testing(false);
        cloth.reset();
    }

    struct TrajectoryInput {
        double cadence = 0;
        std::uint64_t seed = 0;
        bool irregular = false;
    };
    Pose movement_trajectory(TrajectoryInput input) {
        ClothMovementParams params;
        params.variation = 0;
        ClothFlutter cloth;
        cloth.begin_movement(params, input.seed, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        double time = 0;
        int frame = 0;
        while (time < .2 - 1e-9) {
            double factor = 1;
            if (input.irregular) {
                factor = ((frame++ % 2) != 0) ? 1.6 : .4;
            }
            const double STEP_SECONDS = std::min(input.cadence * factor, .2 - time);
            Box box = WINDOW;
            box.x += 300 * (time + STEP_SECONDS);
            movement_step(cloth, box, STEP_SECONDS, time);
        }
        return pose(cloth);
    }

    void movement_cadence_and_invalid() {
        const auto REFERENCE = movement_trajectory({.cadence = 1.0 / 120, .seed = 7});
        for (const double REFRESH_HZ : {60., 144., 240.}) {
            const auto SAMPLED = movement_trajectory({.cadence = 1 / REFRESH_HZ, .seed = 100});
            CHECK(delta(REFERENCE.p, SAMPLED.p) < .015);
        }
        const auto SAME = movement_trajectory({.cadence = 1.0 / 120, .seed = 900});
        CHECK(delta(REFERENCE.p, SAME.p) == 0);
        CHECK(delta(REFERENCE.p, movement_trajectory({1.0 / 120, 100, true}).p) < .015);
        ClothFlutter cloth;
        auto params = ClothMovementParams{};
        params.strength = 0;
        cloth.begin_movement(params, 1, WINDOW);
        CHECK(!cloth.active());
        params.strength = std::numeric_limits<double>::quiet_NaN();
        cloth.begin_movement(params, 1, WINDOW);
        CHECK(!cloth.active());
        cloth.begin_movement({}, 1, WINDOW);
        cloth.update_movement(Box{.x = 0, .y = 0, .w = -1, .h = 200}, .01);
        CHECK(!cloth.active());
        ClothPhysics::pause_workers_for_testing(true, 2);
        cloth.begin_movement({}, 1, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        advance(cloth, .5);
        CHECK(!cloth.active());
        ClothPhysics::pause_workers_for_testing(false);
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            cloth.begin_movement({}, i, WINDOW);
            cloth.reset();
        }
        CHECK(std::chrono::steady_clock::now() - START < std::chrono::seconds(1));
    }
    // Each frame posts a target the worker has not answered yet. The displayed sheet must already
    // be fully simulated, so it cannot depend on whether the worker happened to answer before the
    // frame was drawn.
    void movement_independent_of_worker_timing() {
        ClothMovementParams params;
        params.variation = 0;
        constexpr int FRAMES = 30;
        constexpr double FRAME = 1.0 / 60;
        std::vector<SheetGrid> answered;
        {
            ClothFlutter animation;
            animation.begin_movement(params, 3, WINDOW);
            CHECK(animation.wait_for_snapshots(2));
            Box box = WINDOW;
            for (int k = 1; k <= FRAMES; ++k) {
                box.x += 5;
                animation.update_movement(box, FRAME);
                animation.advance(FRAME);
                CHECK(animation.wait_for_snapshots(2 + (2 * k)));
                animation.advance(0);
                answered.push_back(pose(animation).p);
            }
        }
        ClothFlutter cloth;
        cloth.begin_movement(params, 3, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        Box box = WINDOW;
        double worst = 0;
        auto reference = answered.begin();
        for (int k = 1; k <= FRAMES; ++k) {
            // Let the worker answer every earlier target, then hold it before the one this frame
            // posts.
            ClothPhysics::pause_workers_for_testing(true, 2 + (2 * (k - 1)));
            CHECK(cloth.wait_for_snapshots(2 + (2 * (k - 1))));
            box.x += 5;
            cloth.update_movement(box, FRAME);
            cloth.advance(FRAME);
            worst = std::max(worst, delta(*reference++, pose(cloth).p));
        }
        ClothPhysics::pause_workers_for_testing(false);
        CHECK(worst < 1e-9);
    }

    void movement_gravity_control() {
        const auto HEM_AFTER_DRAG = [](double gravity) {
            ClothFlutter animation;
            animation.begin_movement({.variation = 0, .gravity = gravity}, 7, WINDOW);
            CHECK(animation.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            for (int frame = 0; frame < 36; ++frame) {
                box.y += 2800.0 / 120;
                movement_step(animation, box, 1.0 / 120, time);
            }
            const auto MOVED = pose(animation);
            double hem = 0;
            for (const auto &vertex : std::span(MOVED.p).last(GRID)) {
                hem += vertex.y;
            }
            return hem / GRID;
        };
        const double ZERO = HEM_AFTER_DRAG(0);
        const double NORMAL = HEM_AFTER_DRAG(1);
        const double HEAVY = HEM_AFTER_DRAG(3);
        CHECK(ZERO > NORMAL + .01);
        CHECK(NORMAL > HEAVY + .01);
        CHECK(HEM_AFTER_DRAG(-1) == ZERO);
        CHECK(HEM_AFTER_DRAG(4) == HEAVY);
        for (const double INVALID :
            {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            ClothFlutter cloth;
            cloth.begin_movement({.gravity = INVALID}, 7, WINDOW);
            CHECK(!cloth.active());
        }
    }

    // A quick downward pull leaves the free hem behind instead of dragging a rigid panel.
    void downward_drag_lifts_hem() {
        for (const double REFRESH_HZ : {60., 120., 240.}) {
            ClothFlutter cloth;
            cloth.begin_movement({}, 7, WINDOW);
            CHECK(cloth.wait_for_snapshots(2));
            Box box = WINDOW;
            double time = 0;
            while (time < .3 - 1e-9) {
                box.y = WINDOW.y + (2800 * (time + (1 / REFRESH_HZ)));
                movement_step(cloth, box, 1 / REFRESH_HZ, time);
            }
            const auto LIFTED = pose(cloth);
            double hem = 0;
            for (const auto &vertex : std::span(LIFTED.p).last(GRID)) {
                hem += vertex.y + 1;
            }
            CHECK(hem / GRID > .3);
            for (const auto &vertex : std::span(LIFTED.p).first(GRID)) {
                CHECK(std::abs(vertex.y) < 1e-5);
            }
        }
    }

    void movement_bounds_and_restart() {
        ClothFlutter cloth;
        cloth.begin_movement({}, 1, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        double time = 0;
        Box box = WINDOW;
        box.x += 300;
        movement_step(cloth, box, 1.0 / 120, time);
        movement_step(cloth, box, 1.0 / 120, time);
        const auto MOVED = pose(cloth);
        constexpr auto ANCHOR = static_cast<std::size_t>(PAPER_CELLS);
        CHECK(std::get<ANCHOR>(MOVED.p).x >= -.50001 && std::get<ANCHOR>(MOVED.p).x <= .50001 &&
              std::abs(std::get<ANCHOR>(MOVED.p).y) < 1e-5);
        for (int i = 0; i < 48; ++i) {
            movement_step(cloth, box, 1.0 / 120, time);
        }
        cloth.advance(.12);  // begin the bounded final flattening transition
        const auto SETTLING = pose(cloth);
        box.y += 10;
        cloth.update_movement(box, .01);
        CHECK(cloth.moving());
        CHECK(delta(pose(cloth).p, SETTLING.p) < 1e-6);
        CHECK(delta(pose(cloth).v, SETTLING.v) < 1e-6);
    }

    void movement_rod_before_publication() {
        ClothFlutter cloth;
        cloth.begin_movement({}, 1, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        ClothPhysics::pause_workers_for_testing(true, 2);
        Box box = WINDOW;
        box.x += 20;
        cloth.update_movement(box, 1.0 / 60);
        cloth.advance(1.0 / 60);
        const auto ONSET = pose(cloth);
        CHECK(std::abs(std::get<PAPER_CELLS>(ONSET.p).x - .5) < 1e-7);
        CHECK(std::abs(std::get<PAPER_CELLS>(ONSET.v).x - 1.2) < 1e-6);
        ClothPhysics::pause_workers_for_testing(false);
        cloth.reset();

        cloth.begin_movement({}, 1, WINDOW);
        CHECK(cloth.wait_for_snapshots(2));
        box = WINDOW;
        double time = 0;
        for (int i = 0; i < 4; ++i) {
            box.x += 10;
            movement_step(cloth, box, 1.0 / 120, time);
        }
        ClothPhysics::pause_workers_for_testing(true, 6);
        cloth.update_movement(box, 1.0 / 60);
        // Already-published samples may still advance the sliding pocket; hold only once those are
        // consumed.
        advance(cloth, .1);
        const auto HELD = pose(cloth);
        advance(cloth, .02);
        CHECK(delta(pose(cloth).p, HELD.p) < 1e-9);
        ClothPhysics::pause_workers_for_testing(false);
    }
}  // namespace
int main() {
    needs_physics();
    {
        const ClothPhysics PHYSICS;
        opening_settles_flat();
        curtain_drape();
        soft_landing();
        exits();
        exit_corridor();
        handoff();
        extreme_duration_handoff();
        cadence();
        deterministic();
        invalid();
        starvation();
        publication_recovery();
        movement_publication_recovery();
        cloth_shine();
        cancel_and_unload();
        pocket_header_stays_horizontal();
        pocket_slides_inward();
        pocket_stays_within_window_bounds();
        pocket_spring_is_stiffer_behind_travel();
        pocket_springiness_control();
        pocket_spring_reversal_and_resize();
        pocket_spring_restores_during_travel();
        curtain_rod_movement();
        curtain_rod_close();
        movement();
        movement_handoff();
        movement_cadence_and_invalid();
        movement_independent_of_worker_timing();
        movement_bounds_and_restart();
        movement_rod_before_publication();
        downward_drag_lifts_hem();
        movement_gravity_control();
    }
    // Shutdown must join a currently paused worker before unregistering Jolt, even with a model
    // reference retained.
    ClothFlutter survivor;
    {
        const ClothPhysics PHYSICS;
        ClothPhysics::pause_workers_for_testing(true);
        survivor.begin(Transition::Open, params(), 1, WINDOW, {});
    }
    survivor.reset();
    if (g_failures != 0) {
        std::println(stderr, "{} cloth checks failed", g_failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
}
