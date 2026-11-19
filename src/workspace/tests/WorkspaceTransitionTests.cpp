#include <cmath>
#include <limits>

#include "Check.hpp"
#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceTransition.hpp"

using hyprimate::workspace::Direction;
using hyprimate::workspace::Transition;

int main() {
    Transition first;
    Transition second;
    const auto LINEAR = [](double time) { return time; };
    const auto QUADRATIC = [](double time) { return time * time; };
    Transition shaped;
    CHECK(shaped.begin(Direction::Forward, 1.0, QUADRATIC));
    shaped.advance(0.25);
    CHECK(std::abs(shaped.progress() - 0.0625) < 1e-6);
    CHECK(shaped.active());
    shaped.advance(0.25);
    CHECK(std::abs(shaped.progress() - 0.25) < 1e-6);
    Transition overshoot;
    CHECK(overshoot.begin(Direction::Forward, 1.0, [](double time) { return 4.0 * time; }));
    overshoot.advance(0.25);
    overshoot.advance(0.1);
    CHECK(overshoot.progress() == 1.0);
    CHECK(overshoot.velocity() == 0.0);
    CHECK(overshoot.active());
    Transition uniform;
    CHECK(uniform.begin(Direction::Forward, 2.0, LINEAR));
    uniform.advance(0.25);
    uniform.advance(0.25);
    CHECK(std::abs(uniform.progress() - 0.25) < 1e-6);
    const double BEFORE_REVERSE = uniform.progress();
    const double BEFORE_VELOCITY = uniform.velocity();
    uniform.retarget(false, 1.0);
    CHECK(uniform.progress() == BEFORE_REVERSE);
    CHECK(uniform.velocity() == BEFORE_VELOCITY);
    uniform.advance(0.001);
    CHECK(uniform.progress() > BEFORE_REVERSE);
    for (int i = 0; i < 200 && uniform.active(); ++i) {
        uniform.advance(0.01);
        CHECK(uniform.progress() >= 0.0 && uniform.progress() <= 1.0);
    }
    CHECK(uniform.progress() == 0.0);
    CHECK(!first.active());
    CHECK(first.begin(Direction::Forward, 0.6));
    CHECK(second.begin(Direction::Backward, 0.6));
    first.advance(0.2);
    CHECK(first.progress() > 0.0 && first.progress() < 1.0);
    CHECK(first.velocity() > 0.0);
    CHECK(second.progress() == 0.0);
    const double POSITION = first.progress();
    const double VELOCITY = first.velocity();
    first.retarget(false, 0.6);
    CHECK(first.progress() == POSITION);
    CHECK(first.velocity() == VELOCITY);
    first.advance(0.001);
    CHECK(first.progress() > POSITION);
    for (int i = 0; i < 100 && first.active(); ++i) {
        first.advance(0.01);
        CHECK(first.progress() >= 0.0 && first.progress() <= 1.0);
    }
    CHECK(!first.active());
    CHECK(first.progress() == 0.0);
    CHECK(first.velocity() == 0.0);
    CHECK(second.active());

    CHECK(first.begin(Direction::Forward, 0.6));
    first.advance(0.2);
    first.retarget(false, 0.6);
    first.advance(0.2);
    const double REVERSED_POSITION = first.progress();
    const double REVERSED_VELOCITY = first.velocity();
    first.retarget(true, 0.6);
    CHECK(first.progress() == REVERSED_POSITION);
    CHECK(first.velocity() == REVERSED_VELOCITY);
    for (int i = 0; i < 100 && first.active(); ++i) {
        first.advance(0.01);
        CHECK(first.progress() >= 0.0 && first.progress() <= 1.0);
    }
    CHECK(first.progress() == 1.0);
    CHECK(!first.begin(Direction::Forward, 0.0));
    CHECK(!first.begin(Direction::Forward, std::numeric_limits<double>::quiet_NaN()));
    CHECK(first.begin(Direction::Forward, 1.0));
    first.advance(std::numeric_limits<double>::infinity());
    CHECK(!first.active());
    CHECK(first.progress() == 1.0);
    CHECK(first.begin(Direction::Forward, 1.0));
    first.retarget(false, 1.0);
    first.advance(hyprimate::STALL_SECONDS + 0.01);
    CHECK(!first.active());
    CHECK(first.progress() == 0.0);
    return g_failures == 0 ? 0 : 1;
}
