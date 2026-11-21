#include <limits>

#include "Check.hpp"
#include "WindowTracker.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

int main() {
    Observation before;
    before.monitor = 1;
    before.workspace = 1;
    before.body = {.x = 100, .y = 80, .w = 400, .h = 300};
    before.capture = {.x = 90, .y = 70, .w = 420, .h = 320};
    auto after = before;
    CHECK(!observe_motion(before, after, true, true));

    after.body.x += 30;
    after.capture.x += 30;
    auto motion = observe_motion(before, after, true, true);
    CHECK(motion && motion->translated && !motion->resized);
    CHECK(motion && motion->from_body.x == 100 && motion->body.x == 130);

    after = before;
    after.body.w += 20;
    motion = observe_motion(before, after, true, true);
    CHECK(motion && !motion->translated && motion->resized);
    after = before;
    after.capture.w += 5;
    motion = observe_motion(before, after, true, true);
    CHECK(motion && motion->capture_changed && !motion->translated && !motion->resized);

    after = before;
    after.body.x += 900;
    after.capture.x += 900;
    CHECK(!observe_motion(before, after, true, true));
    const auto SWEEP = observe_motion(before, after, true, true, true);
    CHECK(SWEEP && SWEEP->from_body.x == 100 && SWEEP->body.x == 1000);
    auto baseline = after;
    after.body.x += 30;
    after.capture.x += 30;
    CHECK(observe_motion(baseline, after, true, true));
    after = before;
    after.monitor = 2;
    after.body.x += 20;
    CHECK(!observe_motion(before, after, true, true));
    CHECK(!observe_motion(before, after, true, true, true));
    after.monitor = 1;
    after.workspace = 2;
    CHECK(!observe_motion(before, after, true, true));
    CHECK(!observe_motion(before, after, true, true, true));
    after = before;
    after.body.w = 0;
    CHECK(!observe_motion(before, after, true, true));
    after = before;
    after.capture.w = 0;
    CHECK(!observe_motion(before, after, true, true));

    after = before;
    after.workspace_moving = true;
    after.workspace_offset = {.x = 3000, .y = 0};
    motion = observe_motion(before, after, true, true);
    CHECK(motion && motion->translated && motion->workspace_moving);
    CHECK(motion && motion->body.x - motion->from_body.x == 3000);
    CHECK(!observe_motion(before, after, true, false));
    CHECK(!observe_motion(before, after, false, false));
    after.body.x += 20;
    after.capture.x += 20;
    motion = observe_motion(before, after, false, false);
    CHECK(motion && motion->body.x - motion->from_body.x == 20 && !motion->workspace_moving);
    baseline = after;
    after.workspace_offset = {};
    motion = observe_motion(baseline, after, true, true);
    CHECK(motion && motion->body.x - motion->from_body.x == -3000);

    after = before;
    after.body.x += 20;
    after.workspace_offset.y = std::numeric_limits<double>::quiet_NaN();
    CHECK(!observe_motion(before, after, true, true));
    CHECK(!observe_motion(after, before, true, true));
    CHECK(observe_motion(before, after, false, false));

    // The source box and offset are finite individually, but the translated
    // capture is not. It must not reach a style as an ordinary movement.
    after.capture.x = std::numeric_limits<double>::max();
    after.workspace_offset = {.x = std::numeric_limits<double>::max(), .y = 0};
    CHECK(!observe_motion(before, after, true, true));

    return g_failures == 0 ? 0 : 1;
}
