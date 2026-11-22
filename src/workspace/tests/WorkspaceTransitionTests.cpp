#include <array>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceTransition.hpp"

using hyprimate::workspace::Direction;
using hyprimate::workspace::Transition;

namespace {
    void positive_durations_are_not_clamped() {
        constexpr double SHORTEST = std::numeric_limits<float>::denorm_min() / 1000.0;
        constexpr double LONGEST = std::numeric_limits<float>::max() / 1000.0;
        for (const double SECONDS : {SHORTEST, LONGEST}) {
            Transition motion;
            CHECK(motion.begin(Direction::Forward, SECONDS, [](double time) { return time; }));
            motion.advance(SECONDS * .5);
            CHECK(motion.active() && motion.progress() == .5);
            motion.advance(SECONDS * .5);
            CHECK(!motion.active() && motion.progress() == 1.0);
        }
    }

    void short_return_uses_travel_distance() {
        Transition motion;
        CHECK(motion.begin(Direction::Forward, .8, [](double time) { return time; }));
        motion.advance(.0048);
        CHECK(std::abs(motion.progress() - .006) < 1e-12);
        motion.retarget(false, .8, motion.motion());
        motion.advance(.0047);
        CHECK(motion.active());
        motion.advance(.0002);
        CHECK(!motion.active());
        CHECK(motion.progress() == 0);
    }

    void linear_returns_scale_with_angular_displacement() {
        constexpr std::array POSITIONS{0., .001, .006, .05, .25, .50, .90, .95, .99, 1.};
        constexpr std::array SECONDS{0., .0008, .0048, .04, .20, .40, .72, .76, .792, .80};
        for (std::size_t i = 0; i < POSITIONS.size(); ++i) {
            Transition motion;
            CHECK(motion.begin(Direction::Backward, .8, [](double time) { return time; }));
            if (SECONDS[i] != 0) {
                motion.advance(SECONDS[i]);
            }
            const auto OUTBOUND = motion.motion();
            CHECK(std::abs(OUTBOUND.position - POSITIONS[i]) < 1e-12);
            motion.retarget(false, .8, OUTBOUND);
            CHECK(motion.motion().position == OUTBOUND.position);
            CHECK(motion.motion().velocity == OUTBOUND.velocity);
            CHECK(motion.direction() == Direction::Backward);
            if (SECONDS[i] == 0) {
                CHECK(!motion.active());
                continue;
            }
            constexpr int FRAMES = 128;
            double previous = OUTBOUND.position;
            bool heading_back = false;
            for (int frame = 1; frame < FRAMES; ++frame) {
                motion.advance(SECONDS[i] / FRAMES);
                const double POSITION = motion.progress();
                CHECK(motion.active() && POSITION > 0 && POSITION <= 1);
                CHECK(OUTBOUND.position == 1 || POSITION < 1);  // No wrong-endpoint flash.
                CHECK(!heading_back || POSITION <= previous);
                heading_back = heading_back || POSITION < previous;
                previous = POSITION;
            }
            CHECK(heading_back);
            motion.advance((SECONDS[i] / FRAMES) + 1e-12);
            CHECK(!motion.active() && motion.progress() == 0 && motion.velocity() == 0);
        }
    }

    void eased_return_uses_visual_not_elapsed_progress() {
        Transition motion;
        CHECK(motion.begin(Direction::Forward, .8, [](double time) { return time * time; }));
        motion.advance(.008);
        CHECK(std::abs(motion.progress() - .0001) < 1e-12);
        motion.retarget(false, .8, motion.motion());
        motion.advance(.000079);
        CHECK(motion.active() && motion.progress() > 0);
        motion.advance(.000002);
        CHECK(!motion.active() && motion.progress() == 0);
    }

    void second_reversal_preserves_motion_in_both_directions() {
        Transition motion;
        CHECK(motion.begin(Direction::Forward, .8, [](double time) { return time; }));
        motion.advance(.2);
        motion.retarget(false, .8, motion.motion());
        motion.advance(.08);
        const auto RETURN = motion.motion();
        CHECK(RETURN.position > 0 && RETURN.velocity < 0);
        motion.retarget(true, .8, RETURN);
        CHECK(motion.motion().position == RETURN.position && motion.velocity() == RETURN.velocity);
        constexpr double STEP = 1e-7;
        motion.advance(STEP);
        CHECK(std::abs(((motion.progress() - RETURN.position) / STEP) - RETURN.velocity) < .00001);
        motion.advance(.2);
        const auto FORWARD = motion.motion();
        CHECK(FORWARD.velocity > 0);
        motion.retarget(false, .8, FORWARD);
        motion.advance(STEP);
        CHECK(
            std::abs(((motion.progress() - FORWARD.position) / STEP) - FORWARD.velocity) < .00001);
        motion.advance(.8);
        CHECK(!motion.active() && motion.progress() == 0);
    }

    void overshooting_configuration_does_not_land_early_on_return() {
        Transition motion;
        CHECK(motion.begin(
            Direction::Forward, .8, [](double time) { return time + (3 * time * (1 - time)); }));
        motion.advance(.08);
        motion.retarget(false, .8, motion.motion());
        motion.advance(.18);
        CHECK(motion.active() && motion.progress() > 0);
        motion.advance(.12);
        CHECK(!motion.active() && motion.progress() == 0);
    }

}  // namespace

int main() {
    positive_durations_are_not_clamped();
    short_return_uses_travel_distance();
    linear_returns_scale_with_angular_displacement();
    eased_return_uses_visual_not_elapsed_progress();
    second_reversal_preserves_motion_in_both_directions();
    overshooting_configuration_does_not_land_early_on_return();
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
    uniform.retarget(false, 1.0, uniform.motion());
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
    first.retarget(false, 0.6, first.motion());
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
    first.retarget(false, 0.6, first.motion());
    first.advance(0.2);
    const double REVERSED_POSITION = first.progress();
    const double REVERSED_VELOCITY = first.velocity();
    first.retarget(true, 0.6, first.motion());
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
    first.advance(0.32);
    CHECK(first.active());
    CHECK(first.progress() > 0 && first.progress() < 1);
    return g_failures == 0 ? 0 : 1;
}
