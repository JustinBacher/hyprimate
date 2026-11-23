#include <array>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"
#include "workspace/planet_hop/PlanetSpring.hpp"

int main() {
    using hyprimate::workspace::Direction;
    using hyprimate::workspace::Transition;
    using hyprimate::workspace::planet_hop::spring_curve;
    // Damping below, at and above critical. No premature landing followed by a
    // new departure; a redraw samples the same position irrespective of cadence.
    constexpr double SECONDS = .75;
    for (const float DAMPING : std::array{0.F, 8.F, 20.F, 40.F}) {
        const auto CURVE = spring_curve({.stiffness = 100, .damping = DAMPING, .mass = 1}, SECONDS);
        CHECK(CURVE && CURVE(0) == 0 && CURVE(1) == 1);
        if (!CURVE) {
            continue;
        }
        Transition turn;
        CHECK(turn.begin(Direction::Backward, SECONDS, CURVE));
        for (int frame = 1; frame < 96; ++frame) {
            const double BEFORE = turn.progress();
            turn.advance(SECONDS / 96);
            CHECK(turn.active());
            CHECK(turn.progress() >= BEFORE && turn.progress() < 1);
            CHECK(std::abs(turn.progress() - CURVE(frame / 96.0)) < 1e-6);
        }
        turn.advance(SECONDS / 96);
        CHECK(!turn.active() && turn.progress() == 1);
        CHECK(turn.begin(Direction::Forward, SECONDS, CURVE));
        turn.advance(.125);
        const double OUTBOUND = turn.progress();
        turn.retarget(false, SECONDS, turn.motion(), [](double position, bool destination) {
            return hyprimate::workspace::planet_hop::remaining_travel(position, destination, true);
        });
        turn.advance(.001);
        CHECK(turn.progress() >= OUTBOUND && turn.progress() - OUTBOUND < .01);
        turn.advance(.25);
        CHECK(!turn.active() && turn.progress() == 0 && turn.velocity() == 0);
    }
    // Undamped analytic first lobe (1-cos(pi*t))/2. Detect an accidental
    // linear mapping or a clamp of the native overshoot at the destination.
    const auto UNDAMPED = spring_curve({.stiffness = 100, .damping = 0, .mass = 1}, SECONDS);
    CHECK(std::abs(UNDAMPED(.25) - .1464466) < 1e-5);
    const auto HEAVY = spring_curve({.stiffness = 100, .damping = 40, .mass = 4}, SECONDS);
    const auto LIGHT = spring_curve({.stiffness = 100, .damping = 40, .mass = 1}, SECONDS);
    CHECK(std::abs(LIGHT(.25) - HEAVY(.25)) > .04);
    const auto LONG = spring_curve({.stiffness = 100, .damping = 20, .mass = 1}, 10);
    CHECK(LONG(.9) < LONG(.99) && LONG(.99) < 1);
    constexpr double TINY = std::numeric_limits<float>::denorm_min() / 1000.0;
    constexpr double HUGE = std::numeric_limits<float>::max() / 1000.0;
    for (const float DAMPING : std::array{0.F, 20.F, 40.F}) {
        const auto SHORT = spring_curve({.stiffness = 100, .damping = DAMPING, .mass = 1}, TINY);
        CHECK(SHORT);
        if (SHORT) {
            CHECK(SHORT(0) == 0 && SHORT(1) == 1);
            CHECK(std::abs(SHORT(.5) - .25) < 1e-6);
        }
        const auto EXTENDED = spring_curve({.stiffness = 100, .damping = DAMPING, .mass = 1}, HUGE);
        CHECK(EXTENDED);
        if (EXTENDED) {
            CHECK(EXTENDED(.9) < EXTENDED(.99) && EXTENDED(.99) < 1);
            CHECK(EXTENDED(1) == 1);
        }
    }
    return g_failures == 0 ? 0 : 1;
}
