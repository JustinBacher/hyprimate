#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>

#include "Check.hpp"
#include "FakeWorkspacePort.hpp"
#include "WorkspaceCapturePlanTests.hpp"

using namespace hyprimate::workspace;
using namespace hyprimate::workspace::test;

namespace {
const auto LINEAR = [](double time) { return time; };
const auto SQUARE = [](double time) { return time * time; };

struct Fixture {
    FakeWorkspacePort port;
    WorkspaceTurn<FakeWorkspacePort> turn{port};
    std::shared_ptr<FakeWorkspace> one = std::make_shared<FakeWorkspace>(1);
    std::shared_ptr<FakeWorkspace> two = std::make_shared<FakeWorkspace>(2);
    std::shared_ptr<FakeWorkspace> three = std::make_shared<FakeWorkspace>(3);
    std::shared_ptr<FakeWorkspace> four = std::make_shared<FakeWorkspace>(4);
    FakeWorkspacePort::Composition a = port.composition(one);
    FakeWorkspacePort::Composition b = port.composition(two);
    FakeWorkspacePort::Composition c = port.composition(three);
    FakeWorkspacePort::Composition d = port.composition(four);

    void start() {
        CHECK(turn.stage(a));
        turn.begin(a, b, 1.0, LINEAR);
        CHECK(turn.active());
    }
    void frame(const FakeWorkspacePort::Composition &current, double elapsed) {
        port.advance(elapsed);
        turn.frame(current, elapsed);
    }
    void present() {
        frame(b, 0.0);
        CHECK(turn.drawable());
        turn.presented(true);
    }
};

void identity_and_direction() {
    Fixture fixture;
    const auto SPECIAL = std::make_shared<FakeWorkspace>(99);
    const auto WITH_SPECIAL = fixture.port.composition(fixture.one, SPECIAL);
    CHECK(fixture.port.composition(fixture.one).identity == fixture.a.identity);
    CHECK(WITH_SPECIAL.identity != fixture.a.identity);
    CHECK(fixture.port.composition(fixture.two, SPECIAL).identity != WITH_SPECIAL.identity);
    CHECK(blocks_surface_feedback(CaptureKind::SourceIsolated));
    CHECK(!blocks_surface_feedback(CaptureKind::DestinationLive));
    CHECK(direction_between({}, 1, false, false) == Direction::Forward);
    CHECK(direction_between(2, 1, false, true) == Direction::Forward);
    CHECK(direction_between(1, 2, true, false) == Direction::Backward);
    fixture.start();
    CHECK(fixture.turn.direction() == Direction::Forward);
    fixture.turn.cancel();
    CHECK(fixture.turn.stage(fixture.b));
    fixture.turn.begin(fixture.b, fixture.a, 1.0, LINEAR);
    CHECK(fixture.turn.direction() == Direction::Backward);
    fixture.turn.cancel();
    CHECK(fixture.turn.stage(fixture.a));
    fixture.turn.begin(fixture.a, WITH_SPECIAL, 1.0, LINEAR);
    CHECK(fixture.turn.direction() == Direction::Forward);
    fixture.turn.cancel();
    CHECK(fixture.turn.stage(WITH_SPECIAL));
    fixture.turn.begin(WITH_SPECIAL, fixture.a, 1.0, LINEAR);
    CHECK(fixture.turn.direction() == Direction::Backward);
}

void staging_and_capture_failures() {
    Fixture fixture;
    CHECK(fixture.turn.stage(fixture.a));
    CHECK(fixture.turn.stage(fixture.a));
    CHECK(fixture.port.captures.size() == 1);
    fixture.turn.release_gesture();
    CHECK(fixture.turn.stage(fixture.a));
    CHECK(fixture.port.captures.size() == 2);
    fixture.turn.begin(fixture.a, fixture.b, 1.0, LINEAR);
    CHECK(!fixture.turn.drawable());
    fixture.frame(fixture.b, 0.0);
    CHECK(fixture.turn.drawable());
    CHECK(fixture.port.captures.back().kind == CaptureKind::DestinationLive);
    const auto COUNT = fixture.port.captures.size();
    fixture.frame(fixture.b, 0.0);
    CHECK(fixture.port.captures.size() == COUNT + 1);
    fixture.port.capture_succeeds = false;
    fixture.frame(fixture.b, 0.0);
    CHECK(!fixture.turn.active());
    CHECK(!fixture.turn.stage(fixture.a));
    CHECK(!fixture.turn.active());
    fixture.port.capture_succeeds = true;
    fixture.port.buffers_ready = false;
    CHECK(!fixture.turn.stage(fixture.a));
    fixture.port.buffers_ready = true;
    fixture.start();
    fixture.present();
    fixture.port.capture_succeeds = false;
    CHECK(!fixture.turn.stage(fixture.b));
    CHECK(!fixture.turn.active());
}

void gate_and_unpresented_retarget() {
    Fixture fixture;
    fixture.start();
    fixture.frame(fixture.b, 0.2);
    CHECK(fixture.turn.progress() == 0.0);
    CHECK(fixture.turn.stage(fixture.b));
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    CHECK(fixture.port.source.identity == fixture.a.identity);
    fixture.frame(fixture.c, 0.2);
    CHECK(fixture.turn.progress() == 0.0);
    fixture.turn.presented(true);
    fixture.frame(fixture.c, 0.1);
    CHECK(fixture.turn.progress() > 0.0);
    fixture.turn.cancel();
    fixture.start();
    fixture.turn.begin(fixture.b, fixture.a, 1.0, LINEAR);
    CHECK(!fixture.turn.active());
}

void reversal_and_coalescing() {
    Fixture fixture;
    fixture.start();
    fixture.present();
    fixture.frame(fixture.b, 0.2);
    const double BEFORE = fixture.turn.progress();
    CHECK(fixture.turn.stage(fixture.b));
    fixture.turn.begin(fixture.b, fixture.a, 1.0, LINEAR);
    CHECK(fixture.turn.progress() == BEFORE);
    fixture.frame(fixture.a, 0.01);
    CHECK(fixture.turn.progress() > BEFORE); // velocity survives the reversal
    fixture.turn.begin(fixture.a, fixture.c, 1.0, LINEAR);
    fixture.turn.begin(fixture.c, fixture.d, 1.0, LINEAR);
    for (int i = 0; i < 40 && fixture.port.destination.identity != fixture.d.identity; ++i) {
        fixture.frame(fixture.d, 0.05);
    }
    CHECK(fixture.turn.active());
    CHECK(fixture.port.source.identity == fixture.a.identity);
    CHECK(fixture.port.destination.identity == fixture.d.identity);
    CHECK(fixture.port.swaps == 0); // reversal finishes at the source
}

void returning_to_destination_discards_pending() {
    Fixture fixture;
    fixture.start();
    fixture.present();
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    fixture.turn.begin(fixture.c, fixture.b, 1.0, LINEAR);
    for (int i = 0; i < 30 && fixture.turn.active(); ++i) {
        fixture.frame(fixture.b, 0.05);
    }
    CHECK(!fixture.turn.active());
    CHECK(fixture.port.swaps == 0);
}

void returning_to_current_target_preserves_timeline() {
    Fixture fixture;
    Fixture uninterrupted;
    fixture.start();
    uninterrupted.start();
    fixture.present();
    uninterrupted.present();
    fixture.frame(fixture.b, .2);
    uninterrupted.frame(uninterrupted.b, .2);
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    fixture.turn.begin(fixture.c, fixture.b, 1.0, LINEAR);
    for (int i = 0; i < 20 && uninterrupted.turn.active(); ++i) {
        fixture.frame(fixture.b, .05);
        uninterrupted.frame(uninterrupted.b, .05);
        CHECK(std::abs(fixture.turn.progress() - uninterrupted.turn.progress()) < 1e-9);
        CHECK(fixture.turn.active() == uninterrupted.turn.active());
    }
    CHECK(!fixture.turn.active());
    CHECK(fixture.port.swaps == 0);
}

void destination_swap_and_stall() {
    Fixture fixture;
    fixture.port.next_wallpaper = {.texture = 10, .generation = 1};
    fixture.port.frame_gap = 1.0;
    fixture.start();
    fixture.port.next_wallpaper = {.texture = 20, .generation = 2};
    fixture.present();
    fixture.frame(fixture.b, 0.32);  // A slow refresh inside a one-second flight.
    CHECK(fixture.turn.active());
    CHECK(std::abs(fixture.turn.progress() - 0.32) < 1e-6);
    fixture.port.next_wallpaper = {.texture = 30, .generation = 3};
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    for (int i = 0; i < 30 && fixture.port.swaps == 0; ++i) {
        fixture.frame(fixture.c, 0.05);
    }
    CHECK(fixture.port.swaps == 1);
    CHECK(fixture.port.source.identity == fixture.b.identity);
    CHECK(fixture.port.destination.identity == fixture.c.identity);
    CHECK(fixture.port.source.wallpaper.texture == 20);
    CHECK(fixture.port.source.wallpaper.generation == 2);
    CHECK(fixture.port.destination.wallpaper.texture == 30);
    CHECK(fixture.port.destination.wallpaper.generation == 3);
    CHECK(fixture.turn.active());
    fixture.turn.presented(true);
    fixture.turn.begin(fixture.c, fixture.d, 1.0, LINEAR);
    fixture.frame(fixture.d, hyprimate::STALL_SECONDS + 1.0);
    CHECK(!fixture.turn.active());
}

void adaptive_capture_and_queued_distance() {
    Fixture fixture;
    fixture.port.boundary = 0.22;
    fixture.start();
    fixture.present(); // Both slots are coherent even if the destination is too distant for live refresh.
    const auto FIRST = fixture.port.captures.size();
    fixture.frame(fixture.b, 0.12);
    CHECK(fixture.port.inactive_source == fixture.a.identity);
    fixture.frame(fixture.b, 0.2);
    fixture.frame(fixture.b, 0.2);
    CHECK(fixture.port.captures.size() == FIRST + 1); // Destination remains live during takeoff too.
    CHECK(fixture.turn.stage(fixture.b));
    CHECK(fixture.port.captures.size() == FIRST + 1); // An interruption in flight must not replace a frozen image.
    fixture.frame(fixture.b, 0.2);
    fixture.frame(fixture.b, 0.15);
    CHECK(fixture.port.captures.size() == FIRST + 2);
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    fixture.turn.begin(fixture.c, fixture.d, 1.0, LINEAR);
    CHECK(fixture.port.timed_pair.first == fixture.b.identity && fixture.port.timed_pair.second == fixture.d.identity);
    CHECK(fixture.turn.source().identity == fixture.a.identity && fixture.turn.destination().identity == fixture.b.identity);
    fixture.turn.begin(fixture.d, fixture.a, 1.0, LINEAR);
    CHECK(fixture.port.timed_pair.first == fixture.a.identity && fixture.port.timed_pair.second == fixture.b.identity);
    CHECK(fixture.turn.progress() > 0.8);
}

void cancellation_and_monitors() {
    Fixture first;
    Fixture second;
    first.start();
    second.start();
    first.present();
    second.present();
    first.port.buffers_ready = false;
    first.frame(first.b, 0.1);
    CHECK(!first.turn.active());
    CHECK(second.turn.active());
    second.turn.cancel(); // monitor removal destroys/cancels only its own turn
    CHECK(!second.turn.drawable());
    first.port.buffers_ready = true;
    first.start();
    first.frame(first.a, 0.0); // destination has never been captured
    CHECK(!first.turn.active());
    second.start();
    second.frame(second.b, 0.0);
    second.turn.presented(false);
    CHECK(second.port.native_presentations == 1);
    CHECK(!second.turn.active());
}

void lifetime_and_curve_latching() {
    Fixture fixture;
    FakeWorkspace address{1};
    fixture.one = std::shared_ptr<FakeWorkspace>(&address, [](FakeWorkspace *) {});
    fixture.a = fixture.port.composition(fixture.one);
    fixture.start();
    fixture.present();
    fixture.frame(fixture.b, 0.1);
    auto old_id = fixture.a.identity;
    fixture.one.reset();
    const auto REPLACEMENT = std::shared_ptr<FakeWorkspace>(&address, [](FakeWorkspace *) {});
    const auto FRESH = fixture.port.composition(REPLACEMENT);
    CHECK(FRESH.identity != old_id);
    fixture.turn.begin(fixture.b, FRESH, 1.0, SQUARE);
    fixture.frame(FRESH, 0.1);
    CHECK(std::abs(fixture.turn.progress() - 0.2) < 0.000001);
    for (int i = 0; i < 30 && fixture.port.swaps == 0; ++i) {
        fixture.frame(FRESH, 0.05);
    }
    CHECK(fixture.port.swaps == 1); // new identity is pending, not a reversal
    fixture.turn.presented(true);
    fixture.frame(FRESH, 0.2);
    CHECK(std::abs(fixture.turn.progress() - 0.04) < 0.000001);
}

void drawing_and_fallback() {
    Fixture fixture;
    RecordingWorkspaceStyle style;
    const auto DRAW = [&](double progress, Direction direction, bool matching) {
        return style.draw(progress, direction, matching);
    };
    CHECK(!fixture.turn.draw(DRAW, true));
    CHECK(style.draws.empty());
    fixture.start();
    CHECK(!fixture.turn.draw(DRAW, true));
    fixture.frame(fixture.b, 0.0);
    style.live_value = 0.25;
    CHECK(fixture.turn.draw(DRAW, true));
    CHECK(style.draws.size() == 1);
    CHECK(style.draws.back().matching_wallpaper);
    CHECK(style.draws.back().live_value == 0.25);
    fixture.frame(fixture.b, 0.1);
    style.live_value = 0.75;
    CHECK(fixture.turn.draw(DRAW, false));
    CHECK(style.draws.back().progress > 0.0);
    CHECK(!style.draws.back().matching_wallpaper);
    CHECK(style.draws.back().live_value == 0.75);
    const int NATIVE_BEFORE_FAILURE = fixture.port.native_presentations;
    style.succeeds = false;
    CHECK(!fixture.turn.draw(DRAW, false));
    CHECK(fixture.port.native_presentations == NATIVE_BEFORE_FAILURE + 1);
    CHECK(!fixture.turn.active());
}

void registry_settings_and_reload() {
    FakeWorkspacePort first;
    FakeWorkspacePort second;
    WorkspaceTurns<int, FakeWorkspacePort> turns;
    int resolutions = 0;
    bool use_square = false;
    const auto RESOLVE = [&](std::string_view) -> Transition::Curve {
        ++resolutions;
        return use_square ? Transition::Curve(SQUARE) : Transition::Curve(LINEAR);
    };
    turns.configure(true, 1, 1.0, "curve", true, RESOLVE);
    CHECK(turns.enabled());
    CHECK(resolutions == 1);
    auto &turn = turns.get(1, first);
    auto &other = turns.get(2, second);
    const auto ONE = std::make_shared<FakeWorkspace>(1);
    const auto TWO = std::make_shared<FakeWorkspace>(2);
    const auto SOURCE = first.composition(ONE);
    const auto DESTINATION = first.composition(TWO);
    CHECK(turn.stage(SOURCE));
    turn.begin(SOURCE, DESTINATION, turns.duration(), turns.curve());
    turn.frame(DESTINATION, 0.0);
    turn.presented(true);
    turn.frame(DESTINATION, 0.1);
    CHECK(other.stage(SOURCE));
    other.begin(SOURCE, DESTINATION, turns.duration(), turns.curve());
    turns.configure(true, 1, 2.0, "curve", false, RESOLVE);
    CHECK(resolutions == 2);
    use_square = true;
    turns.reload();
    turns.configure(true, 1, 2.0, "curve", false, RESOLVE);
    CHECK(resolutions == 3);
    turn.frame(DESTINATION, 0.1);
    CHECK(std::abs(turn.progress() - 0.2) < 0.000001);
    CHECK(std::abs(turns.curve()(0.5) - 0.25) < 0.000001);
    CHECK(turns.duration() == 2.0);
    turn.cancel();
    CHECK(turn.stage(SOURCE));
    turn.begin(SOURCE, DESTINATION, turns.duration(), turns.curve());
    turn.frame(DESTINATION, 0.0);
    turn.presented(true);
    turn.frame(DESTINATION, 0.2);
    CHECK(std::abs(turn.progress() - 0.01) < 0.000001);
    turns.remove(1);
    CHECK(turns.find(1) == nullptr);
    CHECK(turns.find(2) != nullptr && turns.find(2)->active());
    turns.configure(true, 2, 2.0, "curve", false, RESOLVE);
    CHECK(turns.find(2) == nullptr);
    turns.get(2, second);
    turns.configure(false, 2, 2.0, "curve", false, RESOLVE);
    CHECK(!turns.enabled());
    CHECK(turns.find(2) == nullptr);
    WorkspaceTurns<int, FakeWorkspacePort> delayed;
    int attempts = 0;
    const auto DELAYED_RESOLVE = [&](std::string_view) -> Transition::Curve {
        ++attempts;
        return attempts == 1 ? Transition::Curve{} : Transition::Curve(LINEAR);
    };
    delayed.configure(true, 1, 1.0, "curve", false, DELAYED_RESOLVE);
    CHECK(!delayed.curve());
    CHECK(!delayed.enabled());
    delayed.configure(true, 1, 1.0, "curve", false, DELAYED_RESOLVE);
    CHECK(attempts == 2);
    CHECK(static_cast<bool>(delayed.curve()));
    CHECK(delayed.enabled());
}

void wallpaper_identity_matching() {
    using Identity = WallpaperIdentity<int>;
    const std::array<Identity, 2> SOURCE{{{.texture = 7, .generation = 1},
                                          {.texture = 8, .generation = 2}}};
    auto destination = SOURCE;
    CHECK(wallpaper_matches<int>(SOURCE, destination));
    destination[1].generation = 3;
    CHECK(!wallpaper_matches<int>(SOURCE, destination));
    destination = SOURCE;
    destination[0].texture = 9;
    CHECK(!wallpaper_matches<int>(SOURCE, destination));
    CHECK(!wallpaper_matches<int>(SOURCE, std::span<const Identity>(destination.data(), 1)));
}

void blend_handoff_and_failures() {
    Fixture fixture;
    fixture.port.boundary = .22;
    fixture.start();
    fixture.present();
    fixture.frame(fixture.b, .2);
    fixture.frame(fixture.b, .2);
    fixture.frame(fixture.b, .2);
    CHECK(fixture.port.clones.empty());
    fixture.frame(fixture.b, .2);
    CHECK(fixture.port.clones.size() == 1);
    CHECK(fixture.port.clones.front() == CaptureKind::DestinationLive);
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) == 0.0);
    fixture.port.advance(.02);
    const double FIRST_MIX = fixture.turn.blend_mix(CaptureKind::DestinationLive);
    CHECK(FIRST_MIX > .24 && FIRST_MIX < .26);
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) >= FIRST_MIX);
    fixture.port.advance(.08);
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) == 1.0);
    CHECK(fixture.port.releases.size() == 1);

    Fixture failure;
    failure.port.boundary = .22;
    failure.start();
    failure.present();
    failure.frame(failure.b, .2);
    failure.frame(failure.b, .2);
    failure.frame(failure.b, .2);
    failure.port.clone_succeeds = false;
    failure.frame(failure.b, .2);
    CHECK(!failure.turn.active());
}

void inactive_source_guards() {
    Fixture fixture;
    fixture.start();
    fixture.present();
    fixture.port.inactive_facts.popup = true;
    fixture.frame(fixture.b, .01);
    CHECK(fixture.port.inactive_source == 0);
    fixture.port.hud_enabled = true;
    fixture.frame(fixture.b, .01);
    CHECK(fixture.port.inactive_source == fixture.a.identity);
    fixture.port.inactive_source = 0;
    fixture.port.inactive_facts.shell_matches = false;
    fixture.frame(fixture.b, .01);
    CHECK(fixture.port.inactive_source == 0);
    fixture.port.inactive_facts.shell_matches = true;
    fixture.port.inactive_capture_succeeds = false;
    fixture.frame(fixture.b, .01);
    CHECK(!fixture.turn.active());
}

void queue_and_hud_order() {
    Fixture fixture;
    fixture.start();
    fixture.present();
    bool prepared = false;
    Fixture missing;
    missing.start();
    missing.present();
    CHECK(!missing.turn.queue(false, [&] { prepared = true; return true; }));
    CHECK(!prepared);
    CHECK(!missing.turn.active());
    CHECK(missing.port.diagnostics.back() == "queue-resources");
    CHECK(fixture.turn.queue(true, [&] { prepared = true; return true; }));
    CHECK(prepared);
    std::vector<HudReplay> replay;
    fixture.turn.replay_hud([&](HudReplay step) { replay.push_back(step); });
    CHECK(replay.empty());
    fixture.port.hud_enabled = true;
    fixture.turn.replay_hud([&](HudReplay step) { replay.push_back(step); });
    const std::vector<HudReplay> EXPECTED{HudReplay::Bottom, HudReplay::Blur,
        HudReplay::Top, HudReplay::Overlay, HudReplay::Popups, HudReplay::RestoreBlur};
    CHECK(replay == EXPECTED);
    CHECK(!fixture.turn.queue(true, [] { return false; }));
    CHECK(!fixture.turn.active());
}

void context_and_safety_gates() {
    using Turn = WorkspaceTurn<FakeWorkspacePort>;
    CHECK(Turn::prime_native({.enabled = true, .output_configured = true}));
    CHECK(!Turn::prime_native({.output_configured = true}));
    CHECK(!Turn::prime_native({.enabled = true}));
    CHECK(!Turn::prime_native({.enabled = true, .output_configured = true, .presented = true}));
    CHECK(Turn::intercept_mutation({.enabled = true}));
    CHECK(!Turn::intercept_mutation({.enabled = true, .capture_depth = 1}));
    CHECK(!Turn::intercept_mutation({.enabled = true, .mutation_depth = 1}));
    CHECK(!Turn::intercept_mutation({}));
    CHECK(Turn::queue_context({.monitor = true}));
    CHECK(!Turn::queue_context({.monitor = true, .capture_depth = 1}));
    CHECK(!Turn::queue_context({.monitor = true, .capture = true}));
    CHECK(!Turn::queue_context({.monitor = true, .snapshot = true}));
    CHECK(!Turn::queue_context({.monitor = true, .read_only = true}));
    CHECK(!Turn::queue_context({}));
    CHECK(Turn::prime_native({.enabled = true, .output_configured = true}));
    CHECK(!Turn::prime_native({.enabled = true, .output_configured = true, .presented = true}));
    CHECK(!Turn::prime_native({.enabled = false, .output_configured = true}));
    CHECK(!Turn::prime_native({.enabled = true, .output_configured = false}));
    Fixture fixture;
    fixture.start();
    fixture.present();
    fixture.port.retiring_shell = true;
    fixture.frame(fixture.b, .01);
    CHECK(fixture.turn.active()); // Non-HUD styles retain native shell ordering.
    fixture.port.hud_enabled = true;
    bool prepared = false;
    CHECK(!fixture.turn.queue(true, [&] { prepared = true; return true; }));
    CHECK(!prepared);
    CHECK(!fixture.turn.active());
}

void blend_state_does_not_escape_its_leg() {
    Fixture fixture;
    fixture.port.boundary = .22;
    fixture.start();
    fixture.present();
    for (int i = 0; i < 4; ++i) {
        fixture.frame(fixture.b, .2);
    }
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) == 0.0);
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR);
    fixture.frame(fixture.c, .21);
    CHECK(fixture.turn.active());
    CHECK(fixture.port.swaps == 1);
    CHECK(fixture.port.source.identity == fixture.b.identity);
    CHECK(fixture.port.destination.identity == fixture.c.identity);
    CHECK(fixture.turn.blend_mix(CaptureKind::SourceIsolated) == 1.0);
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) == 1.0);
    fixture.turn.presented(true);
    fixture.frame(fixture.c, .1);
    fixture.turn.cancel();
    const auto CLONES = fixture.port.clones.size();
    fixture.start();
    fixture.present();
    fixture.frame(fixture.b, .1);
    CHECK(fixture.port.clones.size() == CLONES);
    CHECK(fixture.turn.blend_mix(CaptureKind::SourceIsolated) == 1.0);
    CHECK(fixture.turn.blend_mix(CaptureKind::DestinationLive) == 1.0);
}

void failure_diagnostics_and_recovery() {
    Fixture fixture;
    fixture.port.buffers_ready = false;
    CHECK(!fixture.turn.stage(fixture.a));
    CHECK(fixture.port.diagnostics.back() == "stage-buffers");
    fixture.port.buffers_ready = true;
    fixture.port.capture_succeeds = false;
    CHECK(!fixture.turn.stage(fixture.a));
    CHECK(fixture.port.diagnostics.back() == "capture-source");
    fixture.port.capture_succeeds = true;
    fixture.start();
    fixture.port.capture_succeeds = false;
    fixture.frame(fixture.b, .01);
    CHECK(!fixture.turn.active());
    CHECK(fixture.port.diagnostics[fixture.port.diagnostics.size() - 2] == "capture-destination");
    CHECK(fixture.port.diagnostics.back() == "frame-capture");
    fixture.port.capture_succeeds = true;
    fixture.start();
    fixture.present();
    fixture.port.safety_facts.locked = true;
    fixture.frame(fixture.b, .01);
    CHECK(!fixture.turn.active());
    CHECK(fixture.port.diagnostics.back() == "scene-unsafe");
    fixture.port.safety_facts.locked = false;
    fixture.start();
    fixture.present();
    CHECK(fixture.turn.active());
}

void composite_style_request_lifecycle() {
    Fixture fixture;
    fixture.port.layered_capture = false; // Rotation uses only the two composite slots.
    fixture.start();
    CHECK(fixture.turn.stage(fixture.b));
    fixture.turn.begin(fixture.b, fixture.b, 1.0, LINEAR); // Native early repeat.
    CHECK(fixture.turn.active());
    CHECK(fixture.turn.stage(fixture.b));
    fixture.turn.begin(fixture.b, fixture.c, 1.0, LINEAR); // Coalesce before first draw.
    CHECK(fixture.turn.source().identity == fixture.a.identity);
    CHECK(fixture.turn.destination().identity == fixture.c.identity);
    fixture.frame(fixture.c, .2);
    CHECK(fixture.turn.progress() == 0.0);
    RecordingWorkspaceStyle style;
    const auto DRAW = [&](double progress, Direction direction, bool matching) {
        return style.draw(progress, direction, matching);
    };
    CHECK(fixture.turn.draw(DRAW, true));
    fixture.frame(fixture.c, .1);
    const auto PROGRESS = fixture.turn.progress();
    fixture.turn.begin(fixture.c, fixture.c, 1.0, LINEAR);
    CHECK(fixture.turn.progress() == PROGRESS);
    CHECK(fixture.turn.stage(fixture.c));
    fixture.turn.begin(fixture.c, fixture.d, 1.0, LINEAR); // Queue after presentation.
    CHECK(fixture.turn.destination().identity == fixture.c.identity);
    CHECK(fixture.turn.stage(fixture.d));
    fixture.turn.begin(fixture.d, fixture.a, 1.0, LINEAR); // Reverse and discard queue.
    CHECK(fixture.turn.progress() == PROGRESS);
    for (int i = 0; i < 40 && fixture.turn.active(); ++i) {
        fixture.frame(fixture.a, .05);
        if (fixture.turn.drawable()) {
            CHECK(fixture.turn.draw(DRAW, true));
        }
    }
    CHECK(!fixture.turn.active());
    CHECK(fixture.port.swaps == 0);
    CHECK(fixture.port.clones.empty());
    CHECK(fixture.port.native_presentations == 0);
    CHECK(fixture.port.diagnostics.empty());
}

void logical_rank_survives_mutation() {
    Fixture fixture;
    fixture.a.logical_order = 3;
    fixture.b.logical_order = 3;
    fixture.start();
    CHECK(fixture.turn.source().logical_order == 3);
    CHECK(fixture.turn.destination().logical_order == 4);
}

void floating_presentation_commands() {
    Fixture fixture;
    FakeWorkspacePort::PresentationPort presentation{
        .old = {{.identity = 10, .rect = {0., 1., 2., 3.}, .order = 3.},
            {.identity = 20, .opacity = .8F, .order = 0.}},
        .live = {{.identity = 10, .rect = {4., 5., 6., 7.}, .opacity = .9F, .order = 1.},
            {.identity = 30, .opacity = .5F, .order = 2.}},
        .emitted = {}};
    CHECK(fixture.turn.present_floating(CaptureKind::SourceIsolated, presentation, .25));
    CHECK(presentation.emitted.size() == 3);
    const auto &retired = presentation.emitted[0];
    CHECK(retired.stale && retired.index == 1);
    CHECK(std::abs(retired.facts.opacity - .6F) < .000001F);
    const auto &added = presentation.emitted[1];
    CHECK(!added.stale && added.index == 1);
    CHECK(!added.previous.has_value());
    CHECK(added.facts.opacity == .125F);
    const auto &matched = presentation.emitted[2];
    CHECK(!matched.stale && matched.index == 0 && matched.previous == 0);
    CHECK(matched.live_mix == .25F);
    CHECK(matched.facts.opacity == .9F);
    CHECK(matched.facts.order == 2.5);
    const std::array<double, 4> EXPECTED_RECT{1., 2., 3., 4.};
    CHECK(matched.facts.rect == EXPECTED_RECT);
    CHECK(presentation.old[0].rect[0] == 0.);
    CHECK(presentation.live[0].rect[0] == 4.);
    CHECK(fixture.turn.present_floating(CaptureKind::SourceIsolated, presentation, .75));
    CHECK(presentation.emitted[1].facts.identity == 10); // Re-sort crossing stack positions.
    CHECK(presentation.emitted[2].facts.identity == 30);
    presentation.allocation_succeeds = false;
    CHECK(!fixture.turn.present_floating(CaptureKind::SourceIsolated, presentation, .75));

    FakeWorkspacePort::PresentationPort empty;
    CHECK(fixture.turn.present_floating(CaptureKind::DestinationLive, empty, .5));
    CHECK(empty.emitted.empty());
}
void repeated_endpoint_keeps_the_visual_timeline() {
    Fixture repeated;
    Fixture uninterrupted;
    repeated.start();
    uninterrupted.start();
    repeated.present();
    uninterrupted.present();
    repeated.frame(repeated.b, .2);
    uninterrupted.frame(uninterrupted.b, .2);
    repeated.turn.begin(repeated.b, repeated.c, 1.0, LINEAR);
    repeated.turn.begin(repeated.c, repeated.b, 1.0, LINEAR);
    for (int i = 0; i < 20 && uninterrupted.turn.active(); ++i) {
        repeated.frame(repeated.b, .05);
        uninterrupted.frame(uninterrupted.b, .05);
        CHECK(repeated.turn.active() == uninterrupted.turn.active());
        CHECK(std::abs(repeated.turn.progress() - uninterrupted.turn.progress()) < 1e-9);
    }
    CHECK(!repeated.turn.active());
    CHECK(repeated.port.swaps == 0);
    CHECK(repeated.port.landings == 1);
    CHECK(repeated.port.diagnostics.empty());
}

} // namespace

int main() {
    identity_and_direction();
    capture_plan_tests::run();
    returning_to_destination_discards_pending();
    repeated_endpoint_keeps_the_visual_timeline();
    staging_and_capture_failures();
    gate_and_unpresented_retarget();
    reversal_and_coalescing();
    destination_swap_and_stall();
    adaptive_capture_and_queued_distance();
    cancellation_and_monitors();
    lifetime_and_curve_latching();
    registry_settings_and_reload();
    wallpaper_identity_matching();
    drawing_and_fallback();
    blend_handoff_and_failures();
    inactive_source_guards();
    queue_and_hud_order();
    context_and_safety_gates();
    blend_state_does_not_escape_its_leg();
    failure_diagnostics_and_recovery();
    composite_style_request_lifecycle();
    returning_to_current_target_preserves_timeline();
    logical_rank_survives_mutation();
    floating_presentation_commands();
    return g_failures == 0 ? 0 : 1;
}
