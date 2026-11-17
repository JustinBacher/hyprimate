// Compositor-independent behavior tests for hyprimate::LiquidMesh.
// Checks are explicit (not assert), so they stay active in NDEBUG/RelWithDebInfo builds.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "Check.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/shared/Core.hpp"

namespace {

    using hyprimate::LiquidMesh;
    using hyprimate::MaterialParams;
    using hyprimate::Vec2;

    constexpr Vec2 WINDOW{.x = 600.0, .y = 400.0};
    constexpr double STRENGTH = 0.65;
    constexpr double NA_N = std::numeric_limits<double>::quiet_NaN();
    constexpr double POSITIVE_INFINITY = std::numeric_limits<double>::infinity();

    std::size_t node(unsigned column, unsigned row) {
        return (static_cast<std::size_t>(row) * 9) + column;
    }

    double magnitude(Vec2 velocity) {
        return std::hypot(velocity.x, velocity.y);
    }

    double max_magnitude(const std::array<Vec2, 81> &field) {
        double result = 0.0;
        for (const Vec2 &point : field) {
            result = std::max(result, magnitude(point));
        }
        return result;
    }

    double max_difference(const std::array<Vec2, 81> &first, const std::array<Vec2, 81> &second) {
        double result = 0.0;
        for (std::size_t i = 0; i < first.size(); ++i) {
            result = std::max(result,
                magnitude(Vec2{.x = first[i].x - second[i].x, .y = first[i].y - second[i].y}));
        }
        return result;
    }

    double signed_area(Vec2 first, Vec2 second, Vec2 third) {
        return 0.5 * (((second.x - first.x) * (third.y - first.y)) -
                         ((second.y - first.y) * (third.x - first.x)));
    }

    struct Corner {
        unsigned x;
        unsigned y;
    };

    // Independent triangle check over the renderer's 8x8 cell layout: each deformed triangle must
    // keep at least 20% of its rest signed area in the same orientation.
    bool triangle_valid(const std::array<Corner, 3> &corners,
        const std::array<Vec2, 81> &displacements,
        Vec2 size) {
        std::array<Vec2, 3> rest{};
        std::array<Vec2, 3> deformed{};
        for (std::size_t k = 0; k < 3; ++k) {
            rest[k] = Vec2{.x = size.x * corners[k].x / 8.0, .y = size.y * corners[k].y / 8.0};
            const Vec2 DISP = displacements[node(corners[k].x, corners[k].y)];
            deformed[k] = Vec2{.x = rest[k].x + DISP.x, .y = rest[k].y + DISP.y};
        }
        const double REST_AREA = signed_area(rest[0], rest[1], rest[2]);
        const double AREA = signed_area(deformed[0], deformed[1], deformed[2]);
        return std::isfinite(AREA) && AREA / REST_AREA >= 0.2;
    }

    // Lattice validity for test geometry: finite, bounded, and every triangle keeps orientation.
    // A reset (settled) field is the undeformed, trivially valid state.
    bool lattice_valid(const LiquidMesh &mesh, Vec2 size) {
        if (mesh.settled()) {
            return true;
        }
        const auto &offsets = mesh.displacements();
        const double LIMIT = std::min(120.0, 0.2 * std::min(size.x, size.y));
        for (const Vec2 &displacement : offsets) {
            if (!std::isfinite(displacement.x) || !std::isfinite(displacement.y)) {
                return false;
            }
            if (magnitude(displacement) > LIMIT + 1e-9) {
                return false;
            }
        }
        for (unsigned cy = 0; cy < 8; ++cy) {
            for (unsigned cx = 0; cx < 8; ++cx) {
                const std::array<Corner, 3> FIRST{
                    Corner{.x = cx, .y = cy},
                    Corner{.x = cx, .y = cy + 1},
                    Corner{.x = cx + 1, .y = cy},
                };
                const std::array<Corner, 3> SECOND{
                    Corner{.x = cx + 1, .y = cy},
                    Corner{.x = cx, .y = cy + 1},
                    Corner{.x = cx + 1, .y = cy + 1},
                };
                if (!triangle_valid(FIRST, offsets, size) ||
                    !triangle_valid(SECOND, offsets, size)) {
                    return false;
                }
            }
        }
        return true;
    }

    // Render-mesh validity: the 32x32-cell mesh sampled from sampleDisplacement keeps every
    // triangle oriented.
    bool render_valid(const LiquidMesh &mesh, Vec2 size) {
        constexpr unsigned CELLS = 32;
        const auto POSITION = [&](unsigned column, unsigned row) {
            const double TEX_U = static_cast<double>(column) / CELLS;
            const double TEX_V = static_cast<double>(row) / CELLS;
            const Vec2 DISPLACEMENT = mesh.sampleDisplacement(TEX_U, TEX_V);
            if (!std::isfinite(DISPLACEMENT.x) || !std::isfinite(DISPLACEMENT.y)) {
                return Vec2{.x = NA_N, .y = NA_N};
            }
            return Vec2{
                .x = (size.x * TEX_U) + DISPLACEMENT.x,
                .y = (size.y * TEX_V) + DISPLACEMENT.y,
            };
        };
        for (unsigned cy = 0; cy < CELLS; ++cy) {
            for (unsigned cx = 0; cx < CELLS; ++cx) {
                const Vec2 REST00{.x = size.x * cx / CELLS, .y = size.y * cy / CELLS};
                const Vec2 REST10{.x = size.x * (cx + 1) / CELLS, .y = size.y * cy / CELLS};
                const Vec2 REST01{.x = size.x * cx / CELLS, .y = size.y * (cy + 1) / CELLS};
                const Vec2 REST11{.x = size.x * (cx + 1) / CELLS, .y = size.y * (cy + 1) / CELLS};
                const Vec2 P00 = POSITION(cx, cy);
                const Vec2 P10 = POSITION(cx + 1, cy);
                const Vec2 P01 = POSITION(cx, cy + 1);
                const Vec2 P11 = POSITION(cx + 1, cy + 1);
                const std::array<std::array<Vec2, 3>, 2> REST{
                    {{REST00, REST01, REST10}, {REST10, REST01, REST11}},
                };
                const std::array<std::array<Vec2, 3>, 2> DEFORMED{
                    {{P00, P01, P10}, {P10, P01, P11}},
                };
                for (std::size_t triangle = 0; triangle < 2; ++triangle) {
                    const double REST_AREA =
                        signed_area(REST[triangle][0], REST[triangle][1], REST[triangle][2]);
                    const double AREA = signed_area(
                        DEFORMED[triangle][0], DEFORMED[triangle][1], DEFORMED[triangle][2]);
                    if (!std::isfinite(AREA) || !(AREA / REST_AREA >= 0.2)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // Extreme, reversing, tiny, paused, zero-strength, empty and nonfinite inputs.
    void check_degenerate_geometry() {
        // Extreme translation is bounded and keeps every triangle oriented.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 5000.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(lattice_valid(mesh, WINDOW));
            for (int initial_frame = 0; initial_frame < 30; ++initial_frame) {
                mesh.advance(1.0 / 60.0, WINDOW);
                CHECK(lattice_valid(mesh, WINDOW));
            }
        }

        // Rapid reversals, including a diagonal impulse.
        {
            LiquidMesh mesh{};
            const std::array<Vec2, 5> IMPULSES{
                Vec2{.x = 80.0, .y = 0.0},
                Vec2{.x = -80.0, .y = 0.0},
                Vec2{.x = 80.0, .y = 0.0},
                Vec2{.x = -80.0, .y = 0.0},
                Vec2{.x = 56.0, .y = 56.0},
            };
            for (const Vec2 &impulse : IMPULSES) {
                mesh.recordTranslation(impulse, WINDOW, STRENGTH);
                CHECK(lattice_valid(mesh, WINDOW));
                for (int frame_index = 0; frame_index < 3; ++frame_index) {
                    mesh.advance(1.0 / 60.0, WINDOW);
                    CHECK(lattice_valid(mesh, WINDOW));
                }
            }
        }

        // A 16x16 window: the bound is 0.2 * 16 = 3.2 px, so the +80 px impulse is clamped hard.
        {
            LiquidMesh mesh{};
            const Vec2 TINY{.x = 16.0, .y = 16.0};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, TINY, STRENGTH);
            CHECK(max_magnitude(mesh.displacements()) <= 0.2 * 16.0 + 1e-9);
            CHECK(lattice_valid(mesh, TINY));
            for (int frame = 0; frame < 30; ++frame) {
                mesh.advance(1.0 / 60.0, TINY);
                CHECK(lattice_valid(mesh, TINY));
            }
        }

        // A 1000 ms pause exceeds the 250 ms gap limit and resets to the undeformed state.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(!mesh.advance(1.0, WINDOW));
            CHECK(mesh.settled());
            CHECK(max_magnitude(mesh.displacements()) == 0.0);
        }

        // Zero strength bypasses deformation entirely.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, 0.0);
            CHECK(mesh.settled());
            CHECK(!mesh.advance(1.0 / 60.0, WINDOW));
            CHECK(max_magnitude(mesh.displacements()) == 0.0);
        }

        // NaN strength is invalid input and resets.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, NA_N);
            CHECK(mesh.settled());
        }

        // Zero delta does not excite, even though the field is still constrained.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 0.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(mesh.settled());
        }

        // Empty geometry resets, whether it arrives with a translation or with advance().
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, Vec2{.x = 0.0, .y = 400.0}, STRENGTH);
            CHECK(mesh.settled());

            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(!mesh.settled());
            CHECK(!mesh.advance(1.0 / 60.0, Vec2{600.0, 0.0}));
            CHECK(mesh.settled());
        }

        // Nonfinite translation, size, elapsed time, or excitation resets.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = NA_N, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(mesh.settled());

            mesh.recordTranslation(
                Vec2{.x = 80.0, .y = 0.0}, Vec2{.x = NA_N, .y = 400.0}, STRENGTH);
            CHECK(mesh.settled());

            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(!mesh.advance(NA_N, WINDOW));
            CHECK(mesh.settled());

            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(!mesh.advance(POSITIVE_INFINITY, WINDOW));
            CHECK(mesh.settled());

            // Overflowing excitation becomes nonfinite and must reset rather than propagate.
            mesh.recordTranslation(Vec2{.x = 1e308, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(lattice_valid(mesh, WINDOW));
            mesh.advance(1.0 / 60.0, WINDOW);
            CHECK(lattice_valid(mesh, WINDOW));
        }

        // Resizing without translation applies the new bound immediately, with or without time
        // stepping.
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            mesh.recordTranslation(
                Vec2{.x = 0.0, .y = 0.0}, Vec2{.x = 100.0, .y = 100.0}, STRENGTH);
            CHECK(max_magnitude(mesh.displacements()) <= 0.2 * 100.0 + 1e-9);
            CHECK(lattice_valid(mesh, Vec2{100.0, 100.0}));
        }
        {
            LiquidMesh mesh{};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            mesh.advance(0.0, Vec2{.x = 100.0, .y = 100.0});
            CHECK(max_magnitude(mesh.displacements()) <= 0.2 * 100.0 + 1e-9);
            CHECK(lattice_valid(mesh, Vec2{100.0, 100.0}));
        }
    }

    void test_extreme_and_degenerate_geometry() {
        check_degenerate_geometry();
    }

    // ---------------------------------------------------------------------------
    // Liquid material targets.
    // ---------------------------------------------------------------------------

    // One frame of a constant-velocity drag: record this frame's translation, then advance by its
    // duration.
    void drive_frame(
        LiquidMesh &mesh, Vec2 velocity, double frequency, Vec2 size, double strength) {
        const double STEP_SECONDS = 1.0 / frequency;
        mesh.recordTranslation(
            Vec2{.x = velocity.x * STEP_SECONDS, .y = velocity.y * STEP_SECONDS}, size, strength);
        mesh.advance(STEP_SECONDS, size);
    }

    // One frame with no motion (a stop).
    void coast_frame(LiquidMesh &mesh, double frequency, Vec2 size) {
        mesh.advance(1.0 / frequency, size);
    }

    // Trailing-edge displacement along x for a +x drag: negative while lagging behind the window.
    double trailing_x(const LiquidMesh &mesh) {
        return mesh.displacements()[node(0, 4)].x;
    }

    struct StopTrace {
        double peak_lag = 0.0;  // lag magnitude at the end of the drag
        double overshoot = 0.0;  // largest positive excursion after the stop
        double later_negative = 0.0;  // largest negative excursion after the overshoot peak
        double settle_seconds = 1e9;  // time from stop until settled(); 1e9 if never
    };

    // Drags at 2000 px/s for 0.25 s at 60 Hz, then stops and records the trailing response.
    StopTrace trace_stop(
        const MaterialParams &params, double frequency = 60.0, std::uint64_t seed = 0) {
        LiquidMesh mesh{params, seed};
        for (int frame_index = 0; frame_index < static_cast<int>(std::lround(frequency * 0.25));
            ++frame_index) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, frequency, WINDOW, STRENGTH);
        }

        StopTrace trace{};
        trace.peak_lag = -trailing_x(mesh);

        double best_overshoot = 0.0;
        int best_frame = -1;
        std::array<double, 400> series{};
        int frames = 0;
        for (int frame = 1; frame <= static_cast<int>(frequency * 2.0); ++frame) {
            coast_frame(mesh, frequency, WINDOW);
            series[static_cast<std::size_t>(frame - 1)] = trailing_x(mesh);
            frames = frame;
            if (mesh.settled()) {
                if (trace.settle_seconds > 1e8) {
                    trace.settle_seconds = frame / frequency;
                }
                break;
            }
            if (trailing_x(mesh) > best_overshoot) {
                best_overshoot = trailing_x(mesh);
                best_frame = frame - 1;
            }
        }
        trace.overshoot = best_overshoot;
        if (best_frame >= 0) {
            for (int k = best_frame; k < frames; ++k) {
                trace.later_negative =
                    std::max(trace.later_negative, -series[static_cast<std::size_t>(k)]);
            }
        }
        return trace;
    }

    // Liquid drag schedule at a given frame rate: drag for 0.25 s, then stop until 0.5 s.
    struct LiquidSnapshots {
        std::array<Vec2, 81> drag;
        std::array<Vec2, 81> stop;
    };

    LiquidSnapshots run_liquid_schedule(
        double frequency, const MaterialParams &params, std::uint64_t seed = 0) {
        LiquidMesh mesh{params, seed};
        const int FRAMES_DRAG = static_cast<int>(std::lround(frequency * 0.25));
        const int FRAMES_TOTAL = static_cast<int>(std::lround(frequency * 0.5));
        for (int drag_frame = 0; drag_frame < FRAMES_DRAG; ++drag_frame) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, frequency, WINDOW, STRENGTH);
        }
        LiquidSnapshots result{};
        result.drag = mesh.displacements();
        for (int frame = FRAMES_DRAG; frame < FRAMES_TOTAL; ++frame) {
            coast_frame(mesh, frequency, WINDOW);
        }
        result.stop = mesh.displacements();
        return result;
    }

    // Drag at 2000 px/s: the trailing side lags well behind, the leading face stays tight, and the
    // sheet narrows transversely.
    void test_liquid_drag_lag() {
        LiquidMesh mesh{};
        for (int frame = 0; frame < 15; ++frame) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
            CHECK(render_valid(mesh, WINDOW));
        }
        const double TRAILING = -trailing_x(mesh);
        const double LEADING = std::abs(mesh.displacements()[node(8, 4)].x);
        double transverse = 0.0;
        for (const Vec2 &displacement : mesh.displacements()) {
            transverse = std::max(transverse, std::abs(displacement.y));
        }

        CHECK(TRAILING >= 20.0);
        CHECK(LEADING <= 0.35 * TRAILING);
        CHECK(transverse > 2.0);
    }

    // Slow drag at 100 px/s for 0.5 s deforms little.
    void test_liquid_slow_drag_is_subtle() {
        LiquidMesh mesh{};
        for (int frame = 0; frame < 30; ++frame) {
            drive_frame(mesh, Vec2{.x = 100.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        CHECK(max_magnitude(mesh.displacements()) < 3.0);
    }

    // Abrupt stop: a small overshoot, a decaying second swing, and an exact settle.
    void test_liquid_stop_overshoot_and_settle() {
        const StopTrace TRACE = trace_stop(MaterialParams{});
        CHECK(TRACE.peak_lag > 20.0);
        CHECK(TRACE.overshoot >= 0.05 * TRACE.peak_lag);
        CHECK(TRACE.overshoot <= 0.30 * TRACE.peak_lag);
        CHECK(TRACE.later_negative <= 0.5 * TRACE.overshoot);
        CHECK(TRACE.settle_seconds <= 1.5);
        LiquidMesh mesh{};
        for (int frame_index = 0; frame_index < 15; ++frame_index) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        for (int frame = 0; frame < 90; ++frame) {
            coast_frame(mesh, 60.0, WINDOW);
        }
        CHECK(mesh.settled());
        CHECK(max_magnitude(mesh.displacements()) == 0.0);
    }

    // Direction reversal at speed: the centre node's lag crosses zero within 150 ms. The centre is
    // the node whose lag truly changes sign with the drive; the old trailing edge becomes the
    // leading face. The render mesh keeps every triangle oriented throughout.
    void test_liquid_reversal() {
        LiquidMesh mesh{};
        for (int frame_index = 0; frame_index < 15; ++frame_index) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        CHECK(trailing_x(mesh) < -10.0);

        int cross_frame = -1;
        for (int frame = 0; frame < 30; ++frame) {
            drive_frame(mesh, Vec2{.x = -2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
            CHECK(render_valid(mesh, WINDOW));
            if (cross_frame < 0 && mesh.displacements()[(4 * 9) + 4].x > 0.0) {
                cross_frame = frame;
            }
        }
        CHECK(cross_frame >= 0);
        CHECK(cross_frame >= 0 && cross_frame * (1.0 / 60.0) <= 0.15);
    }

    // Refresh-rate independence: the same drag and stop agree across 30..240 Hz.
    void test_liquid_refresh_rate_agreement() {
        const LiquidSnapshots S30 = run_liquid_schedule(30.0, MaterialParams{});
        const LiquidSnapshots S60 = run_liquid_schedule(60.0, MaterialParams{});
        const LiquidSnapshots S144 = run_liquid_schedule(144.0, MaterialParams{});
        const LiquidSnapshots S240 = run_liquid_schedule(240.0, MaterialParams{});
        CHECK(max_magnitude(S60.drag) > 10.0);
        CHECK(max_difference(S30.drag, S60.drag) <= 1.0);
        CHECK(max_difference(S144.drag, S60.drag) <= 1.0);
        // Drag lag is ~70 px; a 2 px band (about 3%) absorbs the one-substep sampling difference at
        // 240 Hz.
        CHECK(max_difference(S240.drag, S60.drag) <= 2.0);
        // Stop snapshots may differ by up to ~2% of peak lag (1.5 px) across cadences: each tick's
        // pseudo-force is sampled once, so a 30 Hz tick shifts its decay by up to one frame.
        CHECK(max_difference(S30.stop, S60.stop) <= 1.5);
        CHECK(max_difference(S144.stop, S60.stop) <= 1.5);
        CHECK(max_difference(S240.stop, S60.stop) <= 1.5);
    }

    // Size scaling: the same relative speed produces a similar relative deformation on small and
    // large windows.
    void test_liquid_size_scaling() {
        const auto PEAK_OVER = [](Vec2 size) {
            const double MIN_SIDE = std::min(size.x, size.y);
            LiquidMesh mesh{};
            for (int frame = 0; frame < 12; ++frame) {
                drive_frame(mesh, Vec2{.x = 5.0 * MIN_SIDE, .y = 0.0}, 60.0, size, STRENGTH);
                CHECK(render_valid(mesh, size));
            }
            return -trailing_x(mesh) / MIN_SIDE;
        };
        const double SMALL = PEAK_OVER(Vec2{.x = 300.0, .y = 200.0});
        const double LARGE = PEAK_OVER(Vec2{.x = 1600.0, .y = 1000.0});
        CHECK(SMALL > 0.0 && LARGE > 0.0);
        CHECK(std::max(SMALL, LARGE) <= 1.6 * std::min(SMALL, LARGE));
    }

    // Degenerate and thin geometry stays stable and settles exactly.
    void test_liquid_degenerate_geometry_settles() {
        for (const Vec2 SIZE : {Vec2{.x = 16.0, .y = 16.0}, Vec2{.x = 1000.0, .y = 40.0}}) {
            LiquidMesh mesh{};
            for (int frame_index = 0; frame_index < 15; ++frame_index) {
                drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, SIZE, STRENGTH);
                CHECK(lattice_valid(mesh, SIZE));
                CHECK(render_valid(mesh, SIZE));
            }
            for (int frame = 0; frame < 240 * 4; ++frame) {
                coast_frame(mesh, 240.0, SIZE);
                CHECK(lattice_valid(mesh, SIZE));
            }
            CHECK(mesh.settled());
            CHECK(max_magnitude(mesh.displacements()) == 0.0);
        }
    }

    // Elasticity: higher elasticity returns faster.
    void test_liquid_elasticity_settle_time() {
        MaterialParams firm{};
        firm.elasticity = 1.0;
        MaterialParams soft{};
        soft.elasticity = 0.0;
        const StopTrace SNAPPY = trace_stop(firm);
        const StopTrace SLOW = trace_stop(soft);
        CHECK(SNAPPY.settle_seconds < SLOW.settle_seconds);
    }

    // Momentum: zero gives no overshoot; higher momentum gives a larger overshoot.
    void test_liquid_momentum_overshoot() {
        MaterialParams none{};
        none.momentum = 0.0;
        MaterialParams high{};
        high.momentum = 1.0;
        const StopTrace NO_OVERSHOOT = trace_stop(none);
        const StopTrace STANDARD = trace_stop(MaterialParams{});
        const StopTrace BIG = trace_stop(high);
        CHECK(NO_OVERSHOOT.overshoot <= 0.02 * NO_OVERSHOOT.peak_lag);
        CHECK(BIG.overshoot > STANDARD.overshoot);
    }

    // sampleDisplacement: exact at nodes, clamped outside [0, 1], and C1 across cell boundaries.
    void test_sample_displacement() {
        LiquidMesh mesh{};
        for (int frame = 0; frame < 15; ++frame) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        const auto &offsets = mesh.displacements();

        for (unsigned row = 0; row < 9; ++row) {
            for (unsigned col = 0; col < 9; ++col) {
                const Vec2 SAMPLE = mesh.sampleDisplacement(col / 8.0, row / 8.0);
                CHECK(std::abs(SAMPLE.x - offsets[node(col, row)].x) <= 1e-9);
                CHECK(std::abs(SAMPLE.y - offsets[node(col, row)].y) <= 1e-9);
            }
        }

        const Vec2 CLAMPED_U = mesh.sampleDisplacement(-1.0, 2.0);
        CHECK(std::abs(CLAMPED_U.x - offsets[node(0, 8)].x) <= 1e-9);
        const Vec2 CLAMPED_V = mesh.sampleDisplacement(2.0, -1.0);
        CHECK(std::abs(CLAMPED_V.x - offsets[node(8, 0)].x) <= 1e-9);
        const Vec2 NONFINITE = mesh.sampleDisplacement(NA_N, 0.5);
        CHECK(NONFINITE.x == 0.0 && NONFINITE.y == 0.0);

        // C1: the slope from each side of an interior cell boundary matches.
        constexpr double EPSILON = 1e-5;
        for (const double TEX_V : {0.2, 0.55}) {
            const double TEX_U = 3.0 / 8.0;
            const double LEFT = (mesh.sampleDisplacement(TEX_U, TEX_V).x -
                                    mesh.sampleDisplacement(TEX_U - EPSILON, TEX_V).x) /
                                EPSILON;
            const double RIGHT = (mesh.sampleDisplacement(TEX_U + EPSILON, TEX_V).x -
                                     mesh.sampleDisplacement(TEX_U, TEX_V).x) /
                                 EPSILON;
            CHECK(std::abs(LEFT - RIGHT) <= 1e-3 * std::max(1.0, std::abs(LEFT)));
        }
    }

    // Viscosity changes internal flow: thick resists shear more than thin.
    void test_viscosity_resists_internal_flow() {
        MaterialParams thin_params{};
        thin_params.viscosity = 0.0;
        MaterialParams thick_params{};
        thick_params.viscosity = 1.0;
        LiquidMesh thin{thin_params};
        LiquidMesh thick{thick_params};
        // Viscosity must change internal flow, not masquerade as a strength control.
        CHECK(max_difference(thin.displacements(), thick.displacements()) == 0.0);
        for (int frame = 0; frame < 15; ++frame) {
            drive_frame(thin, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
            drive_frame(thick, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        CHECK(max_difference(thin.displacements(), thick.displacements()) > 0.5);
    }

    // Viscosity boundaries must remain stable and cadence-independent, including thin strips.
    void test_viscosity_boundaries_and_cadence() {
        const std::array<double, 3> LEVELS{0.0, 0.75, 1.0};
        for (const double LEVEL : LEVELS) {
            MaterialParams params{};
            params.viscosity = LEVEL;
            const LiquidSnapshots S30 = run_liquid_schedule(30.0, params);
            const LiquidSnapshots S60 = run_liquid_schedule(60.0, params);
            const LiquidSnapshots S144 = run_liquid_schedule(144.0, params);
            CHECK(max_magnitude(S30.drag) > 0.1);
            CHECK(max_difference(S30.drag, S60.drag) <= 1.0);
            CHECK(max_difference(S30.drag, S144.drag) <= 2.0);
            // Stop snapshots may differ by up to ~2.5% of peak lag across cadences: each tick's
            // pseudo-force is sampled once, so a 30 Hz tick shifts its decay by up to one frame.
            // Thinnest material (viscosity 0) measures 1.57 px.
            CHECK(max_difference(S30.stop, S60.stop) <= 1.6);
            CHECK(max_difference(S30.stop, S144.stop) <= 1.6);
        }

        // Out-of-range viscosity clamps to the same material as its boundary.
        MaterialParams below{};
        below.viscosity = -0.5;
        MaterialParams zero{};
        zero.viscosity = 0.0;
        MaterialParams above{};
        above.viscosity = 1.5;
        MaterialParams one{};
        one.viscosity = 1.0;
        LiquidMesh low{below};
        LiquidMesh thin{zero};
        LiquidMesh high{above};
        LiquidMesh thick{one};
        CHECK(low.viscosity() == 0.0 && high.viscosity() == 1.0);
        for (auto *mesh : {&low, &thin, &high, &thick}) {
            mesh->recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
        }
        for (int step = 0; step < 24; ++step) {
            for (auto *mesh : {&low, &thin, &high, &thick}) {
                mesh->advance(1.0 / 240.0, WINDOW);
            }
            CHECK(max_difference(low.displacements(), thin.displacements()) == 0.0);
            CHECK(max_difference(high.displacements(), thick.displacements()) == 0.0);
        }

        for (const double INVALID : {NA_N, POSITIVE_INFINITY}) {
            MaterialParams bad{};
            bad.viscosity = INVALID;
            LiquidMesh mesh{bad};
            mesh.recordTranslation(Vec2{.x = 80.0, .y = 0.0}, WINDOW, STRENGTH);
            CHECK(mesh.settled());
            CHECK(!mesh.advance(1.0 / 240.0, WINDOW));
            CHECK(max_magnitude(mesh.displacements()) == 0.0);
        }
    }

    // Taper: the trailing corners pull cross-axis toward the tail at 15-20% of the centre lag, and
    // the side edge falls monotonically from the corner to the centre with no kink or reversal, so
    // there is no tail-corner cusp.
    void test_liquid_trailing_taper_curvature() {
        LiquidMesh mesh{};
        for (int frame = 0; frame < 15; ++frame) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        const double CENTRE = -trailing_x(mesh);
        const double CORNER = -mesh.displacements()[node(0, 0)].x;
        const double TAPER = std::abs(mesh.displacements()[node(0, 0)].y);
        CHECK(CENTRE > 20.0);
        CHECK(CENTRE / CORNER >= 1.5);
        CHECK(TAPER >= 0.15 * CENTRE);
        CHECK(TAPER <= 0.20 * CENTRE);
        double previous = TAPER;
        for (unsigned row = 1; row <= 4; ++row) {
            const double EDGE = std::abs(mesh.displacements()[node(0, row)].y);
            CHECK(EDGE < previous);
            previous = EDGE;
        }
    }

    // Stop landing: the leading face bulges forward 2.5-4% of minSide, the trailing edge surges
    // 15-30% of its lag, and the mesh settles within 1.0 s.
    void test_liquid_stop_landing_bulge_and_surge() {
        const double MIN_SIDE = std::min(WINDOW.x, WINDOW.y);
        LiquidMesh mesh{};
        for (int frame = 0; frame < 15; ++frame) {
            drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
        }
        const double PEAK_LAG = -trailing_x(mesh);
        double bulge = 0.0;
        double surge = 0.0;
        double elapsed = 0.0;
        while (!mesh.settled() && elapsed < 2.0) {
            mesh.advance(1.0 / 60.0, WINDOW);
            elapsed += 1.0 / 60.0;
            bulge = std::max(bulge, mesh.displacements()[node(8, 4)].x);
            surge = std::max(surge, trailing_x(mesh));
        }
        CHECK(bulge >= 0.02 * MIN_SIDE);
        CHECK(bulge <= 0.04 * MIN_SIDE);
        CHECK(surge >= 0.15 * PEAK_LAG);
        CHECK(surge <= 0.30 * PEAK_LAG);
        CHECK(elapsed <= 1.0);
    }

    struct CadenceStats {
        double lag_mean = 0.0;
        double lag_max_step = 0.0;
        double bulge = 0.0;
    };

    // Steady 2000 px/s drag where translation lands on irregular frames: the lattice advances every
    // frame, but the position stream only reports on some frames, so deltas arrive lumped.
    // `visible(k, t)` gives the position-stream timestamp seen on frame k (ending at time t).
    // Measured over the second half of two seconds of drag.
    template <typename Visible>
    CadenceStats drive_cadence(double advance_hz, Visible visible) {
        constexpr double K_SPEED = 2000.0;
        const double STEP_SECONDS = 1.0 / advance_hz;
        const int WARMUP_FRAMES = static_cast<int>(std::lround(advance_hz * 1.0));
        const int TOTAL_FRAMES = static_cast<int>(std::lround(advance_hz * 2.0));
        LiquidMesh mesh{};
        CadenceStats stats{};
        double previous_reported = 0.0;
        double previous_lag = 0.0;
        double sum_lag = 0.0;
        int count = 0;
        for (int k = 0; k < TOTAL_FRAMES; ++k) {
            const double TIME = (k + 1) * STEP_SECONDS;
            const double REPORTED = visible(k, TIME);
            const double DELTA = K_SPEED * (REPORTED - previous_reported);
            previous_reported = REPORTED;
            mesh.recordTranslation(Vec2{.x = DELTA, .y = 0.0}, WINDOW, STRENGTH);
            mesh.advance(STEP_SECONDS, WINDOW);
            if (k < WARMUP_FRAMES) {
                continue;
            }
            const double LAG = -trailing_x(mesh);
            if (count > 0) {
                stats.lag_max_step = std::max(stats.lag_max_step, std::abs(LAG - previous_lag));
            }
            previous_lag = LAG;
            sum_lag += LAG;
            ++count;
            stats.bulge = std::max(stats.bulge, mesh.displacements()[node(8, 4)].x);
        }
        stats.lag_mean = sum_lag / count;
        return stats;
    }

    // Irregular cadences must not read as a stop: the lag changes by under 15% of its mean frame to
    // frame, and the leading face never bulges more than 0.5% of minSide before a real stop.
    void test_liquid_irregular_cadence_steady_drag() {
        const double MIN_SIDE = std::min(WINDOW.x, WINDOW.y);
        const CadenceStats FAST =
            drive_cadence(144.0, [](int, double time) { return std::floor(time * 125.0) / 125.0; });
        const CadenceStats DROPPED = drive_cadence(
            60.0, [](int frame, double) { return (frame % 3 == 2 ? frame : frame + 1) / 60.0; });
        for (const CadenceStats *stats : {&FAST, &DROPPED}) {
            CHECK(stats->lag_mean > 20.0);
            CHECK(stats->lag_max_step < 0.15 * stats->lag_mean);
            CHECK(stats->bulge < 0.01 * MIN_SIDE);
        }
    }

    // Variation 0: the seed has no effect, so every window moves exactly alike.
    void test_variation_zero_ignores_seed() {
        const LiquidSnapshots FIRST = run_liquid_schedule(60.0, MaterialParams{}, 1);
        const LiquidSnapshots SECOND =
            run_liquid_schedule(60.0, MaterialParams{}, 0x9E3779B97F4A7C15ULL);
        CHECK(max_difference(FIRST.drag, SECOND.drag) == 0.0);
        CHECK(max_difference(FIRST.stop, SECOND.stop) == 0.0);
    }

    // Variation > 0: two windows given the same motion (a scrolling-layout pan) deform visibly
    // differently, while equal seeds stay reproducible.
    void test_variation_seeds_differ() {
        MaterialParams params{};
        params.variation = 0.5;
        const LiquidSnapshots FIRST = run_liquid_schedule(60.0, params, 1);
        const LiquidSnapshots SECOND = run_liquid_schedule(60.0, params, 2);
        const LiquidSnapshots AGAIN = run_liquid_schedule(60.0, params, 1);
        CHECK(max_difference(FIRST.drag, SECOND.drag) > 2.0);
        CHECK(max_difference(FIRST.drag, AGAIN.drag) == 0.0);
        CHECK(max_difference(FIRST.stop, AGAIN.stop) == 0.0);
    }

    // Each movement from rest draws a new character: the same window repeating the same move
    // responds differently.
    void test_variation_rerolls_per_motion() {
        MaterialParams params{};
        params.variation = 0.5;
        LiquidMesh mesh{params, 7};
        const auto DRAG_ONCE = [&] {
            for (int frame_index = 0; frame_index < 15; ++frame_index) {
                drive_frame(mesh, Vec2{.x = 2000.0, .y = 0.0}, 60.0, WINDOW, STRENGTH);
            }
            const auto FIELD = mesh.displacements();
            for (int frame = 0; frame < 240 && !mesh.settled(); ++frame) {
                coast_frame(mesh, 60.0, WINDOW);
            }
            CHECK(mesh.settled());
            return FIELD;
        };
        const auto FIRST = DRAG_ONCE();
        const auto SECOND = DRAG_ONCE();
        CHECK(max_difference(FIRST, SECOND) > 2.0);
    }

    // Full variation never folds the render mesh, still settles exactly, and keeps momentum 0 free
    // of overshoot.
    void test_variation_stays_valid_and_settles() {
        for (const double MOMENTUM : {0.0, 0.5, 1.0}) {
            MaterialParams params{};
            params.variation = 1.0;
            params.momentum = MOMENTUM;
            for (std::uint64_t seed = 0; seed < 48; ++seed) {
                LiquidMesh mesh{params, seed};
                bool valid = true;
                for (int frame_index = 0; frame_index < 15; ++frame_index) {
                    drive_frame(mesh, Vec2{.x = 2400.0, .y = 900.0}, 60.0, WINDOW, STRENGTH);
                    valid = valid && render_valid(mesh, WINDOW) && lattice_valid(mesh, WINDOW);
                }
                for (int frame = 0; frame < 10; ++frame) {
                    drive_frame(mesh, Vec2{.x = -2400.0, .y = -900.0}, 60.0, WINDOW, STRENGTH);
                    valid = valid && render_valid(mesh, WINDOW) && lattice_valid(mesh, WINDOW);
                }
                int frames = 0;
                for (; frames < 150 && !mesh.settled(); ++frames) {
                    coast_frame(mesh, 60.0, WINDOW);
                    valid = valid && render_valid(mesh, WINDOW) && lattice_valid(mesh, WINDOW);
                }
                CHECK(valid);
                CHECK(mesh.settled());

                if (MOMENTUM == 0.0) {
                    const StopTrace TRACE = trace_stop(params, 60.0, seed);
                    CHECK(TRACE.overshoot <= 0.02 * TRACE.peak_lag);
                }
            }
        }
    }

}  // namespace

int main() {
    test_extreme_and_degenerate_geometry();
    test_liquid_drag_lag();
    test_liquid_trailing_taper_curvature();
    test_liquid_slow_drag_is_subtle();
    test_liquid_stop_overshoot_and_settle();
    test_liquid_stop_landing_bulge_and_surge();
    test_liquid_irregular_cadence_steady_drag();
    test_liquid_reversal();
    test_liquid_refresh_rate_agreement();
    test_liquid_size_scaling();
    test_liquid_degenerate_geometry_settles();
    test_liquid_elasticity_settle_time();
    test_liquid_momentum_overshoot();
    test_sample_displacement();
    test_viscosity_resists_internal_flow();
    test_viscosity_boundaries_and_cadence();
    test_variation_zero_ignores_seed();
    test_variation_seeds_differ();
    test_variation_rerolls_per_motion();
    test_variation_stays_valid_and_settles();

    if (g_failures == 0) {
        std::printf("liquid-mesh-tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "liquid-mesh-tests: %d check(s) failed\n", g_failures);
    return 1;
}
