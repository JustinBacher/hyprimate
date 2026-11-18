#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "animations/paper/PaperCrumple.hpp"
#include "animations/paper/PaperTakes.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {

    using hyprimate::PaperCrumple;
    using hyprimate::PaperParams;
    using hyprimate::SheetFrame;
    using hyprimate::Transition;
    using hyprimate::Vec2;

    constexpr Vec2 SIZE{.x = 1000.0, .y = 700.0};

    PaperParams open_params() {
        return {.duration = 0.6, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    }
    PaperParams close_params() {
        return {.duration = 0.5, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    }

    void advance_for(PaperCrumple &model, double seconds) {
        const double STEP = 0.01;
        // NOLINTNEXTLINE(bugprone-float-loop-counter): Preserve the exact FP recurrence and final partial step.
        for (double time = 0.0; time + 1e-12 < seconds; time += STEP) {
            model.advance(std::min(STEP, seconds - time));
        }
    }

    SheetFrame frame_of(const PaperCrumple &model, Vec2 size = SIZE) {
        SheetFrame out{};
        model.frame(size, out);
        return out;
    }

    // Largest displayed distance from the centre, in logical px.
    double max_radius_px(const SheetFrame &frame, Vec2 size = SIZE) {
        double best = 0.0;
        for (const Vec2 &point : frame.position) {
            best = std::max(best, std::hypot(point.x * size.x, point.y * size.y));
        }
        return best;
    }

    double max_position_delta(const SheetFrame &first, const SheetFrame &second) {
        double best = 0.0;
        const auto *other = second.position.cbegin();
        for (const Vec2 &point : first.position) {
            best = std::max(best, std::abs(point.x - other->x));
            best = std::max(best, std::abs(point.y - other->y));
            ++other;
        }
        return best;
    }

    bool is_identity(const SheetFrame &frame, double tolerance) {
        std::size_t vertex = 0;
        for (const Vec2 &point : frame.position) {
            const Vec2 TEX_COORD = hyprimate::sheet_uv(vertex);
            if (std::abs(point.x - (TEX_COORD.x - 0.5)) > tolerance ||
                std::abs(point.y - (TEX_COORD.y - 0.5)) > tolerance) {
                return false;
            }
            ++vertex;
        }
        for (const auto &shade : frame.shade) {
            for (const float SHADE : shade) {
                if (SHADE != 0.0F) {
                    return false;
                }
            }
        }
        return true;
    }

    // 1. Open ends at identity.
    void test_open_ends_at_identity() {
        PaperCrumple model;
        model.begin(Transition::Open, open_params(), 7);
        advance_for(model, 1.0);
        CHECK(!model.active());
        CHECK(!model.closed());
        CHECK(model.opacity() == 1.0);
        CHECK(is_identity(frame_of(model), 1e-12));
    }

    // 2. Open starts as a hidden ball at the window's spot.
    void test_open_starts_as_hidden_ball() {
        PaperCrumple model;
        model.begin(Transition::Open, open_params(), 7);
        CHECK(model.active());
        CHECK(model.opacity() == 0.0);
        const double RADIUS = max_radius_px(frame_of(model));
        const double LIMIT = 0.25 * std::sqrt(SIZE.x * SIZE.y);
        std::fprintf(stdout,
            "[measure] check 2: open start max radius %.3f px, limit %.3f px\n",
            RADIUS,
            LIMIT);
        CHECK(RADIUS <= LIMIT);
    }

    // 3. Close finishes; mid-close and mid-open steps behave as specified.
    void test_close_finishes_and_aborts_on_large_step() {
        PaperCrumple close;
        close.begin(Transition::Close, close_params(), 3);
        advance_for(close, 0.6);
        CHECK(close.closed());
        CHECK(!close.active());
        CHECK(close.opacity() == 0.0);

        PaperCrumple mid_close;
        mid_close.begin(Transition::Close, close_params(), 3);
        mid_close.advance(0.3);  // > 0.25 s step aborts: Closing -> Closed
        CHECK(mid_close.closed());
        CHECK(!mid_close.active());

        PaperCrumple mid_open;
        mid_open.begin(Transition::Open, open_params(), 3);
        mid_open.advance(0.3);  // > 0.25 s step aborts: Opening -> Idle
        CHECK(!mid_open.active());
        CHECK(!mid_open.closed());
        CHECK(mid_open.opacity() == 1.0);
    }

    // 4. Closing during an open keeps the geometry continuous.
    void test_continuity_when_closing_during_open() {
        PaperCrumple open;
        open.begin(Transition::Open, open_params(), 1);
        advance_for(open, 0.4 * open_params().duration);
        const SheetFrame BEFORE = frame_of(open);

        open.begin(Transition::Close, close_params(), 99);
        const SheetFrame AFTER = frame_of(open);
        const double DELTA = max_position_delta(BEFORE, AFTER);
        std::fprintf(stdout, "[measure] check 4: open->close continuity max delta %.3e\n", DELTA);
        CHECK(DELTA <= 1e-9);
    }

    // 5. Determinism and variation.
    void test_determinism_and_variation() {
        PaperCrumple same_seed_first;
        PaperCrumple same_seed_second;
        same_seed_first.begin(Transition::Close, close_params(), 42);
        same_seed_second.begin(Transition::Close, close_params(), 42);
        advance_for(same_seed_first, 0.2);
        advance_for(same_seed_second, 0.2);
        CHECK(max_position_delta(frame_of(same_seed_first), frame_of(same_seed_second)) == 0.0);

        PaperParams flat = close_params();
        flat.variation = 0.0;
        PaperCrumple flat_seed_first;
        PaperCrumple flat_seed_second;
        flat_seed_first.begin(Transition::Close, flat, 1);
        flat_seed_second.begin(Transition::Close, flat, 2);
        advance_for(flat_seed_first, 0.2);
        advance_for(flat_seed_second, 0.2);
        CHECK(max_position_delta(frame_of(flat_seed_first), frame_of(flat_seed_second)) == 0.0);

        // With variation, windows differ: some seed among a handful must pick another take or
        // mirroring.
        PaperParams varied = close_params();
        varied.variation = 1.0;
        PaperCrumple varied_seed_first;
        varied_seed_first.begin(Transition::Close, varied, 1);
        advance_for(varied_seed_first, 0.2);
        const SheetFrame FIRST = frame_of(varied_seed_first);
        double spread = 0.0;
        for (std::uint64_t seed = 2; seed <= 8; ++seed) {
            PaperCrumple other;
            other.begin(Transition::Close, varied, seed);
            advance_for(other, 0.2);
            spread = std::max(spread, max_position_delta(FIRST, frame_of(other)));
        }
        std::fprintf(stdout, "[measure] check 5: variation 1 seed spread %.3e\n", spread);
        CHECK(spread > 1e-3);
    }

    // 6. Shading.
    void test_shading() {
        PaperParams unlit = close_params();
        unlit.shading = 0.0;
        PaperCrumple dark;
        dark.begin(Transition::Close, unlit, 5);
        advance_for(dark, 0.25);  // s ~ 0.5
        CHECK(!dark.shaded());
        bool all_zero = true;
        const SheetFrame DARK_FRAME = frame_of(dark);
        for (const auto &tri : DARK_FRAME.shade) {
            for (const float SHADE_COMPONENT : tri) {
                all_zero = all_zero && SHADE_COMPONENT == 0.0F;
            }
        }
        CHECK(all_zero);

        PaperCrumple lit;
        lit.begin(Transition::Close, close_params(), 5);  // shading 0.7
        advance_for(lit, 0.25);
        bool any_non_zero = false;
        bool in_range = true;
        const SheetFrame LIT_FRAME = frame_of(lit);
        for (const auto &tri : LIT_FRAME.shade) {
            for (const float SHADE_VALUE : tri) {
                any_non_zero = any_non_zero || SHADE_VALUE != 0.0F;
                in_range = in_range && SHADE_VALUE >= -1.0F && SHADE_VALUE <= 1.0F;
            }
        }
        CHECK(any_non_zero);
        CHECK(in_range);

        PaperParams full = close_params();
        full.shading = 1.0;
        PaperCrumple full_lit;
        full_lit.begin(Transition::Close, full, 5);
        advance_for(full_lit, 0.25);
        bool full_in_range = true;
        bool full_any = false;
        const SheetFrame FULL_FRAME = frame_of(full_lit);
        for (const auto &tri : FULL_FRAME.shade) {
            for (const float SHADE : tri) {
                full_in_range = full_in_range && SHADE >= -1.0F && SHADE <= 1.0F;
                full_any = full_any || SHADE != 0.0F;
            }
        }
        CHECK(full_any);
        CHECK(full_in_range);
    }

    // 7. Crumpliness: higher crumpliness gives a smaller ball.
    void test_crumpliness_shrinks_ball() {
        PaperParams loose;
        loose.duration = 1.0;
        loose.crumpliness = 0.0;
        PaperParams tight = loose;
        tight.crumpliness = 1.0;

        PaperCrumple loose_paper;
        PaperCrumple tight_paper;
        loose_paper.begin(Transition::Close, loose, 11);
        tight_paper.begin(Transition::Close, tight, 11);
        advance_for(loose_paper, 0.75);  // s ~ 0.75
        advance_for(tight_paper, 0.75);
        const double LOOSE_RADIUS = max_radius_px(frame_of(loose_paper));
        const double TIGHT_RADIUS = max_radius_px(frame_of(tight_paper));
        std::fprintf(stdout,
            "[measure] check 7: s=0.75 extents radius crumpliness 0 %.3f px, 1 %.3f px\n",
            LOOSE_RADIUS,
            TIGHT_RADIUS);
        CHECK(TIGHT_RADIUS < LOOSE_RADIUS);
    }

    // 8. Invalid params leave the model inactive.
    void test_invalid_params() {
        PaperParams nan_shading = open_params();
        nan_shading.shading = std::numeric_limits<double>::quiet_NaN();
        PaperCrumple invalid_shading;
        invalid_shading.begin(Transition::Open, nan_shading, 1);
        CHECK(!invalid_shading.active());
        CHECK(!invalid_shading.closed());

        PaperParams zero_duration = close_params();
        zero_duration.duration = 0.0;
        PaperCrumple zero_duration_paper;
        zero_duration_paper.begin(Transition::Close, zero_duration, 1);
        CHECK(!zero_duration_paper.active());
        CHECK(!zero_duration_paper.closed());

        PaperParams nan_duration = close_params();
        nan_duration.duration = std::numeric_limits<double>::quiet_NaN();
        PaperCrumple invalid_duration;
        invalid_duration.begin(Transition::Close, nan_duration, 1);
        CHECK(!invalid_duration.active());
    }

    // 9. A close starts from the window's own rest shape whatever its aspect: the baked take is
    // stretched (and swapped for portrait windows) so its flat sheet lands exactly on the window,
    // with no jump on the first frame.
    void test_close_starts_at_rest_for_any_aspect() {
        for (const Vec2 SIZE : {
                 Vec2{.x = 1000.0, .y = 700.0},
                 Vec2{.x = 600.0, .y = 1000.0},
                 Vec2{.x = 3000.0, .y = 800.0},
                 Vec2{.x = 900.0, .y = 900.0},
             }) {
            for (std::uint64_t seed : {1ULL, 2ULL, 3ULL, 4ULL}) {
                PaperParams params = close_params();
                params.variation = 1.0;  // exercise mirroring and every take
                PaperCrumple model;
                model.begin(Transition::Close, params, seed);
                model.advance(1e-4);
                const SheetFrame FRAME = frame_of(model, SIZE);
                double worst = 0.0;
                std::size_t vertex = 0;
                for (const Vec2 &point : FRAME.position) {
                    const Vec2 TEX_COORD = hyprimate::sheet_uv(vertex);
                    worst = std::max({
                        worst,
                        std::abs(point.x - (TEX_COORD.x - 0.5)) * SIZE.x,
                        std::abs(point.y - (TEX_COORD.y - 0.5)) * SIZE.y,
                    });
                    ++vertex;
                }
                CHECK(worst < 1.0);  // under a pixel
            }
        }
    }

    // 10. A compositor stall (a long but not aborting step) delays the crumple rather than skipping
    // part of it: a 0.2 s step plays like a single 1/30 s frame.
    void test_stall_delays_instead_of_skipping() {
        PaperCrumple stalled;
        PaperCrumple smooth;
        stalled.begin(Transition::Close, close_params(), 7);
        smooth.begin(Transition::Close, close_params(), 7);
        stalled.advance(0.2);
        smooth.advance(1.0 / 30.0);
        CHECK(stalled.active());
        CHECK(max_position_delta(frame_of(stalled), frame_of(smooth)) == 0.0);
    }

    // 11. Along a real take's playback, frame.order is back to front by (mean world z, triangle
    // id), as a fresh sort computes it from the same world data. One SheetFrame carries each order
    // into the next frame, so every projection seeds from the previous one: this checks the seeded
    // sort never drifts from a fresh sort. Each take plays forward (open) and back (close).
    void test_order_matches_fresh_sort_along_playback() {
        using hyprimate::PAPER_TRIANGLES;
        const int FRAMES = hyprimate::paper_take_frames();
        SheetFrame out{};
        for (int take = 0; take < hyprimate::paper_take_count(); ++take) {
            std::vector<int> path;
            for (int frame_index = 0; frame_index < FRAMES; ++frame_index) {
                path.push_back(frame_index);
            }
            for (int reverse_frame = FRAMES - 1; reverse_frame >= 0; --reverse_frame) {
                path.push_back(reverse_frame);
            }
            for (const int FRAME_INDEX : path) {
                hyprimate::SheetGrid world{};
                std::size_t vertex = 0;
                for (auto &point : world) {
                    std::array<double, 3> position{};
                    hyprimate::paper_take_vertex(take, FRAME_INDEX, vertex, position);
                    point = {
                        .x = std::get<0>(position),
                        .y = std::get<1>(position),
                        .z = std::get<2>(position),
                    };
                    ++vertex;
                }
                hyprimate::project_sheet(
                    world, SIZE, 0.7, hyprimate::SheetLighting::PaperCreases, out);

                // Independent recomputation: mean z per triangle with the same arithmetic, then a
                // fresh sort of (z, t) pairs.
                std::vector<std::pair<double, std::uint16_t>> keyed;
                keyed.reserve(PAPER_TRIANGLES);
                for (std::size_t triangle_index = 0; triangle_index < PAPER_TRIANGLES;
                    ++triangle_index) {
                    const auto &corners = hyprimate::sheet_triangle(triangle_index);
                    const double DEPTH =
                        (world[std::get<0>(corners)].z + world[std::get<1>(corners)].z +
                            world[std::get<2>(corners)].z) /
                        3.0;
                    keyed.emplace_back(DEPTH, static_cast<std::uint16_t>(triangle_index));
                }
                std::ranges::sort(keyed);

                bool same = true;
                auto expected = keyed.cbegin();
                for (const auto TRIANGLE : out.order) {
                    same = same && TRIANGLE == expected->second;
                    ++expected;
                }
                CHECK(same);
                if (!same) {
                    std::printf("order mismatch: take %d frame %d\n", take, FRAME_INDEX);
                    return;
                }
            }
        }
    }

}  // namespace

int main() {
    test_open_ends_at_identity();
    test_open_starts_as_hidden_ball();
    test_close_finishes_and_aborts_on_large_step();
    test_continuity_when_closing_during_open();
    test_determinism_and_variation();
    test_shading();
    test_crumpliness_shrinks_ball();
    test_invalid_params();
    test_close_starts_at_rest_for_any_aspect();
    test_stall_delays_instead_of_skipping();
    test_order_matches_fresh_sort_along_playback();
    return g_failures == 0 ? 0 : 1;
}
