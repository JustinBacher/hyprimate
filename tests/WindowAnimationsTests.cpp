#include <cmath>
#include <concepts>
#include <functional>
#include <cstdint>
#include <optional>
#include <vector>

#include "config_settings_generated.hpp"
#include "Check.hpp"
#include "FakeWindowPort.hpp"
#include "WindowAnimations.hpp"
#include "CloseBatch.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
using namespace hyprimate::animation::test;

// Borrowed draw records remain ordinary assignable values, not reference-bound objects.
static_assert(std::assignable_from<Draw<FakeCanvas> &, const Draw<FakeCanvas> &>);
static_assert(std::assignable_from<ScreenDraw<FakeCanvas> &, const ScreenDraw<FakeCanvas> &>);
static_assert(std::assignable_from<MeshTarget<MeshVertex> &, const MeshTarget<MeshVertex> &>);

namespace {
struct Settings {
    double speed = 1;
    bool operator==(const Settings &) const = default;
};
struct State {
    std::uint64_t seed = 0;
    double progress = 0;
    bool closing = false;
    std::shared_ptr<int> lifetime = std::make_shared<int>(1);
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
    MonitorId draw_monitor = 0;
    WindowId draw_window = 0;
    FrameId draw_frame = 0;
    int draw_backdrop = 0;
    bool screen_active = false, stop_screen_on_tick = false;
    bool screen_enabled = false;
    std::optional<Box> drive;
    std::optional<Edge> reported_opening_edge;
    std::optional<Edge> closed_opening_edge;
    bool accept_open = true;
    bool draw_succeeds = true;
    bool release = false, snap = false, alpha = false, running = true, tail = false, backdrop = false;
    bool native_presentation = false;
    CloseStart close_result = CloseStart::Owned;
    std::function<void()> during_draw;
};
template<config::Style Kind> struct TestStyle {
    using State = ::State;
    using Settings = ::Settings;
    using Prepared = ::Settings;
    struct Resources : ResourcesBase<State, Settings, Prepared> {
        Log log;
        ResourcesBase<State, Settings, Prepared>::AnimationPool animations;
    };
    static constexpr auto KIND = Kind;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool NATIVE_PROTECTED_MAPPED = Kind == config::Style::Paper;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static Prepared prepare_settings(const Settings &settings) { return settings; }
    static State start(const Resources & /*resources*/, std::uint64_t seed) { return {.seed = seed}; }
    static bool open(State & /*state*/, const Place & /*place*/, Resources &resources, Effects &effects) {
        ++resources.log.opens;
        effects.opening_edge = resources.log.reported_opening_edge;
        return resources.log.accept_open;
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
                .backdrop = resources.log.backdrop, .tail = resources.log.tail,
                .native_presentation = resources.log.native_presentation};
    }
    static CloseStart close(State &state, const Place &place, Resources &resources) {
        ++resources.log.closes;
        resources.log.closed_opening_edge = place.opening_edge;
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
    static bool draw(State &state, const Draw<FakeCanvas> &draw, Resources &resources) {
        if (draw.real_pass) { ++resources.log.real_draws; }
        resources.log.draw_monitor = draw.monitor;
        resources.log.draw_window = draw.window;
        resources.log.draw_frame = draw.frame;
        resources.log.draw_backdrop = draw.backdrop != nullptr ? draw.backdrop->id : 0;
        draw.canvas->draws.push_back(draw.image->id);
        const auto SEED = state.seed;
        const std::weak_ptr<int> LIFETIME = state.lifetime;
        if (resources.log.during_draw) { resources.log.during_draw(); }
        CHECK(!LIFETIME.expired());
        if (!LIFETIME.expired()) { CHECK(state.seed == SEED); }
        return resources.log.draw_succeeds;
    }
    struct Screen {
        static void tick(Resources &resources, const ScreenFacts &facts) {
            resources.log.events.push_back(1);
            resources.log.screen_enabled = facts.enabled;
            if (facts.monitor != 0 && resources.log.stop_screen_on_tick) {
                resources.log.screen_active = false;
            }
        }
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
struct MeshStyle : TestStyle<config::Style::Cloth> {
    static bool draw(State &, const Draw<FakeCanvas> &, Resources &) = delete;
    template<Vertex V> static Mesh<V> mesh(State & /*state*/, const MeshTarget<V> &target, Resources & /*resources*/) {
        target.buffers->window.push_back(V{});
        return {.window = &target.buffers->window, .clip = std::nullopt};
    }
};
using Style = TestStyle<config::Style::Liquid>;
using Other = TestStyle<config::Style::Paper>;
using List = StyleList<Style, Other>;
using Module = CWindowAnimations<FakeWindowPort, List>;

struct Fixture {
    FakeWindowPort port;
    Module::Resources resources;
    Module module;
    Policy policy{.style = Style::KIND, .enabled = true, .opening = true, .movement = true,
                  .closing = true, .workspace_movement = true, .close_available = true};
    FrameId frame_a = 0, frame_b = 0;
    explicit Fixture(std::uint64_t seed = 0) : module(port, resources, seed) {
        port.monitors.emplace(1, ScreenFacts{.monitor = 1, .now = {}, .screen = {.x=0, .y=0, .w=800, .h=600}, .work_area = {.x=0, .y=0, .w=800, .h=600}});
        port.monitors.emplace(2, ScreenFacts{.monitor = 2, .now = {}, .screen = {.x=800, .y=0, .w=800, .h=600}, .work_area = {.x=800, .y=0, .w=800, .h=600}});
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
        FadeoutFacts facts{.now = port.now, .fadeout = fadeout_id, .monitor = monitor, .workspace = 1, .native_box = {.x=95, .y=95, .w=110, .h=90},
                           .image_size = {.x=110, .y=90}, .scale = 1, .alpha = 1, .alive = true, .image_available = true};
        port.fadeouts[fadeout_id] = facts;
        return facts;
    }
};

void seated_interval_uses_native_surfaces_before_slot_end() {
    Fixture f;
    f.window(1);
    f.frame(1);
    const auto handle = f.handle();
    FakeCanvas canvas;
    FakeCanvas::Image image{7};
    const Draw<FakeCanvas> draw{.canvas=&canvas, .image=&image};
    CHECK(f.module.draw(handle, draw));
    f.log().native_presentation = true;
    f.frame(1);
    f.module.before_window(1, canvas);
    CHECK(!f.port.transformers.contains(1));
    CHECK(f.module.active(handle));
    CHECK(!f.module.draw(handle, draw));
    CHECK(canvas.draws.size() == 1);
    f.log().native_presentation = false;
    f.frame(1);
    CHECK(f.handle() == handle);
    CHECK(f.module.draw(handle, draw));
    f.log().running = false;
    f.frame(1);
    CHECK(!f.module.active(handle));
    CHECK(f.log().last_end == End::Completed);
}

void protected_window_keeps_native_presentation() {
    Fixture f;
    f.policy.style = Other::KIND;
    f.module.configure(f.policy);
    f.window(1);
    f.port.windows.at(1).no_screen_share = true;
    f.window(2);
    f.frame(1);
    CHECK(!f.port.transformers.contains(1));
    CHECK(f.resources.get<Other>().log.opens == 1);
    const auto public_handle = f.handle(2);
    f.port.windows.at(2).no_screen_share = true;
    f.frame(1);
    CHECK(!f.module.active(public_handle));
    CHECK(f.port.transformers.empty());
}

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

void settled_opening_edge_survives_until_close() {
    Fixture fixture;
    fixture.log().reported_opening_edge = Edge::Left;
    fixture.window(1);
    fixture.frame(1);
    const auto ORIGINAL = fixture.handle();
    fixture.log().running = false;
    fixture.frame(1);
    CHECK(!fixture.module.active(ORIGINAL));
    CHECK(fixture.log().last_end == End::Completed);
    fixture.module.close(1, fixture.port.now);
    CHECK(fixture.module.pending_for(1).has_value());
    CHECK(fixture.log().closed_opening_edge == Edge::Left);
    CHECK(fixture.log().close_seed == 0);

    fixture.window(2, 1, false);
    fixture.module.close(2, fixture.port.now);
    CHECK(!fixture.log().closed_opening_edge.has_value());
}

void new_opening_attempt_clears_previous_edge() {
    for (const bool DISABLE_OPEN : {false, true}) {
        Fixture fixture;
        fixture.log().reported_opening_edge = Edge::Right;
        fixture.window(1); fixture.frame(1);
        fixture.log().running = false; fixture.frame(1);
        fixture.port.windows.at(1).animating_in = false;
        fixture.frame(1);
        fixture.log().accept_open = false;
        if (DISABLE_OPEN) {
            fixture.policy.opening = false;
            fixture.module.configure(fixture.policy);
        }
        fixture.port.windows.at(1).animating_in = true;
        fixture.frame(1);
        fixture.module.close(1, fixture.port.now);
        CHECK(fixture.module.pending_for(1).has_value());
        CHECK(!fixture.log().closed_opening_edge.has_value());
        CHECK(fixture.log().opens == (DISABLE_OPEN ? 1 : 2));
    }
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
    auto const FADE = fixture.fade(10); fixture.module.bind_fadeout(2, FADE, {.id = 10}); CHECK(fixture.module.pending_for(1) == HANDLE);
    fixture.module.bind_fadeout(1, FADE, {.id = 10}); CHECK(fixture.module.active(HANDLE)); CHECK(fixture.module.suppresses(10));
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
    auto fade = fixture.fade(11); fade.blurred = true; fixture.module.bind_fadeout(2, fade, {.id = 11});
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

void expired_windows_release_tracking_without_erasing_closes() {
    Fixture mapped;
    mapped.window(1); mapped.frame(1);
    const auto HANDLE = mapped.handle();
    mapped.log().drive = Box{.x=120, .y=100, .w=90, .h=70};
    mapped.frame(1);
    mapped.port.windows.erase(1);
    mapped.frame(2);
    CHECK(!mapped.module.active(HANDLE));
    CHECK(mapped.log().last_end == End::Interrupted);
    CHECK(mapped.port.visuals.empty());
    CHECK(std::ranges::find(mapped.port.forgotten, 1) != mapped.port.forgotten.end());

    Fixture pending;
    pending.window(1); pending.frame(1);
    const auto PENDING_HANDLE = pending.handle();
    pending.module.close(1, pending.port.now);
    pending.port.windows.erase(1);
    pending.frame(1);
    CHECK(pending.module.active(PENDING_HANDLE));
    CHECK(pending.module.pending_for(1) == PENDING_HANDLE);
    CHECK(pending.port.forgotten.empty());
    pending.frame(1, 1.1);
    CHECK(!pending.module.active(PENDING_HANDLE));
    CHECK(std::ranges::find(pending.port.forgotten, 1) != pending.port.forgotten.end());

    Fixture dying;
    dying.window(1); dying.frame(1);
    const auto DYING_HANDLE = dying.handle();
    dying.module.close(1, dying.port.now);
    dying.module.bind_fadeout(1, dying.fade(88), {.id = 88});
    dying.port.windows.erase(1);
    dying.frame(1);
    CHECK(dying.module.active(DYING_HANDLE));
    CHECK(dying.module.suppresses(88));
}

void monitorless_windows_rebaseline_without_losing_their_animation() {
    Fixture fixture;
    fixture.window(1); fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.port.unobservable.insert(1);
    fixture.frame(1, 3.0);
    CHECK(fixture.module.active(HANDLE));
    CHECK(fixture.port.forgotten.empty());
    fixture.port.unobservable.erase(1);
    fixture.frame(1);
    CHECK(fixture.module.active(HANDLE));
    CHECK(fixture.handle() == HANDLE);
    CHECK(fixture.log().steps.back().elapsed == 0);
    CHECK(!fixture.log().steps.back().motion.has_value());

    Fixture driven;
    driven.window(1); driven.frame(1);
    driven.log().drive = Box{.x=120, .y=100, .w=90, .h=70};
    driven.frame(1);
    driven.port.unobservable.insert(1);
    driven.policy.enabled = false;
    driven.module.configure(driven.policy);
    CHECK(driven.port.windows.at(1).body.x == 100);
}

void hidden_windows_require_a_valid_workspace_tail() {
    Fixture hidden;
    hidden.window(1);
    hidden.port.windows.at(1).hidden = true;
    hidden.frame(1);
    CHECK(hidden.port.transformers.empty());
    CHECK(hidden.log().opens == 0);

    Fixture ordinary;
    ordinary.window(1); ordinary.frame(1);
    const auto HANDLE = ordinary.handle();
    ordinary.port.windows.at(1).hidden = true;
    ordinary.frame(1);
    CHECK(!ordinary.module.active(HANDLE));
    CHECK(ordinary.log().last_end == End::Interrupted);

    Fixture tail;
    tail.log().tail = true;
    tail.window(1); tail.frame(1);
    const auto TAIL_HANDLE = tail.handle();
    tail.port.windows.at(1).hidden = true;
    tail.port.windows.at(1).workspace_visible = false;
    tail.frame(1);
    CHECK(tail.module.active(TAIL_HANDLE));
    tail.port.windows.at(1).pinned = true;
    tail.frame(1);
    CHECK(!tail.module.active(TAIL_HANDLE));
}

void runtime_seed_is_injected_without_changing_fresh_close_seed() {
    Fixture fixture{41};
    fixture.window(1); fixture.frame(1);
    CHECK(fixture.state(fixture.handle())->seed == 42);
    fixture.window(2, 1, false);
    fixture.module.close(2, fixture.port.now);
    CHECK(fixture.log().close_seed == 0);
}
void settings_style_reload_monitor() {
    Fixture fixture; fixture.window(1); fixture.frame(1); const auto HANDLE = fixture.handle(); auto  const*state = fixture.state(HANDLE);
    fixture.module.update_settings<Style>({3}); fixture.frame(1, 0.1);
    CHECK(fixture.handle() == HANDLE); CHECK(fixture.state(HANDLE) == state); CHECK(std::abs(state->progress - 0.3) < 1e-8);
    fixture.module.close(1, fixture.port.now); auto const FADE = fixture.fade(20); fixture.module.bind_fadeout(1, FADE, {.id = 20});
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
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{.id = 17};
    Draw<FakeCanvas> const DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
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
            auto const FADE = fixture.fade(40); fixture.module.bind_fadeout(1, FADE, {.id = 40});
            CHECK(fixture.module.active(HANDLE)); CHECK(!fixture.module.suppresses(40));
            fixture.module.native_finished(40); CHECK(!fixture.module.active(HANDLE));
            CHECK(fixture.log().last_end == End::Completed);
        }
    }
    Fixture fixture; fixture.window(1); fixture.frame(1); auto const HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now); auto const FADE = fixture.fade(41); fixture.module.bind_fadeout(1, FADE, {.id = 41});
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
    auto const HANDLE = fixture.handle(); FakeCanvas canvas;
    FakeCanvas::Image const FIRST{.id = 1, .size = {.x=220, .y=180}}; FakeCanvas::Image const MATTE{.id = 2};
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.size() == 1);
    Draw<FakeCanvas> const DRAW{.canvas = &canvas, .image = &FIRST, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {.x=7, .y=8}, .image_size = {},
                          .real_pass = true, .frame = 1};
    CHECK(fixture.module.draw(HANDLE, DRAW));
    Draw<FakeCanvas> const SECOND{.canvas = &canvas, .image = &MATTE, .source = {}, .output = {},
                            .canvas_box = {}, .image_origin = {}, .image_size = {},
                            .real_pass = true, .frame = 2};
    CHECK(fixture.module.draw(HANDLE, SECOND));
    auto retained = fixture.port.retained_mapped_image(HANDLE);
    CHECK(retained.has_value());
    if (retained) { CHECK(retained->image->id == 1); CHECK(retained->image_origin.x == 7); }
    fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
    auto const COUNT = canvas.draws.size(); fixture.module.after_windows(1, canvas);
    CHECK(canvas.draws.size() == COUNT);
    CHECK(fixture.port.queued_tails.size() == 1);
    if (fixture.port.queued_tails.empty()) { return; }
    const auto QUEUED = fixture.port.queued_tails.back();
    CHECK(QUEUED.image.id == 1); CHECK(QUEUED.draw.image_size.x == 220);
    CHECK(QUEUED.draw.output.x == 95 && QUEUED.draw.output.w == 100);
    Draw<FakeCanvas> const DEFERRED{.canvas = &canvas, .image = &QUEUED.image, .backdrop = nullptr,
        .source = QUEUED.draw.source, .output = QUEUED.draw.output, .canvas_box = QUEUED.draw.canvas_box,
        .image_origin = QUEUED.draw.image_origin, .image_size = QUEUED.draw.image_size, .scale = QUEUED.draw.scale,
        .alpha = QUEUED.draw.alpha, .real_pass = true, .monitor = QUEUED.draw.monitor, .window = 0, .frame = QUEUED.draw.frame};
    CHECK(fixture.module.draw_tail(QUEUED.handle, DEFERRED, QUEUED.draw.no_screen_share));
    CHECK(canvas.draws.back() == 1);
    fixture.port.windows.at(1).workspace_visible = true;
    fixture.log().tail = false; fixture.frame(1);
    CHECK(!fixture.port.retained_mapped_image(HANDLE));
    fixture.log().tail = true; fixture.frame(1);
    CHECK(fixture.module.draw(HANDLE, SECOND));
    retained = fixture.port.retained_mapped_image(HANDLE);
    CHECK(retained && retained->image->id == 2);
    fixture.module.end_all(); CHECK(!fixture.port.retained_mapped_image(HANDLE));
    CHECK(!fixture.module.draw_tail(QUEUED.handle, DEFERRED, QUEUED.draw.no_screen_share));
}
void retained_tail_final_draw_privacy() {
    // All images are deterministic synthetic IDs. Queue locally, then change policy
    // without a model tick: the final pass must not trust the cached observation.
    for (int pass = 0; pass < 6; ++pass) {
        for (int change = 0; change < 10; ++change) {
            Fixture fixture; fixture.log().tail = true; fixture.window(1); fixture.frame(1);
            const auto HANDLE = fixture.handle();
            FakeCanvas canvas; const FakeCanvas::Image IMAGE{.id = 73};
            const Draw<FakeCanvas> INITIAL{.canvas = &canvas, .image = &IMAGE, .source = {},
                .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
            CHECK(fixture.module.draw(HANDLE, INITIAL));
            fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
            fixture.module.after_windows(1, canvas);
            CHECK(fixture.port.queued_tails.size() == 1);
            if (fixture.port.queued_tails.empty()) { continue; }
            const auto QUEUED = fixture.port.queued_tails.back();
            if (pass == 1) { canvas.pass_kind = PassKind::WorkspaceSource; }
            if (pass == 2) { canvas.pass_kind = PassKind::Inert; }
            if (pass == 3) { canvas.rendering_capture = true; }
            if (pass == 4) { canvas.rendering_snapshot = true; }
            if (pass == 5) { canvas.destination_live = true; }
            if (change == 1) { fixture.port.windows.at(1).no_screen_share = true; }
            if (change == 2) { fixture.port.windows.erase(1); }
            if (change == 3) { fixture.port.unobservable.insert(1); }
            if (change == 4) { fixture.port.windows.at(1).window = 999; }
            if (change == 5) { fixture.port.windows.at(1).mapped = false; }
            if (change == 6) { fixture.port.unlocked = false; }
            if (change == 7) { fixture.port.session_available = false; }
            if (change == 8) { fixture.policy.enabled = false; fixture.module.configure(fixture.policy); }
            if (change == 9) { fixture.policy.inert = true; fixture.module.configure(fixture.policy); }
            const Draw<FakeCanvas> DEFERRED{.canvas = &canvas, .image = &QUEUED.image,
                .source = QUEUED.draw.source, .output = QUEUED.draw.output,
                .canvas_box = QUEUED.draw.canvas_box, .image_origin = QUEUED.draw.image_origin,
                .image_size = QUEUED.draw.image_size, .real_pass = true};
            const auto COUNT = canvas.draws.size();
            const bool ALLOWED = change == 0 || (change == 1 && pass == 0);
            CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share) == ALLOWED);
            CHECK(canvas.draws.size() == COUNT + (ALLOWED ? 1U : 0U));
            // Queue-time denial is independent of final-pass denial, including local
            // source disappearance. A denied capture never destroys the local tail.
            fixture.port.queued_tails.clear();
            fixture.module.after_windows(1, canvas);
            CHECK(fixture.port.queued_tails.size() == (ALLOWED ? 1U : 0U));
            if (change == 1 && pass != 0) {
                canvas.pass_kind = PassKind::Live;
                canvas.rendering_capture = false; canvas.rendering_snapshot = false;
                canvas.destination_live = false;
                CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
            }
        }
    }
}
void retained_tail_pixel_provenance() {
    Fixture fixture; fixture.log().tail = true; fixture.window(1); fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.port.windows.at(1).no_screen_share = true;
    FakeCanvas canvas; const FakeCanvas::Image PRIVATE{.id = 81}, PUBLIC{.id = 82};
    const Draw<FakeCanvas> INITIAL{.canvas = &canvas, .image = &PRIVATE, .source = {},
        .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    CHECK(fixture.module.draw(HANDLE, INITIAL));
    fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.port.queued_tails.size() == 1);
    if (fixture.port.queued_tails.empty()) { return; }
    const auto QUEUED = fixture.port.queued_tails.back();
    CHECK(QUEUED.draw.no_screen_share);
    fixture.port.windows.at(1).no_screen_share = false;
    const Draw<FakeCanvas> DEFERRED{.canvas = &canvas, .image = &QUEUED.image, .source = {},
        .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
    canvas.pass_kind = PassKind::WorkspaceSource;
    CHECK(!fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
    fixture.port.queued_tails.clear(); fixture.module.after_windows(1, canvas);
    CHECK(fixture.port.queued_tails.empty());
    // Replacing the retained cache with public pixels must not declassify a
    // previously queued private image from the same live animation handle.
    canvas.pass_kind = PassKind::Live;
    fixture.port.windows.at(1).workspace_visible = true;
    fixture.log().tail = false; fixture.frame(1);
    fixture.log().tail = true; fixture.frame(1);
    auto fresh = INITIAL; fresh.image = &PUBLIC;
    CHECK(fixture.module.draw(HANDLE, fresh));
    canvas.pass_kind = PassKind::WorkspaceSource;
    CHECK(!fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
    CHECK(fixture.module.draw_tail(HANDLE, fresh, false));
    fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.port.queued_tails.size() == 1);
    CHECK(!fixture.port.queued_tails.empty() && !fixture.port.queued_tails.back().draw.no_screen_share);
}
void retained_tail_retention_denial() {
    Fixture fixture; fixture.log().tail = true; fixture.window(1); fixture.frame(1);
    const auto HANDLE = fixture.handle();
    FakeCanvas canvas; const FakeCanvas::Image IMAGE{.id = 83};
    const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {},
        .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    fixture.port.unlocked = false;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    CHECK(!fixture.port.retained_mapped_image(HANDLE));
    fixture.port.unlocked = true; fixture.port.session_available = false;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    CHECK(!fixture.port.retained_mapped_image(HANDLE));
    fixture.port.session_available = true; canvas.rendering_capture = true;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    CHECK(!fixture.port.retained_mapped_image(HANDLE));
    canvas.rendering_capture = false; canvas.rendering_snapshot = true;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    CHECK(!fixture.port.retained_mapped_image(HANDLE));
    canvas.rendering_snapshot = false;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    CHECK(fixture.port.retained_mapped_image(HANDLE).has_value());
    // The same framebuffer can be rendered again while private, then become
    // shareable. Its retained pixels stay private until the tail cache is released.
    fixture.port.windows.at(1).no_screen_share = true;
    CHECK(fixture.module.draw(HANDLE, DRAW));
    fixture.port.windows.at(1).no_screen_share = false;
    canvas.pass_kind = PassKind::WorkspaceSource;
    CHECK(!fixture.module.draw_tail(HANDLE, DRAW, false));
    canvas.pass_kind = PassKind::Live;
    CHECK(fixture.module.draw_tail(HANDLE, DRAW, false));
}
void retained_tail_rechecks_final_submission() {
    for (int change = 0; change < 9; ++change) {
        Fixture fixture; fixture.log().tail = true; fixture.window(1); fixture.frame(1);
        const auto HANDLE = fixture.handle();
        FakeCanvas canvas; const FakeCanvas::Image IMAGE{.id = 90};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {},
            .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
        CHECK(fixture.module.draw(HANDLE, DRAW));
        fixture.port.windows.at(1).workspace_visible = false; fixture.frame(1);
        // DestinationLive remains Live for state advancement, but its target
        // framebuffer is retained and must not receive protected window pixels.
        canvas.destination_live = true;
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.port.queued_tails.size() == 1);
        if (fixture.port.queued_tails.empty()) { continue; }
        const auto QUEUED = fixture.port.queued_tails.back();
        fixture.log().during_draw = [&] {
            if (change == 1) { fixture.port.windows.at(1).no_screen_share = true; }
            if (change == 2) { fixture.port.windows.erase(1); }
            if (change == 3) { fixture.port.unobservable.insert(1); }
            if (change == 4) { fixture.port.windows.at(1).window = 999; }
            if (change == 5) { fixture.port.unlocked = false; }
            if (change == 6) { fixture.policy.inert = true; fixture.module.configure(fixture.policy); }
            if (change == 7) { fixture.port.visual_cache(HANDLE).image = FakeCanvas::Image{.id = 91}; }
            if (change == 8) { fixture.port.windows.at(1).mapped = false; }
        };
        const auto REAL_DRAWS = fixture.log().real_draws;
        int submissions = 0;
        CHECK(fixture.module.display_tail(HANDLE, DRAW, QUEUED.draw.no_screen_share,
            [&] { ++submissions; }) == (change == 0));
        CHECK(submissions == (change == 0 ? 1 : 0));
        CHECK(fixture.log().real_draws == REAL_DRAWS + 1);
    }
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
        fixture.module.bind_fadeout(window, FADEOUT, {.id = static_cast<int>(window)});
        CHECK(fixture.module.suppresses(100 + window));
    }
    auto const OVERFLOW = fixture.handle(17);
    fixture.module.close(17, fixture.port.now);
    fixture.module.bind_fadeout(17, fixture.fade(117), {.id = 17});
    CHECK(!fixture.module.active(OVERFLOW)); CHECK(!fixture.module.suppresses(117));
    auto const ORPHAN = fixture.fade(300);
    fixture.module.bind_fadeout(999, ORPHAN, {.id = 1});
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
        fixture.module.bind_fadeout(1, fadeout, {.id = 1});
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
    fixture.module.bind_fadeout(2, fixture.fade(400), {.id = 2});
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

void screen_policy_requires_an_enabled_feature() {
    Fixture fixture;
    fixture.frame(1);
    CHECK(fixture.log().screen_enabled);
    fixture.log().screen_active = true;
    fixture.policy.style = Other::KIND;
    fixture.module.configure(fixture.policy);
    CHECK(fixture.log().screen_enabled);
    fixture.frame(1);
    CHECK(fixture.log().screen_enabled);
    fixture.policy.opening = false;
    fixture.policy.movement = false;
    fixture.policy.closing = false;
    fixture.module.configure(fixture.policy);
    CHECK(!fixture.log().screen_enabled);
    fixture.frame(1);
    CHECK(!fixture.log().screen_enabled);

    Fixture retained;
    retained.window(1); retained.frame(1);
    const auto HANDLE = retained.handle();
    retained.module.close(1, retained.port.now);
    retained.module.bind_fadeout(1, retained.fade(99), {.id = 99});
    retained.policy.style = Other::KIND;
    retained.module.configure(retained.policy);
    CHECK(retained.module.active(HANDLE));
    CHECK(retained.log().screen_enabled);
    retained.frame(1);
    CHECK(retained.log().screen_enabled);
}

void screen_damages_its_final_frame() {
    Fixture fixture;
    fixture.log().screen_active = true;
    fixture.frame(1);
    CHECK(!fixture.port.damage_log.empty());
    if (!fixture.port.damage_log.empty()) {
        const auto &first = fixture.port.damage_log.back();
        CHECK(first.x == 0 && first.y == 0 && first.w == 800 && first.h == 600);
    }
    fixture.port.damage_log.clear();
    fixture.log().stop_screen_on_tick = true;
    fixture.frame(1);
    CHECK(!fixture.module.needs_frame(1));
    CHECK(!fixture.port.damage_log.empty());
    if (!fixture.port.damage_log.empty()) {
        const auto &last = fixture.port.damage_log.back();
        CHECK(last.x == 0 && last.y == 0 && last.w == 800 && last.h == 600);
    }
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
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{.id = 8};
    Draw<FakeCanvas> const DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
                          .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    canvas.pass_kind = PassKind::Inert;
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.empty());
    fixture.module.draw(HANDLE, DRAW); CHECK(!fixture.port.retained_mapped_image(HANDLE));
    CHECK(fixture.log().draw_backdrop == 0);
    CHECK(fixture.log().draw_monitor == 1 && fixture.log().draw_window == 1);
    CHECK(fixture.log().draw_frame == fixture.frame_a);
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().real_draws == 0); CHECK(fixture.log().real_screen_draws == 0);
    canvas.pass_kind = PassKind::WorkspaceSource;
    fixture.module.before_window(1, canvas); CHECK(fixture.port.backdrop_captures.size() == 1);
    fixture.module.draw(HANDLE, DRAW); CHECK(!fixture.port.retained_mapped_image(HANDLE));
    CHECK(fixture.log().draw_backdrop == 2);
    fixture.module.after_windows(1, canvas);
    CHECK(fixture.log().real_draws == 0); CHECK(fixture.log().real_screen_draws == 0);
    CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 0);
    CHECK(fixture.port.visual_cache(HANDLE).scratch_backdrops == 1);
    canvas.pass_kind = PassKind::Live;
    fixture.module.draw(HANDLE, DRAW); CHECK(fixture.port.retained_mapped_image(HANDLE).has_value());
    fixture.module.before_window(1, canvas); fixture.module.after_windows(1, canvas);
    fixture.module.draw(HANDLE, DRAW);
    CHECK(fixture.log().draw_backdrop == 1);
    CHECK(fixture.log().real_draws == 2); CHECK(fixture.log().real_screen_draws == 2);
    CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 1);
}
void out_of_order_and_duplicate_fadeouts() {
    Fixture fixture; fixture.window(1); fixture.window(2); fixture.frame(1);
    auto const FIRST = fixture.handle(1); auto const SECOND = fixture.handle(2);
    fixture.module.close(1, fixture.port.now); fixture.module.close(2, fixture.port.now);
    auto const FADEOUT = fixture.fade(500);
    fixture.module.bind_fadeout(2, FADEOUT, {.id = 2});
    CHECK(fixture.module.active(SECOND)); CHECK(fixture.module.pending_for(1) == FIRST);
    fixture.module.bind_fadeout(1, FADEOUT, {.id = 1});
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
        fixture.module.bind_fadeout(1, fadeout, {.id = 1});
        CHECK(!fixture.module.active(HANDLE)); CHECK(!fixture.module.suppresses(600));
    }
    Fixture fixture; fixture.window(1);
    auto &window = fixture.port.windows.at(1);
    window.workspace_offset = {.x=10, .y=20}; window.floating_offset = {.x=3, .y=4};
    window.screen = {.x=50, .y=30, .w=800, .h=600}; window.scale = 2;
    fixture.frame(1); fixture.module.close(1, fixture.port.now);
    auto fadeout = fixture.fade(601); fadeout.scale = 2;
    fixture.module.bind_fadeout(1, fadeout, {.id = 1});
    auto const VIEWS = fixture.module.dying_views(); CHECK(VIEWS.size() == 1);
    if (!VIEWS.empty()) {
        CHECK(VIEWS[0].source_device.x == 116); CHECK(VIEWS[0].source_device.y == 178);
        CHECK(VIEWS[0].source_device.w == 220); CHECK(VIEWS[0].source_device.h == 180);
    }
}
void refused_retention_keeps_native() {
    Fixture fixture;
    fixture.window(1);
    fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now);
    fixture.port.refuse_retention = true;
    const auto FADE = fixture.fade(810);
    fixture.module.bind_fadeout(1, FADE, {.id = 81});
    CHECK(fixture.log().last_end == End::Interrupted);
    CHECK(!fixture.module.pending_for(1));
    CHECK(!fixture.module.active(HANDLE));
    CHECK(fixture.port.find_visuals(HANDLE) == nullptr);
    CHECK(!fixture.port.suppressed.contains(810));
    CHECK(fixture.port.suppress_requests == 0);
    CHECK(!fixture.module.suppresses(810)); // No owned entry or completion tombstone.
    fixture.port.refuse_retention = false;
    fixture.module.bind_fadeout(1, FADE, {.id = 81});
    CHECK(!fixture.module.dying_view_for(810)); // A late handoff cannot revive it.
    CHECK(!fixture.module.suppresses(810));
}

void retained_close_privacy_is_monotonic() {
    enum class PrivateSource { BindObservation, Fadeout, LaterRefresh };
    for (const auto MODE : {CloseStart::Owned, CloseStart::Native}) {
        for (const auto source : {PrivateSource::BindObservation, PrivateSource::Fadeout, PrivateSource::LaterRefresh}) {
            Fixture fixture;
            fixture.log().close_result = MODE;
            fixture.window(1); // Public at the close event.
            fixture.frame(1);
            fixture.module.close(1, fixture.port.now);
            fixture.port.windows.at(1).no_screen_share = source == PrivateSource::BindObservation;
            fixture.fade(811);
            auto &facts = fixture.port.fadeouts.at(811);
            facts.no_screen_share = source == PrivateSource::Fadeout;
            fixture.module.bind_fadeout(1, facts, {.id = 81});
            const auto BOUND = fixture.module.dying_view_for(811);
            CHECK(BOUND && BOUND->no_screen_share == (source != PrivateSource::LaterRefresh));
            fixture.port.windows.at(1).no_screen_share = false;
            facts.no_screen_share = source == PrivateSource::LaterRefresh;
            fixture.frame(1);
            const auto STRICT = fixture.module.dying_view_for(811);
            CHECK(STRICT && STRICT->no_screen_share);
            facts.no_screen_share = false;
            fixture.frame(1);
            const auto REFRESHED = fixture.module.dying_view_for(811);
            CHECK(REFRESHED && REFRESHED->no_screen_share);
            fixture.port.fadeouts.erase(811);
            fixture.module.native_finished(811);
            fixture.frame(1);
            const auto EXPIRED = fixture.module.dying_view_for(811);
            if (MODE == CloseStart::Owned) {
                CHECK(EXPIRED && EXPIRED->no_screen_share);
            } else {
                CHECK(!EXPIRED);
            }
        }
    }
}

void queued_close_rechecks_session() {
    Fixture fixture;
    fixture.window(1);
    fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now);
    fixture.module.bind_fadeout(1, fixture.fade(812), {.id = 81});
    fixture.frame(1);
    fixture.fade(813); // Native sibling has no retained association.

    struct Item { std::uint64_t id; std::optional<Handle> handle; };
    std::vector<Item> queue;
    const auto valid = [&](const Item &item, std::uint64_t current_id) {
        return close_association_valid(fixture.module, fixture.port, item.handle, item.id, current_id,
            fixture.port.fadeouts.contains(current_id), [](const DyingView &view) { return !view.no_screen_share; });
    };
    auto stale = HANDLE;
    ++stale.generation;
    CHECK(!valid({812, HANDLE}, 813));
    CHECK(!valid({812, stale}, 812));
    for (const Item &item : {Item{812, HANDLE}, Item{813, std::nullopt}}) {
        CHECK(submit_close([&] { return valid(item, item.id); }, [&] { return &item; }, [] {},
            [&](const Item *submitted) { queue.push_back(*submitted); }));
    }
    CHECK(queue.size() == 2);
    if (queue.size() != 2) { return; }
    const auto retained_valid = [&] { return valid(queue.front(), 812); };
    FakeCanvas canvas;
    const FakeCanvas::Image IMAGE{.id = 81};
    const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
        .canvas_box = {}, .image_origin = {}, .image_size = IMAGE.size, .real_pass = true};
    const auto replay = [&] {
        with_guarded_display(retained_valid, [&](const auto &guard_display) {
            if (fixture.module.draw(HANDLE, DRAW)) {
                guard_display([&] { canvas.composite(DRAW.output, DRAW.alpha); });
            }
        });
    };
    fixture.log().during_draw = [&] { fixture.port.unlocked = false; };
    replay(); // Lock inside the style callback, before its layer can be composited.
    CHECK(canvas.draws.size() == 1 && canvas.composites.empty());
    replay(); // Already locked on entry: no style call either.
    CHECK(canvas.draws.size() == 1 && canvas.composites.empty());
    CHECK(valid(queue.back(), 813)); // Unassociated native sibling remains eligible.
    fixture.log().during_draw = {};
    fixture.port.unlocked = true;
    replay();
    CHECK(canvas.draws.size() == 2 && canvas.composites.size() == 1);
}

void owned_close_draw_failure_restores_native() {
    Fixture fixture;
    fixture.window(1);
    fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now);
    fixture.module.bind_fadeout(1, fixture.fade(800), {.id = 80});
    fixture.frame(1);
    CHECK(fixture.module.suppresses(800));
    FakeCanvas canvas;
    const FakeCanvas::Image IMAGE{.id = 80};
    const Draw<FakeCanvas> draw{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
        .canvas_box = {}, .image_origin = {}, .image_size = IMAGE.size, .real_pass = true};
    fixture.log().draw_succeeds = false;
    canvas.pass_kind = PassKind::WorkspaceSource;
    CHECK(!fixture.module.draw(HANDLE, draw));
    CHECK(fixture.module.suppresses(800));
    canvas.pass_kind = PassKind::Live;
    CHECK(!fixture.module.draw(HANDLE, draw));
    CHECK(!fixture.module.suppresses(800));
    CHECK(!fixture.module.active(HANDLE));
    CHECK(fixture.log().last_end == End::Interrupted);
}

void dying_draw_adapter_lifecycle() {
    for (const auto MODE : {CloseStart::Owned, CloseStart::Native}) {
        Fixture fixture;
        fixture.log().close_result = MODE;
        fixture.window(1);
        fixture.port.windows.at(1).no_screen_share = true;
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        fixture.module.bind_fadeout(1, fixture.fade(800), {.id = 80});
        fixture.frame(1);
        const auto VIEWS = fixture.module.dying_views();
        CHECK(VIEWS.size() == 1);
        if (VIEWS.empty()) { continue; }
        const auto VIEW = VIEWS.front();
        CHECK(VIEW.no_screen_share);
        CHECK(VIEW.ownership == MODE);
        CHECK(fixture.module.suppresses(800) == (MODE == CloseStart::Owned));
        FakeCanvas canvas;
        const FakeCanvas::Image IMAGE{.id = 80};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = VIEW.render_box,
            .output = VIEW.bounds_device, .canvas_box = VIEW.bounds_device, .image_origin = {},
            .image_size = VIEW.image_size, .scale = VIEW.scale, .alpha = VIEW.alpha, .real_pass = true};
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(canvas.draws.size() == 1);
        fixture.module.native_finished(800);
        if (MODE == CloseStart::Owned) {
            CHECK(fixture.module.active(HANDLE));
            CHECK(fixture.module.draw(HANDLE, DRAW));
            fixture.log().running = false;
            fixture.frame(1);
        }
        CHECK(!fixture.module.active(HANDLE));
        fixture.log().running = true;
        fixture.window(2);
        fixture.frame(1);
        CHECK(fixture.handle(2) != HANDLE);
        const auto COUNT = canvas.draws.size();
        CHECK(!fixture.module.draw(HANDLE, DRAW));
        CHECK(canvas.draws.size() == COUNT);
    }
}

void dying_damage_follows_native_placement() {
    Fixture fixture;
    fixture.window(1);
    fixture.frame(1);
    fixture.module.close(1, fixture.port.now);
    fixture.module.bind_fadeout(1, fixture.fade(801), {.id = 81});
    fixture.frame(1);
    fixture.port.damage_log.clear();
    fixture.port.fadeouts.at(801).native_box.x += 40;
    fixture.frame(1);
    const auto VIEWS = fixture.module.dying_views();
    CHECK(!VIEWS.empty());
    if (VIEWS.empty()) { return; }
    const auto &box = VIEWS.front().bounds_device;
    CHECK(std::ranges::any_of(fixture.port.damage_log,
        [&](const Box &damage) { return damage.x == box.x && damage.y == box.y &&
            damage.w == box.w && damage.h == box.h; }));
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
    FakeCanvas canvas; FakeCanvas::Image const IMAGE{.id = 1};
    Draw<FakeCanvas> const DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
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
    auto const SECOND_FADE = fixture.fade(701); fixture.module.bind_fadeout(2, SECOND_FADE, {.id = 2});
    auto const FIRST_FADE = fixture.fade(700); fixture.module.bind_fadeout(1, FIRST_FADE, {.id = 1});
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
    try { fixture.module.bind_fadeout(1, FADEOUT, {.id = 1}); }
    catch (const std::runtime_error &) { threw = true; }
    CHECK(threw); CHECK(!fixture.module.active(HANDLE)); CHECK(fixture.port.visuals.empty());
    CHECK(!fixture.module.suppresses(702)); CHECK(!fixture.module.pending_for(1));
}
struct SyntheticCloseItem {
    CloseBatchKey key;
    bool owned = false;
    bool suppressed = false;
    bool image = true;
    bool private_content = false;
};
struct FailingCloseSpill : std::vector<SyntheticCloseItem> {
    void reserve(std::size_t) { throw std::bad_alloc{}; }
};
template<class Spill>
void mixed_close_batch(std::size_t count, bool capture) {
    std::vector<SyntheticCloseItem> source;
    for (std::size_t index = count; index > 0; --index) {
        source.push_back({.key = {.z_index = static_cast<int>(index % 4),
                .retained = index % 5 == 0, .identity = index},
            .owned = index % 3 == 0, .suppressed = index % 3 != 1,
            .image = index % 6 != 0, .private_content = index % 7 == 0});
    }
    const auto ENUMERATE = [&](auto emit) {
        for (const auto &item : source) {
            if (!capture || !item.private_content) { emit(item); }
        }
    };
    std::vector<CloseBatchKey> visited, native, owned;
    bool nested = false;
    draw_close_batch<SyntheticCloseItem, 16, Spill>(ENUMERATE, [&](const auto &item) {
        visited.push_back(item.key);
        if (!nested) {
            nested = true;
            // A nested noncapture pass uses its own scratch. Reallocation invalidates all source
            // pointers/iterators; the outer draw must retain a value, not a borrowed record.
            source.reserve(source.capacity() + 100);
            std::size_t inner_count = 0;
            draw_close_batch<SyntheticCloseItem>(ENUMERATE, [&](const auto &) { ++inner_count; });
            CHECK(inner_count == (capture ? count - count / 7 : count));
        }
        const bool use_owned = item.owned && item.image;
        int preparations = 0;
        submit_close([&] { return use_owned || !item.suppressed; }, [&] {
            if (++preparations == 1 && item.key.identity % 4 == 1) { throw std::bad_alloc{}; }
            return std::optional{item.key};
        }, [&] {
            if (item.key.identity % 3 == 1) { throw std::bad_alloc{}; }
        }, [&](auto content) { (use_owned ? owned : native).push_back(*content); });
    });
    std::vector<CloseBatchKey> expected, expected_native, expected_owned;
    for (const auto &item : source) {
        if (capture && item.private_content) { continue; }
        expected.push_back(item.key);
        if (item.owned && item.image) { expected_owned.push_back(item.key); }
        else if (!item.suppressed) { expected_native.push_back(item.key); }
    }
    std::ranges::sort(expected);
    std::ranges::sort(expected_native);
    std::ranges::sort(expected_owned);
    CHECK(visited == expected); CHECK(native == expected_native); CHECK(owned == expected_owned);
}
void close_batch_capacity_fallback_and_reentry() {
    for (const auto count : {16U, 17U, 40U}) {
        for (const bool capture : {false, true}) {
            mixed_close_batch<std::vector<SyntheticCloseItem>>(count, capture);
            mixed_close_batch<FailingCloseSpill>(count, capture);
        }
    }
    int submissions = 0;
    bool threw = false;
    try {
        draw_close_batch<SyntheticCloseItem>(
            [](auto emit) {
                emit(SyntheticCloseItem{.key = {.identity = 1}});
                emit(SyntheticCloseItem{.key = {.identity = 2}});
            },
            [&](const auto &) { ++submissions; throw std::bad_alloc{}; });
    } catch (const std::bad_alloc &) { threw = true; }
    CHECK(threw); CHECK(submissions == 1); // A partial submit never restarts collection.
}
void backdrop_failure_preserves_close_content() {
    for (const bool dim_queued : {false, true}) {
        std::vector<int> passes;
        submit_close([] { return true; }, [] { return 2; }, [&] {
            if (dim_queued) { passes.push_back(1); }
            throw std::bad_alloc{}; // Building the next optional backdrop pass.
        }, [&](int content) { passes.push_back(content); });
        CHECK(passes == (dim_queued ? std::vector<int>{1, 2} : std::vector<int>{2}));
    }
    int preparations = 0;
    bool allowed = true;
    CHECK(!submit_close([&] { return allowed; }, [&]() -> int {
        ++preparations;
        allowed = false;
        throw std::bad_alloc{};
    }, [] { CHECK(false); }, [](int) { CHECK(false); }));
    CHECK(preparations == 1);
}
void close_draw_rechecks_callback_invalidation() {
    for (const bool succeeded : {false, true}) {
        Fixture fixture;
        fixture.window(1); fixture.frame(1);
        const auto HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        fixture.module.bind_fadeout(1, fixture.fade(704), {.id = 4});
        FakeCanvas canvas;
        const FakeCanvas::Image IMAGE{.id = 4};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {},
            .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
        fixture.log().draw_succeeds = succeeded;
        const std::weak_ptr<int> STATE = fixture.state(HANDLE)->lifetime;
        fixture.log().during_draw = [&] {
            fixture.module.end_all();
            CHECK(!STATE.expired());
            CHECK(fixture.port.find_visuals(HANDLE) != nullptr);
            // Reuse the pool and grow model storage before the outer callback returns.
            for (WindowId id = 2; id < 40; ++id) { fixture.window(id); }
            fixture.frame(1);
            CHECK(!STATE.expired());
        };
        CHECK(!fixture.module.draw(HANDLE, DRAW));
        CHECK(!fixture.module.active(HANDLE));
        CHECK(fixture.module.active(fixture.handle(2)));
        CHECK(STATE.expired());
        CHECK(fixture.port.find_visuals(HANDLE) == nullptr);
    }
}
void nested_close_draw_keeps_outer_state_and_visuals() {
    Fixture fixture;
    fixture.window(1); fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now);
    fixture.module.bind_fadeout(1, fixture.fade(705), {.id = 5});
    const std::weak_ptr<int> STATE = fixture.state(HANDLE)->lifetime;
    FakeCanvas canvas;
    const FakeCanvas::Image IMAGE{.id = 5};
    const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {},
        .output = {}, .canvas_box = {}, .image_origin = {}, .image_size = {}, .real_pass = true};
    bool inner = false;
    fixture.log().during_draw = [&] {
        if (!inner) {
            inner = true;
            CHECK(!fixture.module.draw(HANDLE, DRAW));
            CHECK(!STATE.expired());
            CHECK(fixture.port.find_visuals(HANDLE) != nullptr);
        } else {
            fixture.module.end_all();
        }
    };
    CHECK(!fixture.module.draw(HANDLE, DRAW));
    CHECK(STATE.expired());
    CHECK(fixture.port.find_visuals(HANDLE) == nullptr);
    CHECK(fixture.log().ends == 1);
}
void close_submission_callback_contract() {
    CallbackLifetime owner;
    const auto TICKET = owner.ticket();
    std::vector<int> submitted;
    bool associated = true;
    CHECK(!submit_close([&] { return TICKET.valid() && associated; }, [] { return 1; },
        [&] { associated = false; }, [&](int pass) { submitted.push_back(pass); }));
    // A stale association skips only that item; valid siblings still submit exactly once.
    CHECK(submit_close([&] { return TICKET.valid(); }, [] { return 2; }, [] {},
        [&](int pass) { submitted.push_back(pass); }));
    CHECK(submitted == std::vector<int>{2});
    bool queued = false;
    CHECK(submit_close([&] { return TICKET.valid(); }, [] { return true; }, [] {},
        [&](bool pass) { queued = pass; }));
    CHECK(queued);
    owner.invalidate();
    CHECK(!submit_close([&] { return TICKET.valid(); }, [] { return 3; }, [] {},
        [&](int pass) { submitted.push_back(pass); }));
    CHECK(submitted == std::vector<int>{2});
    CHECK(owner.ticket().valid());
    CallbackLifetime::Ticket destroyed;
    {
        CallbackLifetime temporary;
        destroyed = temporary.ticket();
        CHECK(destroyed.valid());
    }
    CHECK(!destroyed.valid());
    CHECK(!destroyed.alive());
    // Exceptions after a submission must propagate without replaying either stage.
    int backdrops = 0, contents = 0;
    bool threw = false;
    try {
        submit_close([] { return true; }, [] { return 1; }, [&] { ++backdrops; },
            [&](int) { ++contents; throw std::bad_alloc{}; });
    } catch (const std::bad_alloc &) { threw = true; }
    CHECK(threw); CHECK(backdrops == 1); CHECK(contents == 1);
}
void close_submission_rechecks_model_suppression() {
    Fixture fixture;
    fixture.window(1); fixture.frame(1);
    const auto HANDLE = fixture.handle();
    fixture.module.close(1, fixture.port.now);
    const auto TICKET = fixture.module.callback_ticket();
    int native = 0, owned = 0;
    // A native item can acquire an owned association while its backdrop submits.
    CHECK(!submit_close(
        [&] { return TICKET.valid() && !fixture.module.suppresses(706); },
        [] { return 1; }, [&] { fixture.module.bind_fadeout(1, fixture.fade(706), {.id = 6}); },
        [&](int) { ++native; }));
    CHECK(fixture.module.suppresses(706));
    CHECK(submit_close(
        [&] { return TICKET.valid() && !fixture.module.suppresses(707); },
        [] { return 1; }, [] {}, [&](int) { ++native; }));
    CHECK(native == 1);
    // An owned association removed by a callback cannot display its copied source.
    CHECK(!submit_close([&] {
        const auto view = fixture.module.dying_view_for(706);
        return TICKET.valid() && view && view->handle == HANDLE;
    }, [] { return 1; }, [&] { fixture.module.monitor_changed(1); }, [&](int) { ++owned; }));
    CHECK(owned == 0);
    CHECK(!fixture.module.active(HANDLE));
    fixture.module.end_all();
    CHECK(!TICKET.valid());
}
void close_lookup_survives_view_scratch_rebuild() {
    Fixture fixture; fixture.window(1); fixture.frame(1);
    fixture.module.close(1, fixture.port.now);
    fixture.module.bind_fadeout(1, fixture.fade(703), {.id = 1});
    const auto VIEW = fixture.module.dying_view_for(703);
    CHECK(VIEW.has_value()); CHECK(!fixture.module.dying_view_for(0));
    CHECK(!fixture.module.dying_view_for(999));
    (void)fixture.module.dying_views();
    fixture.module.end_all();
    CHECK(!fixture.module.dying_view_for(703));
    CHECK(VIEW && VIEW->fadeout == 703); // Copied data survives scratch rebuild and entry removal.
}
}
int main() try {
    opening_movement_and_settlement(); settled_opening_edge_survives_until_close();
    seated_interval_uses_native_surfaces_before_slot_end();
    protected_window_keeps_native_presentation();
    new_opening_attempt_clears_previous_edge();
    disabled_open_and_move(); monitor_frames_and_reattach();
    close_continuity_and_association(); close_timeout_invalid_and_fresh(); invalidation_and_restore();
    expired_windows_release_tracking_without_erasing_closes();
    monitorless_windows_rebaseline_without_losing_their_animation();
    hidden_windows_require_a_valid_workspace_tail();
    runtime_seed_is_injected_without_changing_fresh_close_seed();
    settings_style_reload_monitor(); stall_bounds();
    effects_and_stale_draw(); close_modes_and_disable(); close_mid_movement();
    tail_and_backdrop();
    retained_tail_final_draw_privacy(); retained_tail_pixel_provenance();
    retained_tail_retention_denial();
    retained_tail_rechecks_final_submission();
    capacity_and_fadeout_validation(); pending_invalidation(); screen_order_and_shutdown();
    screen_survives_window_and_allocation_failure(); screen_damages_its_final_frame();
    screen_policy_requires_an_enabled_feature();
    out_of_order_and_duplicate_fadeouts(); close_placement_and_invalid_fadeouts();
    pass_kind_retention_and_backdrop();
    independent_fadeouts_reversed(); workspace_motion_and_teleport(); capture_failure_releases_entry();
    isolated_mesh_buffers();
    dying_draw_adapter_lifecycle(); dying_damage_follows_native_placement();
    owned_close_draw_failure_restores_native();
    refused_retention_keeps_native(); retained_close_privacy_is_monotonic();
    queued_close_rechecks_session();
    close_batch_capacity_fallback_and_reentry(); close_lookup_survives_view_scratch_rebuild();
    backdrop_failure_preserves_close_content();
    close_draw_rechecks_callback_invalidation();
    close_submission_callback_contract();
    nested_close_draw_keeps_outer_state_and_visuals();
    close_submission_rechecks_model_suppression();
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
