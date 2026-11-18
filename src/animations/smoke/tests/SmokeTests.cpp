#include <cmath>
#include <vector>

#include "Check.hpp"
#include "animations/smoke/Smoke.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{.x = 800, .y = 500};

    void advance_for(Smoke &smoke, double seconds, int rate = 120, Vec2 velocity = {}) {
        const int COUNT = static_cast<int>((seconds * rate) + 0.5);
        for (int i = 0; i < COUNT; ++i) {
            smoke.drive({.x = velocity.x / rate, .y = velocity.y / rate});
            smoke.advance(1.0 / rate, SIZE);
        }
    }

    void endpoints_and_interruption() {
        Smoke smoke;
        smoke.seed(42);
        smoke.begin(Transition::Open, 1.1, SIZE);
        const auto INITIAL = smoke.frame(SIZE);
        CHECK(INITIAL.smoke == 1 && INITIAL.presence == 0 && smoke.opening());
        advance_for(smoke, 0.45);
        // Inside opening: a head grows at a point within the window and gathers loose smoke.
        CHECK(smoke.frame(SIZE).transit > 0.9 && smoke.frame(SIZE).cohesion > 0.5);
        CHECK(smoke.frame(SIZE).head_half.x < SIZE.x / 2 && smoke.frame(SIZE).gather == 0);
        const auto BEFORE = smoke.frame(SIZE);
        smoke.begin(Transition::Close, 0.85, SIZE);
        const auto AFTER = smoke.frame(SIZE);
        CHECK(BEFORE.smoke == AFTER.smoke && BEFORE.presence == AFTER.presence);
        CHECK(BEFORE.time == AFTER.time && BEFORE.seed == AFTER.seed);
        advance_for(smoke, 0.5);
        // The interrupting close takes over the opening head and draws the window into it.
        CHECK(smoke.frame(SIZE).attract > 0 && smoke.frame(SIZE).wind != 0);
        advance_for(smoke, 3.0);
        CHECK(smoke.closed() && !smoke.active());
        CHECK(smoke.frame(SIZE).presence == 0 && smoke.frame(SIZE).fade == 0);
        smoke.begin(Transition::Open, 1.1, SIZE);
        advance_for(smoke, 3.0);
        CHECK(!smoke.active() && !smoke.closed());
        CHECK(smoke.frame(SIZE).smoke == 0 && smoke.frame(SIZE).presence == 1);
    }

    // Wake smoke must outlive the reform briefly, then fade out gradually rather than vanish.
    void residual_wake_fades_out() {
        Smoke smoke;
        smoke.seed(17);
        advance_for(smoke, 0.7, 120, {.x = 500, .y = 0});
        CHECK(smoke.frame(SIZE).smoke > 0.3 && smoke.frame(SIZE).fade == 1);
        smoke.stop_motion();
        double previous = 1;
        double largest_step = 0;
        bool faded_while_active = false;
        for (int i = 0; i < 120 * 8 && smoke.active(); ++i) {
            smoke.advance(1.0 / 120, SIZE);
            const double FADE = smoke.frame(SIZE).fade;
            largest_step = std::max(largest_step, previous - FADE);
            faded_while_active = faded_while_active || (FADE > 0 && FADE < 1);
            previous = FADE;
        }
        CHECK(!smoke.active() && previous == 0);
        CHECK(faded_while_active && largest_step < 0.05);
    }

    void substeps_are_rate_independent() {
        Smoke a, b, c;
        a.seed(99);
        b.seed(99);
        c.seed(99);
        advance_for(a, 1.0, 60, {.x = 360, .y = 75});
        advance_for(b, 1.0, 110, {.x = 360, .y = 75});
        advance_for(c, 1.0, 144, {.x = 360, .y = 75});
        const auto FIRST = a.frame(SIZE), SECOND = b.frame(SIZE), THIRD = c.frame(SIZE);
        CHECK(std::abs(FIRST.smoke - SECOND.smoke) < 1e-9);
        CHECK(std::abs(FIRST.smoke - THIRD.smoke) < 1e-9);
        CHECK(std::abs(FIRST.velocity.x - SECOND.velocity.x) < 1e-7);
        CHECK(std::abs(FIRST.position.x - THIRD.position.x) < 1e-6);
        CHECK(std::abs(FIRST.offset.x - SECOND.offset.x) < 1e-7);
    }

    // The fluid is anchored where the window is drawn. Translation the fixed substeps have not
    // consumed yet still moves the frame; otherwise all smoke shakes along the drag whenever
    // display frames fall between substeps.
    void frame_includes_unstepped_translation() {
        Smoke smoke;
        advance_for(smoke, 0.3, 144, {.x = 1800, .y = 0});
        const Vec2 BEFORE = smoke.frame(SIZE).position;
        smoke.drive({.x = 7, .y = -3});
        smoke.advance(0.4 * SUBSTEP_SECONDS, SIZE);
        const Vec2 AFTER = smoke.frame(SIZE).position;
        CHECK(std::abs(AFTER.x - BEFORE.x - 7) < 1e-9 && std::abs(AFTER.y - BEFORE.y + 3) < 1e-9);
    }

    // The canvas must cover the whole fluid, which slides behind moving windows for the wake.
    void bounds_cover_window_and_wake() {
        Smoke smoke;
        advance_for(smoke, 0.8, 120, {.x = 900, .y = 0});
        const Box WAKE = smoke.bounds(SIZE);
        CHECK(is_usable(WAKE));
        CHECK(WAKE.x <= 0 && WAKE.y <= 0 && WAKE.x + WAKE.w >= SIZE.x && WAKE.y + WAKE.h >= SIZE.y);
        CHECK(-WAKE.x > (WAKE.x + WAKE.w - SIZE.x) + 100);
        for (Vec2 size : {Vec2{.x = 20, .y = 2000}, Vec2{.x = 2000, .y = 20}}) {
            const Box BOX = Smoke{}.bounds(size);
            CHECK(is_usable(BOX));
            CHECK(BOX.x < 0 && BOX.y < 0 && BOX.x + BOX.w > size.x && BOX.y + BOX.h > size.y);
        }
    }

    void disabled_movement_and_stop_transition() {
        Smoke smoke;
        SmokeParams params;
        params.strength = 0;
        for (int i = 0; i < 240; ++i) {
            smoke.drive({.x = 2, .y = 0});
            smoke.advance(1.0 / 240, SIZE, params);
        }
        CHECK(smoke.frame(SIZE).smoke == 0);
        smoke.stop_motion();
        smoke.begin(Transition::Close, 0.85, SIZE);
        smoke.stop_motion();
        CHECK(smoke.closing());
        advance_for(smoke, 3.0);
        CHECK(smoke.closed());
    }

    void first_layout_move_retains_visible_carry() {
        Smoke smoke;
        smoke.drive({.x = 120, .y = 0}, true);
        CHECK(smoke.active());
        smoke.advance(1.0 / 120, SIZE);
        CHECK(smoke.frame(SIZE).carry.x < -100);
        advance_for(smoke, 5.0);
        CHECK(!smoke.active());
    }

    // Moving contracts the window into its body; stopping expands it back to the exact window.
    void movement_contracts_and_reforms_the_body() {
        Smoke smoke;
        SmokeParams params;
        params.mass = 0.4;
        params.roundness = 0.6;
        const auto RESTING = smoke_body(smoke.frame(SIZE));
        CHECK(RESTING.half.x == SIZE.x / 2 && RESTING.half.y == SIZE.y / 2 && RESTING.radius == 0);
        for (int i = 0; i < 120; ++i) {
            smoke.drive({.x = 900.0 / 120, .y = 0});
            smoke.advance(1.0 / 120, SIZE, params);
        }
        const auto MOVING = smoke_body(smoke.frame(SIZE));
        CHECK(MOVING.half.x < SIZE.x * 0.3 && MOVING.half.y < SIZE.y * 0.5);
        CHECK(MOVING.radius > 0 && MOVING.stretch.x > 0 && std::abs(MOVING.stretch.y) < 1e-9);
        // Interrupting the reform mid-way continues from the partly expanded body.
        smoke.stop_motion();
        for (int i = 0; i < 24; ++i) {
            smoke.advance(1.0 / 120, SIZE, params);
        }
        const auto PARTIAL = smoke_body(smoke.frame(SIZE));
        CHECK(PARTIAL.half.x > MOVING.half.x && PARTIAL.half.x < SIZE.x / 2);
        smoke.drive({.x = 900.0 / 120, .y = 0});
        smoke.advance(1.0 / 120, SIZE, params);
        CHECK(std::abs(smoke_body(smoke.frame(SIZE)).half.x - PARTIAL.half.x) < 15);
        smoke.stop_motion();
        advance_for(smoke, 6.0);
        const auto REFORMED = smoke_body(smoke.frame(SIZE));
        CHECK(!smoke.active());
        CHECK(REFORMED.half.x == SIZE.x / 2 && REFORMED.half.y == SIZE.y / 2);
        CHECK(REFORMED.radius == 0 && REFORMED.offset.x == 0 && REFORMED.stretch.x == 0);
    }

    void settle_speed_changes_reform_without_changing_entry_or_transitions() {
        Smoke slow, fast;
        SmokeParams slow_params, fast_params;
        slow_params.settle_speed = 0.25;
        fast_params.settle_speed = 2;
        for (int i = 0; i < 120; ++i) {
            slow.drive({.x = 6, .y = 0});
            fast.drive({.x = 6, .y = 0});
            slow.advance(1.0 / 120, SIZE, slow_params);
            fast.advance(1.0 / 120, SIZE, fast_params);
        }
        CHECK(std::abs(slow.frame(SIZE).smoke - fast.frame(SIZE).smoke) < 1e-9);
        CHECK(std::abs(slow.frame(SIZE).mass - fast.frame(SIZE).mass) < 1e-9);
        slow.stop_motion();
        fast.stop_motion();
        for (int i = 0; i < 60; ++i) {
            slow.advance(1.0 / 120, SIZE, slow_params);
            fast.advance(1.0 / 120, SIZE, fast_params);
        }
        CHECK(fast.frame(SIZE).smoke < slow.frame(SIZE).smoke * 0.2);
        CHECK(fast.frame(SIZE).mass < slow.frame(SIZE).mass * 0.2);
        CHECK(smoke_body(fast.frame(SIZE)).half.x > smoke_body(slow.frame(SIZE)).half.x);
        for (int i = 0; i < 120 * 30; ++i) {
            slow.advance(1.0 / 120, SIZE, slow_params);
            fast.advance(1.0 / 120, SIZE, fast_params);
        }
        CHECK(!slow.active() && !fast.active());
        for (const auto DIRECTION : {Transition::Open, Transition::Close}) {
            slow.begin(DIRECTION, 1.4, SIZE);
            fast.begin(DIRECTION, 1.4, SIZE);
            for (int i = 0; i < 120 * 4; ++i) {
                slow.advance(1.0 / 120, SIZE, slow_params);
                fast.advance(1.0 / 120, SIZE, fast_params);
                CHECK(slow.opening() == fast.opening() && slow.closed() == fast.closed());
                CHECK(std::abs(slow.frame(SIZE).smoke - fast.frame(SIZE).smoke) < 1e-9);
                CHECK(std::abs(slow.frame(SIZE).transit - fast.frame(SIZE).transit) < 1e-9);
            }
        }
    }

    // Every edge, growth and shrinkage, including a fixed-centre resize with no translation.
    void resizing_unravels_without_inventing_translation() {
        for (const Vec2 CHANGE : {Vec2{.x = 5, .y = 0}, Vec2{.x = -5, .y = 0},
                 Vec2{.x = 0, .y = 5}, Vec2{.x = 0, .y = -5}}) {
            for (const double ANCHOR : {-0.5, 0.0, 0.5}) {
                Smoke smoke;
                Vec2 size = SIZE;
                const Vec2 TRANSLATION{.x = CHANGE.x * ANCHOR, .y = CHANGE.y * ANCHOR};
                for (int i = 0; i < 60; ++i) {
                    size.x += CHANGE.x;
                    size.y += CHANGE.y;
                    smoke.drive(TRANSLATION, false, CHANGE);
                    CHECK(smoke.active());
                    smoke.advance(1.0 / 120, size);
                }
                const auto FRAME = smoke.frame(size);
                CHECK(FRAME.smoke > 0.3 && FRAME.mass > 0.3);
                CHECK(std::abs(FRAME.position.x - TRANSLATION.x * 60) < 1e-8);
                CHECK(std::abs(FRAME.position.y - TRANSLATION.y * 60) < 1e-8);
                if (ANCHOR == 0) {
                    CHECK(FRAME.velocity.x == 0 && FRAME.velocity.y == 0);
                    CHECK(FRAME.carry.x == 0 && FRAME.carry.y == 0);
                }
                for (int i = 0; i < 120 * 8; ++i) {
                    smoke.advance(1.0 / 120, size);
                }
                CHECK(!smoke.active());
                CHECK(smoke_body(smoke.frame(size)).half.x == size.x / 2);
                CHECK(smoke_body(smoke.frame(size)).half.y == size.y / 2);
            }
        }
        Smoke cancelled;
        cancelled.drive({}, false, {.x = 10, .y = 0});
        cancelled.stop_motion();
        cancelled.advance(1.0 / 120, SIZE);
        CHECK(!cancelled.active() && cancelled.frame(SIZE).smoke == 0);
        Smoke disabled;
        for (int i = 0; i < 120; ++i) {
            disabled.drive({}, false, {.x = 5, .y = 0});
            disabled.advance(1.0 / 120, SIZE, {.strength = 0});
        }
        CHECK(disabled.frame(SIZE).smoke == 0 && disabled.frame(SIZE).mass == 0);
    }

    // Size scales the body without changing its shape; roundness reshapes it at constant area.
    void size_and_roundness_are_independent() {
        SmokeFrame frame;
        frame.size = {.x = 900, .y = 300};
        frame.mass = 1;
        const auto BODY = [&](double size, double roundness) {
            frame.mass_size = size;
            frame.mass_roundness = roundness;
            return smoke_body(frame);
        };
        const auto SQUARE = BODY(1.0, 0.0), SMALL = BODY(0.5, 0.0), ROUND = BODY(1.0, 1.0);
        CHECK(std::abs(SQUARE.half.x - 450) < 1e-9 && std::abs(SQUARE.half.y - 150) < 1e-9);
        CHECK(std::abs(SMALL.half.x / SMALL.half.y - 3.0) < 1e-9);
        CHECK(std::abs(SMALL.half.x - 225) < 1e-9);
        CHECK(std::abs(ROUND.half.x - ROUND.half.y) < 1e-9);
        CHECK(std::abs(ROUND.half.x * ROUND.half.y - 450.0 * 150.0) < 1e-6);
        CHECK(std::abs(ROUND.radius - ROUND.half.x) < 1e-9 && SQUARE.radius < 0.2 * SQUARE.half.y);
    }

    // Head centres sampled through one opening or closing at 120 Hz.
    std::vector<Vec2> head_path(Transition direction, SmokeRoute route, double variation, std::uint64_t seed) {
        Smoke smoke;
        SmokeParams params;
        params.variation = variation;
        smoke.advance(1.0 / 120, SIZE, params);
        smoke.seed(seed);
        smoke.begin(direction, 1.6, SIZE, route);
        std::vector<Vec2> path;
        for (int i = 0; i < 192; ++i) {
            smoke.advance(1.0 / 120, SIZE, params);
            path.push_back(smoke.frame(SIZE).head);
        }
        return path;
    }

    // Closing right: the head gathers on the left edge, somewhere along it, rears back away
    // from the exit before turning toward it, and always leaves through the right screen edge.
    void directional_close_rears_and_exits() {
        const SmokeRoute RIGHT{.edge = true, .direction = {.x = 1, .y = 0}, .reach = 900};
        double lowest = SIZE.y, highest = -SIZE.y;
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            const auto PATH = head_path(Transition::Close, RIGHT, 1.0, seed);
            const Vec2 START = PATH[2];
            CHECK(START.x < -0.3 * SIZE.x && START.x > -SIZE.x / 2);
            CHECK(std::abs(START.y) <= 0.75 * SIZE.y / 2 + 1e-6);
            lowest = std::min(lowest, START.y);
            highest = std::max(highest, START.y);
            double back = START.x;
            for (std::size_t i = 0; i < PATH.size() / 2; ++i) {
                back = std::min(back, PATH[i].x);
            }
            CHECK(back < START.x - 20);
            CHECK(PATH.back().x > RIGHT.reach);
        }
        CHECK(highest - lowest > 0.2 * SIZE.y);
        // Without variation every window gathers at the middle of the edge and moves alike.
        const auto FIRST = head_path(Transition::Close, RIGHT, 0.0, 3);
        const auto SECOND = head_path(Transition::Close, RIGHT, 0.0, 11);
        CHECK(std::abs(FIRST[2].y) < 1e-6);
        for (std::size_t i = 0; i < FIRST.size(); ++i) {
            CHECK(std::abs(FIRST[i].x - SECOND[i].x) < 1e-9 && std::abs(FIRST[i].y - SECOND[i].y) < 1e-9);
        }
    }

    // Opening from the top streams in from the screen edge; inside opening grows from a point
    // within the window and ends as the window.
    void opening_routes() {
        const SmokeRoute TOP{.edge = true, .direction = {.x = 0, .y = -1}, .reach = 600};
        const auto ENTERING = head_path(Transition::Open, TOP, 0.5, 5);
        CHECK(ENTERING[2].y < -590);
        CHECK(std::abs(ENTERING.back().x) < 1 && std::abs(ENTERING.back().y) < 1);
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            const auto GROWING = head_path(Transition::Open, {}, 1.0, seed);
            CHECK(std::abs(GROWING[2].x) < SIZE.x / 2 && std::abs(GROWING[2].y) < SIZE.y / 2);
        }
        Smoke smoke;
        smoke.begin(Transition::Open, 1.4, SIZE);
        smoke.advance(1.0 / 120, SIZE);
        CHECK(smoke.frame(SIZE).head_half.x < 0.05 * SIZE.x);
        advance_for(smoke, 3.0);
        CHECK(smoke.frame(SIZE).transit == 0 && smoke_body(smoke.frame(SIZE)).half.x == SIZE.x / 2);
    }
}  // namespace

int main() {
    endpoints_and_interruption();
    residual_wake_fades_out();
    substeps_are_rate_independent();
    frame_includes_unstepped_translation();
    bounds_cover_window_and_wake();
    disabled_movement_and_stop_transition();
    first_layout_move_retains_visible_carry();
    movement_contracts_and_reforms_the_body();
    settle_speed_changes_reform_without_changing_entry_or_transitions();
    resizing_unravels_without_inventing_translation();
    size_and_roundness_are_independent();
    directional_close_rears_and_exits();
    opening_routes();
    return g_failures == 0 ? 0 : 1;
}
