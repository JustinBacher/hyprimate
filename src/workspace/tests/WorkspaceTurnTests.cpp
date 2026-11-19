#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>

#include "Check.hpp"
#include "FakeWorkspacePort.hpp"

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

void destination_swap_and_stall() {
    Fixture fixture;
    fixture.port.next_wallpaper = {.texture = 10, .generation = 1};
    fixture.start();
    fixture.port.next_wallpaper = {.texture = 20, .generation = 2};
    fixture.present();
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
    Fixture f;
    f.port.live_boundary = 0.22;
    f.start();
    f.present(); // Both slots are coherent even if the destination is too distant for live refresh.
    const auto FIRST = f.port.captures.size();
    f.frame(f.b, 0.12);
    CHECK(f.port.inactive_source == f.a.identity);
    f.frame(f.b, 0.2);
    f.frame(f.b, 0.2);
    CHECK(f.port.captures.size() == FIRST);
    CHECK(f.turn.stage(f.b));
    CHECK(f.port.captures.size() == FIRST); // An interruption in flight must not replace a frozen image.
    f.frame(f.b, 0.2);
    f.frame(f.b, 0.15);
    CHECK(f.port.captures.size() == FIRST + 1);
    f.turn.begin(f.b, f.c, 1.0, LINEAR);
    f.turn.begin(f.c, f.d, 1.0, LINEAR);
    CHECK(f.port.timed_pair.first == f.b.identity && f.port.timed_pair.second == f.d.identity);
    CHECK(f.turn.source().identity == f.a.identity && f.turn.destination().identity == f.b.identity);
    f.turn.begin(f.d, f.a, 1.0, LINEAR);
    CHECK(f.port.timed_pair.first == f.a.identity && f.port.timed_pair.second == f.b.identity);
    CHECK(f.turn.progress() > 0.8);
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
} // namespace

int main() {
    identity_and_direction();
    returning_to_destination_discards_pending();
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
    return g_failures == 0 ? 0 : 1;
}
