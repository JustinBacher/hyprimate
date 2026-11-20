#include <algorithm>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "animations/burn/BurnDynamics.hpp"

using namespace hyprimate;
namespace {
    constexpr Box CAPTURE{100, 200, 800, 600};
    void advance(BurnSimulation &simulation, double seconds) {
        for (double time = 0; time < seconds - 1e-9; time += .01) simulation.advance(.01, CAPTURE);
    }
    bool contains(Box bounds, Vec2 point, double radius) {
        return bounds.x <= point.x - radius && bounds.y <= point.y - radius &&
               bounds.x + bounds.w >= point.x + radius && bounds.y + bounds.h >= point.y + radius;
    }
    void timing_and_interruption() {
        BurnParams params;
        params.duration = 1;
        params.ember_density = params.fragment_density = 0;
        BurnSimulation simulation;
        simulation.open(CAPTURE, params, 43);
        CHECK(simulation.front().progress == 0);
        advance(simulation, .3);
        const double VISIBLE = simulation.front().progress;
        CHECK(std::abs(VISIBLE - .3) < 1e-8);
        simulation.close(CAPTURE, params, 999);
        CHECK(simulation.front().closing);
        CHECK(simulation.front().seed == 43);
        CHECK(simulation.front().progress == VISIBLE);
        advance(simulation, .1);
        CHECK(std::abs(simulation.front().progress - .2) < 1e-8);
        advance(simulation, .3);
        CHECK(simulation.front().progress == 0);
        CHECK(!simulation.active());
        simulation.open(CAPTURE, params, 43);
        advance(simulation, 1.1);
        CHECK(simulation.front().progress == 1);
        CHECK(!simulation.active());
        simulation.close(CAPTURE, params, 43);
        advance(simulation, .5);
        CHECK(std::abs(simulation.front().progress - .5) < 1e-8);
    }
    void arrival_model() {
        for (std::uint64_t seed : {0ULL, 42ULL, 4095ULL}) {
            CHECK(burn_arrival({.5, .5}, seed) == 0);
            CHECK(burn_arrival({0, .2}, seed, 0) == 1);
            CHECK(burn_arrival({1, .8}, seed, 0) == 1);
            CHECK(burn_arrival({.3, 0}, seed, 0) == 1);
            CHECK(burn_arrival({.7, 1}, seed, 0) == 1);
            CHECK(burn_arrival({0, .2}, seed, 1) < 1);
            CHECK(burn_arrival({0, .2}, seed, 1) != burn_arrival({0, .8}, seed, 1));
            double previous = 0;
            for (unsigned i = 0; i <= 100; ++i) {
                const double ARRIVAL = burn_arrival({.5 + .005 * i, .5 + .002 * i}, seed);
                CHECK(ARRIVAL >= previous);
                CHECK(ARRIVAL >= 0 && ARRIVAL <= 1);
                previous = ARRIVAL;
            }
        }
    }
    void fragments_and_bounds() {
        BurnSimulation simulation;
        BurnParams params;
        params.duration = .7;
        params.fragment_density = 2;
        params.ember_density = 2;
        params.direction = BurnDirection::Wind;
        params.wind_strength = 500;
        simulation.close(CAPTURE, params, 42);
        bool split = false, ember = false, ash = false, detached = false;
        for (unsigned frame = 0; frame < 300; ++frame) {
            simulation.advance(.01, {9000, 9000, 10, 10});
            CHECK(simulation.front().capture.x == CAPTURE.x);
            CHECK(simulation.fragments().size() <= BurnSimulation::MAX_FRAGMENTS);
            CHECK(simulation.particles().size() <= BurnSimulation::MAX_PARTICLES);
            const Box BOUNDS = simulation.bounds();
            for (const auto &fragment : simulation.fragments()) {
                CHECK(fragment.uv.x >= 0 && fragment.uv.y >= 0);
                CHECK(fragment.uv.x + fragment.uv.w <= 1.0000001);
                CHECK(fragment.uv.y + fragment.uv.h <= 1.0000001);
                CHECK(fragment.opacity >= 0 && fragment.opacity <= 1);
                CHECK(contains(
                    BOUNDS, fragment.position, std::hypot(fragment.size.x, fragment.size.y) * .5));
                split |= fragment.layer > 0;
                detached |= fragment.age > .1 && std::abs(fragment.rotation.x) > .01;
            }
            for (const auto &particle : simulation.particles()) {
                CHECK(contains(BOUNDS, particle.position, particle.size));
                ember |= particle.ember;
                ash |= !particle.ember;
            }
        }
        CHECK(split && ember && ash && detached);
        CHECK(!simulation.active());
    }
    void deterministic_and_adaptive() {
        BurnSimulation first, second, small;
        const BurnParams params;
        first.close(CAPTURE, params, 88);
        second.close(CAPTURE, params, 88);
        small.close({0, 0, 80, 60}, params, 88);
        advance(first, .25);
        advance(second, .25);
        advance(small, .25);
        CHECK(first.particles().size() > small.particles().size());
        CHECK(first.fragments().size() == second.fragments().size());
        CHECK(first.particles().size() == second.particles().size());
        for (std::size_t i = 0; i < first.fragments().size(); ++i) {
            CHECK(first.fragments()[i].position.x == second.fragments()[i].position.x);
            CHECK(first.fragments()[i].uv.x == second.fragments()[i].uv.x);
        }
        const double BEFORE = first.front().progress;
        first.advance(std::numeric_limits<double>::quiet_NaN(), CAPTURE);
        CHECK(first.front().progress == BEFORE);
        first.advance(1000, CAPTURE);
        CHECK(BEFORE - first.front().progress <= STALL_SECONDS / params.duration + 1e-8);
        first.reset();
        CHECK(!first.initialized() && !first.active());
        CHECK(first.fragments().empty() && first.particles().empty());
    }
    void directional_forces() {
        BurnParams params;
        params.duration = .5;
        params.fragment_density = 0;
        params.wind_strength = 0;
        params.gravity_strength = 1000;
        BurnSimulation fall, rise;
        params.direction = BurnDirection::Fall;
        fall.close(CAPTURE, params, 99);
        params.direction = BurnDirection::Rise;
        rise.close(CAPTURE, params, 99);
        advance(fall, .3);
        advance(rise, .3);
        CHECK(!fall.particles().empty());
        CHECK(fall.particles().size() == rise.particles().size());
        for (std::size_t i = 0; i < fall.particles().size(); ++i) {
            CHECK(fall.particles()[i].position.y >= rise.particles()[i].position.y);
        }
        // Reversing a horizontal wind must carry the same patches to opposite
        // sides, without changing their source pixels or the combustion clock.
        params.direction = BurnDirection::Wind;
        params.fragment_density = 1;
        params.wind_strength = 460;
        BurnSimulation east, west;
        east.close(CAPTURE, params, 99);
        params.wind_direction = 180;
        west.close(CAPTURE, params, 99);
        advance(east, .35);
        advance(west, .35);
        CHECK(east.front().progress == west.front().progress);
        CHECK(east.fragments().size() == west.fragments().size());
        bool wind_carried_print = false;
        for (std::size_t i = 0; i < std::min(east.fragments().size(), west.fragments().size());
            ++i) {
            const auto &a = east.fragments()[i];
            const auto &b = west.fragments()[i];
            CHECK(a.uv.x == b.uv.x && a.uv.y == b.uv.y && a.uv.w == b.uv.w);
            wind_carried_print |= a.position.x - b.position.x > 30;
        }
        CHECK(wind_carried_print);
        params.consume = true;
        params.fragment_density = 2;
        BurnSimulation consumed;
        consumed.close(CAPTURE, params, 99);
        advance(consumed, .3);
        CHECK(!consumed.fragments().empty());
        for (const auto &fragment : consumed.fragments()) {
            CHECK(fragment.size.x < 14 && fragment.size.y < 14);
        }
        CHECK(!consumed.particles().empty());
    }
}  // namespace
int main() {
    timing_and_interruption();
    arrival_model();
    fragments_and_bounds();
    deterministic_and_adaptive();
    directional_forces();
    return g_failures == 0 ? 0 : 1;
}
