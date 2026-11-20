#include <chrono>
#include <thread>

#include "Check.hpp"
#include "animations/paper/PaperStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

static_assert(StyleCore<SPaperModule>);

int main() {
    SPaperResources resources;
    resources.prepared.open = {
        .duration = 0.6, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    resources.prepared.close = {
        .duration = 0.5, .crumpliness = 0.3, .shading = 0.0, .variation = 1.0};
    auto state = SPaperModule::start(resources, 0);
    Place place{};
    place.capture = {.x = 100, .y = 200, .w = 1000, .h = 700};
    place.seed = 7;
    place.goal_body = place.rest_capture = place.capture;
    Effects effects;
    CHECK(SPaperModule::open(state, place, resources, effects));
    CHECK(SPaperModule::moves(Motion{.translated = true}, resources));
    CHECK(SPaperModule::moves(Motion{.resized = true}, resources));
    CHECK(!SPaperModule::moves(Motion{.capture_changed = true}, resources));
    Step step{};
    step.elapsed = 1.0 / 120.0;
    step.capture = place.capture;
    step.open_enabled = true;
    // Wait for an actual visible, partially released sheet, not just a running handle.
    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (
        state.display.opacity >= 1 || state.display.opacity <= 0 || state.display.faces.empty()) {
        if (std::chrono::steady_clock::now() >= DEADLINE ||
            !SPaperModule::advance(state, step, resources, effects).running) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!state.display.faces.empty());
    const auto BEFORE = state.display.opacity;
    CHECK(BEFORE > 0 && BEFORE < 1);
    CHECK(SPaperModule::close(state, place, resources) == CloseStart::Owned);
    CHECK(state.display.opacity == BEFORE);
    step.dying = true;
    step.open_enabled = false;
    CHECK(SPaperModule::advance(state, step, resources, effects).running);
    SPaperModule::end(state, resources, End::Interrupted);
    CHECK(!state.run);

    // Movement is independent of map animation. A close during flight must
    // publish the bent starting pose before advancing its own solver.
    auto moving = SPaperModule::start(resources, 0);
    Step drag{};
    drag.capture = drag.body = place.capture;
    drag.movement_enabled = true;
    drag.motion = Motion{.from_body = place.capture,
        .body = place.capture,
        .translated = true,
        .sample_seconds = 1.0 / 60};
    drag.motion->from_body.x -= 28;
    for (int i = 0; i < 12; ++i) {
        CHECK(SPaperModule::advance(moving, drag, resources, effects).running);
        drag.elapsed = 1.0 / 60;
        drag.motion->from_body = drag.body;
        drag.capture.x = drag.body.x += 28;
        drag.motion->body = drag.body;
    }
    CHECK(moving.display.interaction_mesh && moving.display.shadow_strength > 0);
    place.capture = drag.capture;
    CHECK(SPaperModule::close(moving, place, resources) == CloseStart::Owned);
    const auto HANDOFF = moving.closing_from;
    CHECK(HANDOFF && moving.surface().interaction_mesh);
    const auto CLOSE_DEADLINE = Clock::now() + std::chrono::seconds(2);
    const Step FIRST_CLOSE{.capture = place.capture, .dying = true};
    while (moving.closing_from && Clock::now() < CLOSE_DEADLINE) {
        CHECK(SPaperModule::advance(moving, FIRST_CLOSE, resources, effects).running);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!moving.closing_from && !moving.display.points.empty());
    if (HANDOFF) {
        for (const auto &point : moving.display.points) {
            const auto source = PaperInteraction::sample(*HANDOFF, point.uv);
            CHECK(std::abs(source.z - point.position.z) < 1e-7);
        }
    }
    SPaperModule::end(moving, resources, End::Interrupted);

    auto fresh = SPaperModule::start(resources, 0);
    CHECK(SPaperModule::close(fresh, place, resources) == CloseStart::Owned);
    CHECK(fresh.display.opacity == 1 && fresh.display.points.front().uv.x == 0 &&
          fresh.display.points.back().uv.y == 1);
    step.elapsed = STALL_SECONDS + 0.01;
    const auto STALLED = SPaperModule::advance(fresh, step, resources, effects);
    CHECK(!STALLED.running && STALLED.interrupted);
    SPaperModule::end(fresh, resources, End::Interrupted);

    auto invisible = SPaperModule::start(resources, 0);
    CHECK(SPaperModule::open(invisible, place, resources, effects));
    CHECK(SPaperModule::close(invisible, place, resources) == CloseStart::Owned);
    step.elapsed = 0;
    const auto UNSEEN = SPaperModule::advance(invisible, step, resources, effects);
    CHECK(!UNSEEN.running && !UNSEEN.interrupted && invisible.display.faces.empty());
    SPaperModule::end(invisible, resources, End::Completed);
    return g_failures != 0;
}
