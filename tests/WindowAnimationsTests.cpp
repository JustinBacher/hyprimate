#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "Check.hpp"
#include "FakeWindowPort.hpp"
#include "WindowAnimations.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
using namespace hyprimate::animation::test;

namespace {
struct Settings {
    double speed = 1;
    bool operator==(const Settings &) const = default;
};
struct State {
    std::uint64_t seed = 0;
    double progress = 0;
    bool closing = false;
};
struct Log {
    int opens = 0, closes = 0, ends = 0, prepares = 0;
    End last_end = End::Completed;
    const State *closed_state = nullptr;
    double close_progress = 0;
    std::uint64_t close_seed = 0;
    std::vector<Step> steps;
    std::vector<int> events;
    int real_draws = 0, real_screen_draws = 0;
    bool screen_active = false;
    std::optional<Box> drive;
    bool release = false, snap = false, alpha = false, running = true, tail = false, backdrop = false;
    CloseStart close_result = CloseStart::Owned;
};
template<int Id> struct TestStyle {
    using State = ::State;
    using Settings = ::Settings;
    using Prepared = ::Settings;
    struct Resources : ResourcesBase<State, Settings, Prepared> {
        Log log;
        ResourcesBase<State, Settings, Prepared>::AnimationPool animations;
    };
    static constexpr auto KIND = static_cast<config::Style>(Id);
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static Prepared prepare_settings(const Settings &settings) { return settings; }
    static State start(const Resources & /*resources*/, std::uint64_t seed) { return {.seed = seed}; }
    static bool open(State & /*state*/, const Place & /*place*/, Resources &resources, Effects & /*effects*/) {
        ++resources.log.opens;
        return true;
    }
    static Frame advance(State &state, const Step &step, Resources &resources, Effects &effects) {
        resources.log.events.push_back(2);
        resources.log.steps.push_back(step);
        state.progress += step.elapsed * resources.prepared.speed;
        effects.drive_body = resources.log.drive;
        effects.release_body = resources.log.release;
        effects.snap_to_goal = resources.log.snap;
        effects.settle_native_open_alpha = resources.log.alpha;
        return {.running = resources.log.running, .bounds = {0, 0, 100, 80},
                .backdrop = resources.log.backdrop, .tail = resources.log.tail};
    }
    static CloseStart close(State &state, const Place & /*place*/, Resources &resources) {
        ++resources.log.closes;
        resources.log.closed_state = &state;
        resources.log.close_progress = state.progress;
        resources.log.close_seed = state.seed;
        state.closing = true;
        return resources.log.close_result;
    }
    static void end(State & /*state*/, Resources &resources, End why) noexcept {
        ++resources.log.ends;
        resources.log.last_end = why;
        resources.log.events.push_back(7);
    }
    static bool draw(State & /*state*/, const Draw<FakeCanvas> &draw, Resources &resources) {
        if (draw.real_pass) { ++resources.log.real_draws; }
        draw.canvas.draws.push_back(draw.image.id);
        return true;
    }
    struct Screen {
        static void tick(Resources &resources, const ScreenFacts & /*facts*/) { resources.log.events.push_back(1); }
        static void under_window(Resources &resources, const ScreenDraw<FakeCanvas> &draw) {
            if (draw.real_pass) { ++resources.log.real_screen_draws; }
            resources.log.events.push_back(3);
        }
        static void after_windows(Resources &resources, const ScreenDraw<FakeCanvas> &draw) {
            if (draw.real_pass) { ++resources.log.real_screen_draws; }
            resources.log.events.push_back(4);
        }
        static bool needs_frame(const Resources &resources, MonitorId monitor) {
            return resources.log.screen_active && monitor == 1;
        }
        static void clear(Resources &resources, std::optional<MonitorId> /*monitor*/) {
            resources.log.screen_active = false;
            resources.log.events.push_back(5);
        }
    };
};
struct MeshStyle : TestStyle<3> {
    static bool draw(State &, const Draw<FakeCanvas> &, Resources &) = delete;
    template<Vertex V> static Mesh<V> mesh(State & /*state*/, const MeshTarget<V> &target, Resources & /*resources*/) {
        target.buffers.window.push_back(V{});
        return {.window = &target.buffers.window, .clip = std::nullopt};
    }
};
using Style = TestStyle<1>;
using Other = TestStyle<2>;
using List = StyleList<Style, Other>;
using Module = CWindowAnimations<FakeWindowPort, List>;

struct Fixture {
    FakeWindowPort port;
    Module::Resources resources;
    Module module{port, resources};
    Policy policy{.style = Style::KIND, .enabled = true, .opening = true, .movement = true,
                  .closing = true, .workspace_movement = true, .close_available = true};
    FrameId frame_a = 0, frame_b = 0;
    Fixture() {
        port.monitors.emplace(1, ScreenFacts{.monitor = 1, .screen = {.x=0, .y=0, .w=800, .h=600}, .work_area = {.x=0, .y=0, .w=800, .h=600}});
        port.monitors.emplace(2, ScreenFacts{.monitor = 2, .screen = {.x=800, .y=0, .w=800, .h=600}, .work_area = {.x=800, .y=0, .w=800, .h=600}});
        port.overlapping = {1, 2};
        module.configure(policy);
        module.update_settings<Style>({1});
        module.update_settings<Other>({1});
    }
    ~Fixture() { module.end_all(); }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    Fixture(Fixture &&) = delete;
    Fixture &operator=(Fixture &&) = delete;
    Log &log() { return resources.get<Style>().log; }
    void window(WindowId window_id, MonitorId monitor = 1, bool opening = true) {
        Observation observation{.window = window_id, .monitor = monitor, .workspace = 1,
            .body = {.x=100, .y=100, .w=100, .h=80}, .capture = {.x=95, .y=95, .w=110, .h=90},
            .goal_body = {.x=100, .y=100, .w=100, .h=80}, .screen = {.x=0, .y=0, .w=800, .h=600},
            .work_area = {.x=0, .y=0, .w=800, .h=600}, .mapped = true, .animating_in = opening};
        port.windows.emplace(window_id, observation);
        module.track(window_id);
    }
    void frame(MonitorId monitor, double elapsed = 0.016) {
        auto &frame_number = monitor == 1 ? frame_a : frame_b;
        module.frame({.monitor=monitor, .frame=++frame_number, .now=port.advance(elapsed)});
    }
    Handle handle(WindowId window_id = 1) { return port.transformers.at(window_id); }
    State *state(Handle handle) {
        return &resources.get<Style>().animations.get({.index=handle.index, .generation=handle.generation})->state;
    }
    FadeoutFacts fade(std::uint64_t fadeout_id, MonitorId monitor = 1) {
        FadeoutFacts facts{.fadeout = fadeout_id, .monitor = monitor, .workspace = 1, .native_box = {.x=95, .y=95, .w=110, .h=90},
                           .image_size = {.x=110, .y=90}, .scale = 1, .alpha = 1, .alive = true, .image_available = true};
        facts.now = port.now;
        port.fadeouts[fadeout_id] = facts;
        return facts;
    }
};

void opening_movement_and_settlement() {
    Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle();
    CHECK(fixture.log().opens == 1); CHECK(fixture.module.active(HANDLE));
    fixture.frame(1); CHECK(fixture.log().opens == 1);
    fixture.log().drive = Box{.x=130, .y=110, .w=80, .h=60}; fixture.frame(1);
    CHECK(fixture.port.windows.at(1).body.x == 130);
    fixture.log().drive.reset(); fixture.frame(1); CHECK(!fixture.log().steps.back().motion.has_value());
    fixture.log().running = false; fixture.frame(1);
    CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.log().ends == 1); CHECK(fixture.log().last_end == End::Completed);
    CHECK(fixture.port.windows.at(1).body.x == 100); CHECK(fixture.port.transformers.empty());
    fixture.port.windows.at(1).body.x += 10; fixture.port.windows.at(1).capture.x += 10; fixture.frame(1);
    CHECK(!fixture.module.active(HANDLE));
}
void disabled_open_and_move() {
    Fixture fixture; fixture.policy.opening = false; fixture.module.configure(fixture.policy); fixture.window(1); fixture.frame(1);
    CHECK(fixture.port.transformers.empty()); CHECK(fixture.log().opens == 0);
    fixture.port.windows.at(1).body.x += 10; fixture.port.windows.at(1).capture.x += 10; fixture.frame(1);
    CHECK(fixture.module.active(fixture.handle())); CHECK(fixture.log().opens == 0);
}
void monitor_frames_and_reattach() {
    Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle(); auto  const*state = fixture.state(HANDLE);
    const auto COUNT = fixture.log().steps.size(); fixture.frame(2); CHECK(fixture.log().steps.size() == COUNT);
    fixture.module.frame({.monitor=1, .frame=fixture.frame_a, .now=fixture.port.now}); CHECK(fixture.log().steps.size() == COUNT);
    fixture.port.transformers.erase(1); CHECK(fixture.module.needs_frame(1));
    FakeCanvas canvas; fixture.module.before_window(1, canvas);
    CHECK(fixture.handle() == HANDLE); CHECK(fixture.state(HANDLE) == state);
    CHECK(fixture.port.scheduled.size() >= 2);
    fixture.port.windows.at(1).monitor = 2; fixture.frame(1); CHECK(fixture.log().steps.size() == COUNT);
    fixture.frame(2); CHECK(fixture.log().steps.back().elapsed == 0); CHECK(fixture.state(HANDLE) == state);
}
void close_continuity_and_association() {
    Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle(); auto  const*state = fixture.state(HANDLE);
    fixture.frame(1, 0.1); const double PROGRESS = state->progress;
    fixture.module.close(1, fixture.port.now); CHECK(fixture.module.pending_for(1) == HANDLE);
    CHECK(fixture.log().closed_state == state); CHECK(fixture.log().close_progress == PROGRESS); CHECK(fixture.log().closes == 1);
    fixture.module.close(1, fixture.port.now); CHECK(fixture.log().closes == 1);
    fixture.module.unmap(1); fixture.module.destroyed(1); CHECK(fixture.module.active(HANDLE));
    auto const FADE = fixture.fade(10); fixture.module.bind_fadeout(2, FADE, {10}); CHECK(fixture.module.pending_for(1) == HANDLE);
    fixture.module.bind_fadeout(1, FADE, {10}); CHECK(fixture.module.active(HANDLE)); CHECK(fixture.module.suppresses(10));
    fixture.module.destroyed(1); fixture.frame(1); CHECK(fixture.module.active(HANDLE)); CHECK(fixture.state(HANDLE) == state);
    fixture.log().running = false; fixture.frame(1); CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.module.suppresses(10));
    fixture.module.native_finished(10); CHECK(!fixture.module.suppresses(10));
}
void close_timeout_invalid_and_fresh() {
    Fixture fixture; fixture.window(1, 1, false); fixture.frame(1); fixture.module.close(1, fixture.port.now);
    const auto PENDING = fixture.module.pending_for(1); CHECK(PENDING.has_value());
    if (!PENDING) { return; }
    const auto HANDLE = *PENDING; CHECK(fixture.log().close_seed == 0);
    fixture.frame(1, 1.1); CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.log().last_end == End::Interrupted);
    fixture.window(2); fixture.frame(1); const auto HANDLE_TWO = fixture.handle(2); fixture.module.close(2, fixture.port.now);
    auto fade = fixture.fade(11); fade.blurred = true; fixture.module.bind_fadeout(2, fade, {11});
    CHECK(!fixture.module.active(HANDLE_TWO)); CHECK(!fixture.module.suppresses(11));
}
void invalidation_and_restore() {
    for (int mode = 0; mode < 3; ++mode) {
        Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle();
        fixture.log().drive = Box{.x=120, .y=100, .w=100, .h=80}; fixture.frame(1);
        fixture.port.windows.at(1).goal_body = {.x=200, .y=100, .w=100, .h=80}; fixture.frame(1);
        if (mode == 0) { fixture.port.windows.at(1).fullscreen = true; fixture.frame(1); }
        if (mode == 1) { fixture.module.unmap(1); }
        if (mode == 2) { fixture.module.destroyed(1); }
        CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.log().ends == 1); CHECK(fixture.log().last_end == End::Interrupted);
        CHECK(fixture.port.windows.at(1).body.x == 200);
    }
}
void settings_style_reload_monitor() {
    Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle(); auto  const*state = fixture.state(HANDLE);
    fixture.module.update_settings<Style>({3}); fixture.frame(1, 0.1);
    CHECK(fixture.handle() == HANDLE); CHECK(fixture.state(HANDLE) == state); CHECK(std::abs(state->progress - 0.3) < 1e-8);
    fixture.module.close(1, fixture.port.now); auto const FADE = fixture.fade(20); fixture.module.bind_fadeout(1, FADE, {20});
    fixture.window(2); fixture.frame(1); auto const MAPPED = fixture.handle(2);
    fixture.policy.style = Other::KIND; fixture.module.configure(fixture.policy);
    CHECK(fixture.module.active(HANDLE)); CHECK(!fixture.module.active(MAPPED));
    fixture.module.monitor_changed(2); CHECK(fixture.module.active(HANDLE));
    fixture.module.monitor_changed(1); CHECK(!fixture.module.active(HANDLE));
    fixture.window(3); fixture.frame(1); auto const NEWER = fixture.handle(3); fixture.module.reload(); CHECK(!fixture.module.active(NEWER));
}
void stall_bounds() {
    { Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle();
      fixture.frame(1, 0.25); CHECK(!fixture.log().steps.back().stalled);
      fixture.frame(1, 0.251); CHECK(fixture.log().steps.back().stalled);
      fixture.frame(1, 0.1); fixture.frame(1, 0.251); CHECK(fixture.module.active(HANDLE));
      fixture.frame(1, 0.251); CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.log().last_end == End::Interrupted); }
    { Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle(); fixture.frame(1, 2.0); CHECK(fixture.module.active(HANDLE)); }
    { Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle(); fixture.frame(1, 2.001); CHECK(!fixture.module.active(HANDLE)); }
    { Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
      fixture.frame(1, 0.3); for (int i = 0; i < 10; ++i) { fixture.frame(1, 0.2); }
      fixture.frame(1, 0.3); fixture.frame(1, 0.3); CHECK(fixture.module.active(HANDLE)); }
}
void effects_and_stale_draw() {
    Fixture fixture; fixture.window(1); fixture.frame(1); auto const OLD = fixture.handle();
    fixture.log().drive = Box{.x=140, .y=100, .w=90, .h=70}; fixture.log().alpha = true; fixture.frame(1);
    CHECK(fixture.port.windows.at(1).body.x == 140); CHECK(!fixture.port.alpha_settled.empty());
    fixture.log().drive.reset(); fixture.log().release = true; fixture.frame(1);
    CHECK(fixture.port.windows.at(1).body.x == 100); CHECK(fixture.module.active(OLD));
    fixture.log().release = false; fixture.log().snap = true; fixture.frame(1); CHECK(fixture.module.active(OLD));
    fixture.module.unmap(1);
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{17};
    Draw<FakeCanvas> const DRAW{.canvas = canvas, .image = IMAGE, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {}, .image_size = {}};
    CHECK(!fixture.module.draw(OLD, DRAW)); CHECK(canvas.draws.empty());
    fixture.window(2); fixture.frame(1); auto const REPLACEMENT = fixture.handle(2);
    CHECK(REPLACEMENT != OLD); CHECK(!fixture.module.draw(OLD, DRAW));
    CHECK(fixture.module.draw(REPLACEMENT, DRAW)); CHECK(canvas.draws.size() == 1);
}
void close_modes_and_disable() {
    for (auto const MODE : {CloseStart::Native, CloseStart::Declined}) {
        Fixture fixture; fixture.log().close_result = MODE; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        if (MODE == CloseStart::Declined) {
            CHECK(!fixture.module.active(HANDLE)); CHECK(!fixture.module.pending_for(1));
        } else {
            auto const FADE = fixture.fade(40); fixture.module.bind_fadeout(1, FADE, {40});
            CHECK(fixture.module.active(HANDLE)); CHECK(!fixture.module.suppresses(40));
            fixture.module.native_finished(40); CHECK(!fixture.module.active(HANDLE));
            CHECK(fixture.log().last_end == End::Completed);
        }
    }
    Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now); auto const FADE = fixture.fade(41); fixture.module.bind_fadeout(1, FADE, {41});
    fixture.window(2); fixture.frame(1); auto const MAPPED = fixture.handle(2); fixture.module.close(2, fixture.port.now);
    fixture.policy.closing = false; fixture.module.configure(fixture.policy);
    CHECK(!fixture.module.active(HANDLE)); CHECK(!fixture.module.active(MAPPED)); CHECK(!fixture.module.suppresses(41));
}
void close_mid_movement() {
    Fixture fixture; fixture.policy.opening = false; fixture.module.configure(fixture.policy); fixture.window(1); fixture.frame(1);
    fixture.port.windows.at(1).body.x += 15; fixture.port.windows.at(1).capture.x += 15; fixture.frame(1);
    auto const HANDLE = fixture.handle(); auto  const*state = fixture.state(HANDLE); fixture.frame(1, 0.1);
    auto const PROGRESS = state->progress; fixture.module.close(1, fixture.port.now);
    CHECK(fixture.module.pending_for(1) == HANDLE); CHECK(fixture.log().closed_state == state);
    CHECK(fixture.log().close_progress == PROGRESS); CHECK(fixture.log().closes == 1);
}
void tail_and_backdrop() {
    Fixture fixture; fixture.log().tail = true; fixture.log().backdrop = true; fixture.window(1); fixture.frame(1);
    auto const HANDLE = fixture.handle(); FakeCanvas canvas; FakeCanvas::Image const FIRST{1}; FakeCanvas::Image const MATTE{2};
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.size() == 1);
    Draw<FakeCanvas> const DRAW{.canvas = canvas, .image = FIRST, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {.x=7, .y=8}, .image_size = {},
                          .real_pass = true, .frame = 1};
    CHECK(fixture.module.draw(HANDLE, DRAW));
    Draw<FakeCanvas> const SECOND{.canvas = canvas, .image = MATTE, .source = {}, .output = {},
                            .canvas_box = {}, .image_origin = {}, .image_size = {},
                            .real_pass = true, .frame = 2};
    CHECK(fixture.module.draw(HANDLE, SECOND));
    auto retained = fixture.port.retained_mapped_image(HANDLE);
    CHECK(retained.has_value());
    if (retained) { CHECK(retained->image->id == 1); CHECK(retained->image_origin.x == 7); }
    fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
    auto const COUNT = canvas.draws.size(); fixture.module.after_windows(1, canvas);
    CHECK(canvas.draws.size() == COUNT + 1); CHECK(canvas.draws.back() == 1);
    fixture.module.end_all(); CHECK(!fixture.port.retained_mapped_image(HANDLE));
}
void capacity_and_fadeout_validation() {
    Fixture fixture;
    for (WindowId window = 1; window <= 65; ++window) { fixture.window(window); }
    fixture.frame(1);
    auto const FIRST = fixture.handle(1);
    auto  const*first_state = fixture.state(FIRST);
    CHECK(fixture.resources.get<Style>().animations.size() == 65);
    CHECK(fixture.state(FIRST) == first_state);
    for (WindowId window = 1; window <= 16; ++window) {
        fixture.module.close(window, fixture.port.now);
        auto const FADEOUT = fixture.fade(100 + window);
        fixture.module.bind_fadeout(window, FADEOUT, {static_cast<int>(window)});
        CHECK(fixture.module.suppresses(100 + window));
    }
    auto const OVERFLOW = fixture.handle(17);
    fixture.module.close(17, fixture.port.now);
    fixture.module.bind_fadeout(17, fixture.fade(117), {17});
    CHECK(!fixture.module.active(OVERFLOW)); CHECK(!fixture.module.suppresses(117));
    auto const ORPHAN = fixture.fade(300);
    fixture.module.bind_fadeout(999, ORPHAN, {1});
    CHECK(!fixture.module.suppresses(300));
}
void pending_invalidation() {
    for (int mode = 0; mode < 6; ++mode) {
        Fixture fixture; fixture.window(1); fixture.frame(1);
        auto const HANDLE = fixture.handle(); fixture.module.close(1, fixture.port.now);
        auto fadeout = fixture.fade(200);
        if (mode == 0) { fixture.frame(1, 1.1); }
        if (mode == 1) { fixture.port.monitors.erase(1); fixture.module.monitor_changed(1); }
        if (mode == 2) { fixture.port.windows.at(1).scale = 2; fixture.module.monitor_changed(1); }
        if (mode == 3) { fadeout.monitor = 2; }
        if (mode == 4) { fadeout.image_available = false; }
        if (mode == 5) { fadeout.now = fixture.port.advance(1.1); }
        fixture.module.bind_fadeout(1, fadeout, {1});
        CHECK(!fixture.module.active(HANDLE)); CHECK(!fixture.module.suppresses(200));
        CHECK(fixture.port.visuals.empty());
    }
}
void screen_order_and_shutdown() {
    Fixture fixture; fixture.log().events.clear(); fixture.log().screen_active = true; fixture.window(1); fixture.frame(1);
    CHECK(fixture.log().events.size() >= 2); CHECK(fixture.log().events[0] == 1);
    CHECK(fixture.log().events[1] == 2);
    FakeCanvas canvas; fixture.module.before_window(1, canvas); fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().events[2] == 3); CHECK(fixture.log().events[3] == 4);
    auto const MAPPED = fixture.handle(); fixture.window(2); fixture.frame(1);
    auto const DYING = fixture.handle(2); fixture.module.close(2, fixture.port.now);
    fixture.module.bind_fadeout(2, fixture.fade(400), {2});
    fixture.log().events.clear();
    fixture.port.lifecycle_events = &fixture.log().events;
    fixture.module.detach_transformers(); CHECK(fixture.port.transformers.empty());
    CHECK(fixture.module.active(MAPPED)); CHECK(fixture.module.active(DYING));
    fixture.module.end_all(); CHECK(!fixture.module.active(MAPPED)); CHECK(!fixture.module.active(DYING));
    const auto FIRST_END = std::find(fixture.log().events.begin(), fixture.log().events.end(), 7);
    const auto FIRST_RELEASE = std::find(fixture.log().events.begin(), fixture.log().events.end(), 8);
    CHECK(!fixture.log().events.empty()); CHECK(fixture.log().events.front() == 6);
    CHECK(FIRST_END != fixture.log().events.end()); CHECK(FIRST_RELEASE != fixture.log().events.end());
    CHECK(FIRST_END < FIRST_RELEASE);
    CHECK(fixture.port.visuals.empty()); CHECK(fixture.port.suppressed.empty());
    CHECK(!fixture.module.needs_frame(1)); CHECK(fixture.log().events.back() == 5);
    fixture.module.end_all(); CHECK(fixture.port.visuals.empty());
}
void screen_survives_window_and_allocation_failure() {
    Fixture fixture; fixture.log().screen_active = true; fixture.window(1); fixture.frame(1);
    fixture.module.unmap(1); CHECK(fixture.module.needs_frame(1));
    CHECK(!fixture.port.scanout.empty());
    Fixture failing; failing.port.fail_acquire = true; failing.window(1);
    bool threw = false;
    try { failing.frame(1); } catch (const std::runtime_error &) { threw = true; }
    CHECK(threw); CHECK(failing.resources.get<Style>().animations.size() == 0);
    CHECK(failing.port.transformers.empty()); CHECK(failing.port.visuals.empty());
    CHECK(failing.log().ends == 1); CHECK(failing.log().last_end == End::Interrupted);
}
void pass_kind_retention_and_backdrop() {
    Fixture fixture; fixture.log().tail = true; fixture.log().backdrop = true;
    fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{8};
    Draw<FakeCanvas> const DRAW{.canvas = canvas, .image = IMAGE, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    canvas.pass_kind = PassKind::Inert;
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.empty());
    fixture.module.draw(HANDLE, DRAW); CHECK(!fixture.port.retained_mapped_image(HANDLE));
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().real_draws == 0); CHECK(fixture.log().real_screen_draws == 0);
    canvas.pass_kind = PassKind::WorkspaceSource;
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.size() == 1);
    fixture.module.draw(HANDLE, DRAW); CHECK(!fixture.port.retained_mapped_image(HANDLE));
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().real_draws == 0); CHECK(fixture.log().real_screen_draws == 0);
    CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 0);
    CHECK(fixture.port.visual_cache(HANDLE).scratch_backdrops == 1);
    canvas.pass_kind = PassKind::Live;
    fixture.module.draw(HANDLE, DRAW); CHECK(fixture.port.retained_mapped_image(HANDLE).has_value());
    fixture.module.before_window(1, canvas); fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().real_draws == 1); CHECK(fixture.log().real_screen_draws == 2);
    CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 1);
}
void out_of_order_and_duplicate_fadeouts() {
    Fixture fixture; fixture.window(1); fixture.window(2); fixture.frame(1);
    auto const FIRST = fixture.handle(1); auto const SECOND = fixture.handle(2);
    fixture.module.close(1, fixture.port.now); fixture.module.close(2, fixture.port.now);
    auto const FADEOUT = fixture.fade(500);
    fixture.module.bind_fadeout(2, FADEOUT, {2});
    CHECK(fixture.module.active(SECOND)); CHECK(fixture.module.pending_for(1) == FIRST);
    fixture.module.bind_fadeout(1, FADEOUT, {1});
    CHECK(!fixture.module.active(FIRST)); CHECK(fixture.module.active(SECOND));
    CHECK(fixture.module.dying_views().size() == 1); CHECK(fixture.module.suppresses(500));
}
void close_placement_and_invalid_fadeouts() {
    for (int mode = 0; mode < 4; ++mode) {
        Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now); auto fadeout = fixture.fade(600);
        if (mode == 0) { fadeout.scale = 2; }
        if (mode == 1) { fadeout.alpha = 0; }
        if (mode == 2) { fadeout.alive = false; }
        if (mode == 3) { fadeout.native_box.w = 0; }
        fixture.module.bind_fadeout(1, fadeout, {1});
        CHECK(!fixture.module.active(HANDLE)); CHECK(!fixture.module.suppresses(600));
    }
    Fixture fixture; fixture.window(1);
    auto &window = fixture.port.windows.at(1);
    window.workspace_offset = {.x=10, .y=20}; window.floating_offset = {.x=3, .y=4};
    window.screen = {.x=50, .y=30, .w=800, .h=600}; window.scale = 2;
    fixture.frame(1); fixture.module.close(1, fixture.port.now);
    auto fadeout = fixture.fade(601); fadeout.scale = 2;
    fixture.module.bind_fadeout(1, fadeout, {1});
    auto const VIEWS = fixture.module.dying_views(); CHECK(VIEWS.size() == 1);
    if (!VIEWS.empty()) {
        CHECK(VIEWS[0].source_device.x == 116); CHECK(VIEWS[0].source_device.y == 178);
        CHECK(VIEWS[0].source_device.w == 220); CHECK(VIEWS[0].source_device.h == 180);
    }
}
void isolated_mesh_buffers() {
    using MeshModule = CWindowAnimations<FakeWindowPort, StyleList<MeshStyle>>;
    Fixture fixture; fixture.window(1);
    MeshModule::Resources resources;
    MeshModule module{fixture.port, resources};
    Policy const POLICY{.style = MeshStyle::KIND, .enabled = true, .opening = true};
    module.configure(POLICY); module.update_settings<MeshStyle>({1}); module.track(1);
    module.frame({.monitor=1, .frame=1, .now=fixture.port.now});
    auto const HANDLE = fixture.port.transformers.at(1);
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{1};
    Draw<FakeCanvas> const DRAW{.canvas = canvas, .image = IMAGE, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    CHECK(module.draw(HANDLE, DRAW));
    auto  const&cache = fixture.port.visual_cache(HANDLE);
    CHECK(cache.buffers.window.size() == 1); CHECK(cache.scratch_buffers.window.empty());
    canvas.pass_kind = PassKind::WorkspaceSource;
    CHECK(module.draw(HANDLE, DRAW)); CHECK(cache.buffers.window.size() == 1);
    CHECK(cache.scratch_buffers.window.size() == 1);
    canvas.pass_kind = PassKind::Inert;
    CHECK(module.draw(HANDLE, DRAW)); CHECK(cache.buffers.window.size() == 1);
    CHECK(cache.scratch_buffers.window.size() == 2);
    module.end_all();
}
void independent_fadeouts_reversed() {
    Fixture fixture; fixture.window(1); fixture.window(2); fixture.frame(1);
    auto const FIRST = fixture.handle(1); auto const SECOND = fixture.handle(2);
    fixture.module.close(1, fixture.port.now); fixture.module.close(2, fixture.port.now);
    auto const SECOND_FADE = fixture.fade(701); fixture.module.bind_fadeout(2, SECOND_FADE, {2});
    auto const FIRST_FADE = fixture.fade(700); fixture.module.bind_fadeout(1, FIRST_FADE, {1});
    CHECK(fixture.module.active(FIRST)); CHECK(fixture.module.active(SECOND));
    CHECK(fixture.module.suppresses(700)); CHECK(fixture.module.suppresses(701));
    CHECK(fixture.module.dying_views().size() == 2);
}
void workspace_motion_and_teleport() {
    Fixture fixture; fixture.policy.opening = false; fixture.module.configure(fixture.policy);
    fixture.window(1, 1, false); fixture.frame(1);
    auto &window = fixture.port.windows.at(1);
    window.workspace_moving = true; window.workspace_offset.x = 10; fixture.frame(1);
    auto const HANDLE = fixture.handle();
    const auto MOTION = fixture.log().steps.back().motion;
    CHECK(MOTION.has_value());
    if (MOTION) { CHECK(MOTION->translated); }
    window.workspace_offset.x = 0; fixture.frame(1);
    CHECK(fixture.handle() == HANDLE); CHECK(fixture.log().steps.back().motion.has_value());
    window.pinned = true; fixture.frame(1); window.workspace_offset.x = 20; fixture.frame(1);
    CHECK(!fixture.log().steps.back().motion.has_value());
    window.body.x += 1000; window.capture.x += 1000; fixture.frame(1);
    CHECK(fixture.module.active(HANDLE)); CHECK(!fixture.log().steps.back().motion.has_value());
}
void capture_failure_releases_entry() {
    Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now); auto const FADEOUT = fixture.fade(702);
    fixture.port.fail_bind = true;
    bool threw = false;
    try { fixture.module.bind_fadeout(1, FADEOUT, {1}); }
    catch (const std::runtime_error &) { threw = true; }
    CHECK(threw); CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.port.visuals.empty());
    CHECK(!fixture.module.suppresses(702)); CHECK(!fixture.module.pending_for(1));
}
}
int main() try {
    opening_movement_and_settlement(); disabled_open_and_move(); monitor_frames_and_reattach();
    close_continuity_and_association(); close_timeout_invalid_and_fresh(); invalidation_and_restore();
    settings_style_reload_monitor(); stall_bounds();
    effects_and_stale_draw(); close_modes_and_disable(); close_mid_movement();
    tail_and_backdrop();
    capacity_and_fadeout_validation(); pending_invalidation(); screen_order_and_shutdown();
    screen_survives_window_and_allocation_failure();
    out_of_order_and_duplicate_fadeouts(); close_placement_and_invalid_fadeouts();
    pass_kind_retention_and_backdrop();
    independent_fadeouts_reversed(); workspace_motion_and_teleport(); capture_failure_releases_entry();
    isolated_mesh_buffers();
    return g_failures == 0 ? 0 : 1;
}
catch (const std::exception &error) {
    (void)std::fprintf(stderr, "unexpected test exception: %s\n", error.what());
    return 1;
}
catch (...) {
    (void)std::fprintf(stderr, "unexpected non-standard test exception\n");
    return 1;
}
