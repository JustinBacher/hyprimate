#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include "Check.hpp"
#include "FakeWindowPort.hpp"
#include "config_settings_generated.hpp"
#include "window/CloseBatch.hpp"
#include "window/WindowAnimations.hpp"

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
    enum class Publication { Unknown, Partial, Blank, Native };
    struct CropProof {
        Box crop, source;
        double scale;
    };
    struct State {
        std::uint64_t seed = 0;
        double progress = 0;
        bool closing = false;
        std::shared_ptr<int> lifetime = std::make_shared<int>(1);
        Publication publication = Publication::Unknown;
        std::optional<CropProof> reject_crop{};
        bool allow_source_bind = true;
        double pinned_speed = 1;
        bool blur_safe = false;
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
        bool release = false, snap = false, alpha = false, running = true, tail = false,
             backdrop = false;
        bool native_presentation = false;
        CloseStart close_result = CloseStart::Owned;
        Frame initial_close{.running = true, .bounds = {2, 3, 20, 18}};
        std::optional<Step> initial_close_step;
        int close_initializations = 0;
        bool require_publication = false;
        Publication next_publication = Publication::Partial;
        Publication closed_publication = Publication::Unknown;
        std::optional<CropProof> reject_crop{};
        std::function<void()> during_draw;
        double drawn_speed = 0;
        bool accepts_resize = false;
        bool bind_succeeds = true;
        bool accept_motion = true, blur_safe = false;
        std::vector<MotionKind> motions_started;
    };
    template <config::Style Kind>
    struct TestStyle {
        using State = ::State;
        using Settings = ::Settings;
        using Prepared = ::Settings;
        struct Resources : ResourcesBase<State, Settings, Prepared> {
            Log log;
            ResourcesBase<State, Settings, Prepared>::AnimationPool animations;
        };
        static constexpr auto KIND = Kind;
        static constexpr bool FIXED_CLOSE_PLACEMENT = Kind == config::Style::Paper || Kind == config::Style::Shader;
        static constexpr bool PUBLIC_CLOSE_ONLY = Kind == config::Style::Paper || Kind == config::Style::Shader;
        static constexpr bool NATIVE_PROTECTED_MAPPED = PUBLIC_CLOSE_ONLY;
        static constexpr bool PUBLIC_MAPPED_ONLY = Kind == config::Style::Shader;
        static constexpr bool PRESERVE_ON_RELOAD = Kind == config::Style::Shader;
        static constexpr bool EXCLUSIVE_OPEN = Kind == config::Style::Shader;
        static constexpr bool DRAWS_MAPPED_UNDER_BLUR = !EXCLUSIVE_OPEN;
        static constexpr bool DRAWS_UNDER_BLUR = false;
        static Prepared prepare_settings(const Settings &settings) {
            return settings;
        }
        static State start(const Resources &resources, std::uint64_t seed) {
            return {.seed = seed, .pinned_speed = resources.prepared.speed, .blur_safe = resources.log.blur_safe};
        }
        static bool moves(const Motion &motion, const Resources &resources) noexcept {
            return motion.translated || (resources.log.accepts_resize && motion.resized);
        }
        static bool can_begin_motion(const Resources &resources, MotionKind, bool blurred)
            requires(PUBLIC_MAPPED_ONLY) {
            return resources.log.accept_motion && (!blurred || resources.log.blur_safe);
        }
        static bool begin_motion(State &, const Place &, Resources &resources, MotionKind kind)
            requires(PUBLIC_MAPPED_ONLY) {
            resources.log.motions_started.push_back(kind);
            return resources.log.accept_motion;
        }
        static bool mapped_blur_safe(const State &state) noexcept
            requires(PUBLIC_MAPPED_ONLY) {
            return state.blur_safe;
        }
        static bool close_blur_safe(const State &state) noexcept
            requires(PUBLIC_MAPPED_ONLY) {
            return state.blur_safe;
        }
        static bool open(
            State & /*state*/, const Place & /*place*/, Resources &resources, Effects &effects) {
            ++resources.log.opens;
            effects.opening_edge = resources.log.reported_opening_edge;
            return resources.log.accept_open;
        }
        static Frame advance(
            State &state, const Step &step, Resources &resources, Effects &effects) {
            resources.log.events.push_back(2);
            resources.log.steps.push_back(step);
            state.progress += step.elapsed * resources.prepared.speed;
            effects.drive_body = resources.log.drive;
            effects.release_body = resources.log.release;
            effects.snap_to_goal = resources.log.snap;
            effects.settle_native_open_alpha = resources.log.alpha;
            return {.running = resources.log.running,
                .bounds = {0, 0, 100, 80},
                .backdrop = resources.log.backdrop,
                .tail = resources.log.tail,
                .native_presentation = resources.log.native_presentation};
        }
        static CloseStart close(State &state, const Place &place, Resources &resources) {
            ++resources.log.closes;
            resources.log.closed_opening_edge = place.opening_edge;
            resources.log.closed_state = &state;
            resources.log.close_progress = state.progress;
            resources.log.close_seed = state.seed;
            state.closing = true;
            resources.log.closed_publication = state.publication;
            state.reject_crop = resources.log.reject_crop;
            state.allow_source_bind = resources.log.bind_succeeds;
            state.blur_safe = resources.log.blur_safe;
            if (resources.log.require_publication && state.publication == Publication::Unknown) {
                return CloseStart::Declined;
            }
            return resources.log.close_result;
        }
        static bool bind_close_source(State &state, Box, const FakeCanvas::Image &image) noexcept
            requires(PUBLIC_MAPPED_ONLY) {
            return image.id > 0 && state.allow_source_bind;
        }
        static Frame initial_close_frame(State &, const Step &step, Resources &resources) noexcept
            requires(PUBLIC_CLOSE_ONLY) {
            ++resources.log.close_initializations;
            resources.log.initial_close_step = step;
            return resources.log.initial_close;
        }
        static void invalidate_mapped_publication(State &state, Resources &) noexcept
            requires(PUBLIC_CLOSE_ONLY) {
            state.publication = Publication::Unknown;
        }
        static void mapped_native_publication(State &state, Box, Box, Box, double, Resources &) noexcept
            requires(PUBLIC_CLOSE_ONLY) {
            state.publication = Publication::Native;
        }
        static bool accepts_close_crop(const State &state, Box crop, Box source, double scale) noexcept
            requires(PUBLIC_CLOSE_ONLY) {
            if (!state.reject_crop) {
                return true;
            }
            const auto &expected = *state.reject_crop;
            CHECK(crop.x == expected.crop.x && crop.y == expected.crop.y &&
                crop.w == expected.crop.w && crop.h == expected.crop.h);
            CHECK(source.x == expected.source.x && source.y == expected.source.y &&
                source.w == expected.source.w && source.h == expected.source.h && scale == expected.scale);
            return false;
        }
        static void end(State & /*state*/, Resources &resources, End why) noexcept {
            ++resources.log.ends;
            resources.log.last_end = why;
            resources.log.events.push_back(7);
        }
        static bool draw(State &state, const Draw<FakeCanvas> &draw, Resources &resources) {
            resources.log.drawn_speed = state.pinned_speed;
            if (draw.real_pass) {
                ++resources.log.real_draws;
            }
            resources.log.draw_monitor = draw.monitor;
            resources.log.draw_window = draw.window;
            resources.log.draw_frame = draw.frame;
            resources.log.draw_backdrop = draw.backdrop != nullptr ? draw.backdrop->id : 0;
            draw.canvas->draws.push_back(draw.image->id);
            const auto SEED = state.seed;
            const std::weak_ptr<int> LIFETIME = state.lifetime;
            if (resources.log.during_draw) {
                resources.log.during_draw();
            }
            CHECK(!LIFETIME.expired());
            if (!LIFETIME.expired()) {
                CHECK(state.seed == SEED);
            }
            if constexpr (PUBLIC_CLOSE_ONLY) {
                if (!state.closing && resources.log.draw_succeeds && draw.real_pass && draw.monitor == 1 &&
                    !draw.canvas->captures_pixels()) {
                    state.publication = resources.log.next_publication;
                }
            }
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
                if (draw.real_pass) {
                    ++resources.log.real_screen_draws;
                }
                resources.log.events.push_back(3);
            }
            static void after_windows(Resources &resources, const ScreenDraw<FakeCanvas> &draw) {
                if (draw.real_pass) {
                    ++resources.log.real_screen_draws;
                }
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
        template <Vertex V>
        static Mesh<V> mesh(
            State & /*state*/, const MeshTarget<V> &target, Resources & /*resources*/) {
            target.buffers->window.push_back(V{});
            return {.window = &target.buffers->window, .clip = std::nullopt};
        }
    };
    using Style = TestStyle<config::Style::Liquid>;
    using Other = TestStyle<config::Style::Paper>;
    using Effect = TestStyle<config::Style::Shader>;
    using List = StyleList<Style, Other, Effect>;
    using Module = CWindowAnimations<FakeWindowPort, List>;

    struct BubbleScreenStyle : TestStyle<config::Style::Bubble> {
        struct Resources : TestStyle::Resources {
            std::set<MonitorId> active_outputs;
        };
        struct Screen : TestStyle::Screen {
            static void tick(Resources &resources, const ScreenFacts &facts) {
                if (facts.monitor == 0 && !facts.enabled) {
                    resources.active_outputs.clear();
                }
            }
            static bool needs_frame(const Resources &resources, MonitorId monitor) {
                return resources.active_outputs.contains(monitor);
            }
            static void clear(Resources &resources, std::optional<MonitorId> monitor) {
                if (monitor) {
                    resources.active_outputs.erase(*monitor);
                } else {
                    resources.active_outputs.clear();
                }
            }
        };
    };

    struct ScreenCallbackPort : FakeWindowPort {
        std::function<void()> on_damage, on_schedule, on_scanout;

        void damage(const Box &box) {
            FakeWindowPort::damage(box);
            if (on_damage) on_damage();
        }
        void schedule(MonitorId monitor) {
            FakeWindowPort::schedule(monitor);
            if (on_schedule) on_schedule();
        }
        void recheck_scanout(MonitorId monitor) {
            FakeWindowPort::recheck_scanout(monitor);
            if (on_scanout) on_scanout();
        }
    };

    struct BubbleScreenFixture {
        using Controller =
            CWindowAnimations<ScreenCallbackPort, StyleList<BubbleScreenStyle, Other>>;
        ScreenCallbackPort port;
        Controller::Resources resources;
        Controller module{port, resources};
        Policy policy{.style = BubbleScreenStyle::KIND, .enabled = true, .opening = true};

        BubbleScreenFixture() {
            module.configure(policy);
            resources.get<BubbleScreenStyle>().active_outputs = {1, 2};
            for (const MonitorId monitor : {1, 2, 3}) {
                const Box box{.x = 800. * static_cast<double>(monitor), .y = 0, .w = 800, .h = 600};
                port.monitors.emplace(monitor, ScreenFacts{.monitor = monitor, .screen = box});
                module.frame({.monitor = monitor, .frame = 1, .now = port.now});
            }
            port.damage_log.clear();
            port.scheduled.clear();
            port.scanout.clear();
            resources.get<Other>().log.events.clear();
        }
    };

    struct Fixture {
        FakeWindowPort port;
        Module::Resources resources;
        Module module;
        Policy policy{.style = Style::KIND,
            .enabled = true,
            .opening = true,
            .movement = true,
            .closing = true,
            .workspace_movement = true,
            .close_available = true};
        FrameId frame_a = 0, frame_b = 0;
        explicit Fixture(std::uint64_t seed = 0) : module(port, resources, seed) {
            port.monitors.emplace(1,
                ScreenFacts{.monitor = 1,
                    .now = {},
                    .screen = {.x = 0, .y = 0, .w = 800, .h = 600},
                    .work_area = {.x = 0, .y = 0, .w = 800, .h = 600}});
            port.monitors.emplace(2,
                ScreenFacts{.monitor = 2,
                    .now = {},
                    .screen = {.x = 800, .y = 0, .w = 800, .h = 600},
                    .work_area = {.x = 800, .y = 0, .w = 800, .h = 600}});
            port.overlapping = {1, 2};
            module.configure(policy);
            module.update_settings<Style>({1});
            module.update_settings<Other>({1});
            module.update_settings<Effect>({2});
        }
        ~Fixture() {
            module.end_all();
        }
        Fixture(const Fixture &) = delete;
        Fixture &operator=(const Fixture &) = delete;
        Fixture(Fixture &&) = delete;
        Fixture &operator=(Fixture &&) = delete;
        Log &log() {
            return resources.get<Style>().log;
        }
        void window(WindowId window_id, MonitorId monitor = 1, bool opening = true) {
            Observation observation{.window = window_id,
                .monitor = monitor,
                .workspace = 1,
                .body = {.x = 100, .y = 100, .w = 100, .h = 80},
                .capture = {.x = 95, .y = 95, .w = 110, .h = 90},
                .goal_body = {.x = 100, .y = 100, .w = 100, .h = 80},
                .screen = {.x = 0, .y = 0, .w = 800, .h = 600},
                .work_area = {.x = 0, .y = 0, .w = 800, .h = 600},
                .mapped = true,
                .animating_in = opening};
            port.windows.emplace(window_id, observation);
            module.track(window_id);
        }
        void frame(MonitorId monitor, double elapsed = 0.016) {
            auto &frame_number = monitor == 1 ? frame_a : frame_b;
            module.frame(
                {.monitor = monitor, .frame = ++frame_number, .now = port.advance(elapsed)});
        }
        Handle handle(WindowId window_id = 1) {
            return port.transformers.at(window_id);
        }
        State *state(Handle handle) {
            return &resources.get<Style>()
                        .animations.get({.index = handle.index, .generation = handle.generation})
                        ->state;
        }
        FadeoutFacts fade(std::uint64_t fadeout_id, MonitorId monitor = 1) {
            FadeoutFacts facts{.now = port.now,
                .fadeout = fadeout_id,
                .monitor = monitor,
                .workspace = 1,
                .native_box = {.x = 95, .y = 95, .w = 110, .h = 90},
                .image_size = {.x = 110, .y = 90},
                .scale = 1,
                .alpha = 1,
                .alive = true,
                .image_available = true};
            port.fadeouts[fadeout_id] = facts;
            return facts;
        }
    };

    void opening_override_pins_settings_and_yields_to_native_movement() {
        Fixture f;
        f.policy.open_style = Effect::KIND;
        f.policy.during_workspace_switch = true;
        f.log().accepts_resize = true;
        f.module.configure(f.policy);
        f.window(1);
        f.frame(1);
        const auto handle = f.handle();
        CHECK(handle.style == Effect::KIND);
        CHECK(f.log().opens == 0 && f.resources.get<Effect>().log.opens == 1);

        f.module.reload();
        f.module.update_settings<Effect>({3});
        f.module.configure(f.policy);
        CHECK(f.handle() == handle && f.module.active(handle));
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 19};
        CHECK(f.module.draw(handle, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(f.resources.get<Effect>().log.drawn_speed == 2);
        CHECK(f.resources.get<Effect>().log.real_draws == 1);
        canvas.destination_live = true;
        CHECK(f.module.draw(handle, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(f.resources.get<Effect>().log.real_draws == 1);
        canvas.destination_live = false;
        CHECK(f.module.draw(handle, {.canvas = &canvas, .image = &image, .real_pass = true, .monitor = 2}));
        CHECK(f.resources.get<Effect>().log.real_draws == 1);

        // The host's own opening geometry is not a separate layout event.
        auto &window = f.port.windows.at(1);
        window.layout_animated = true;
        window.body.w -= 5;
        window.capture.w -= 5;
        f.frame(1);
        CHECK(f.handle() == handle && f.module.active(handle));
        CHECK(f.log().steps.empty());

        // A one-sample resize during the long opening must reach the selected
        // movement style immediately, not be swallowed by its previous owner.
        window.goal_body.w += 20;
        const double PREVIOUS_WIDTH = window.body.w;
        window.body.w += 10;
        window.capture.w += 10;
        f.frame(1);
        CHECK(!f.module.active(handle));
        CHECK(f.handle().style == Style::KIND && f.log().opens == 0);
        CHECK(f.log().steps.size() == 1 && f.log().steps.back().motion &&
            f.log().steps.back().motion->resized &&
            f.log().steps.back().motion->from_body.w == PREVIOUS_WIDTH &&
            f.log().steps.back().motion->body.w == PREVIOUS_WIDTH + 10);
        CHECK(f.resources.get<Effect>().log.last_end == End::Interrupted);
        f.module.close(1, f.port.now);
        CHECK(f.module.pending_for(1) && f.module.pending_for(1)->style == Style::KIND);
    }

    void opening_override_yields_to_a_single_drag_sample() {
        Fixture f;
        f.policy.open_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1);
        f.frame(1);
        const auto effect = f.handle();
        auto &window = f.port.windows.at(1);
        window.held = true;
        window.body.x += 10;
        window.capture.x += 10;
        f.frame(1);
        CHECK(!f.module.active(effect) && f.handle().style == Style::KIND);
        CHECK(f.log().steps.size() == 1 && f.log().steps.back().held &&
            f.log().steps.back().motion && f.log().steps.back().motion->translated);
        window.held = false;
        f.frame(1);
        CHECK(f.resources.get<Effect>().log.opens == 1);
    }

    void motion_overrides_preserve_samples_and_pin_the_active_generation() {
        Fixture f;
        f.policy.move_style = Effect::KIND;
        f.policy.resize_style = Effect::KIND;
        f.policy.opening = false;
        f.module.configure(f.policy);
        f.window(1, 1, false);
        auto &window = f.port.windows.at(1);
        window.workspace_offset = {20, 0};
        window.floating_offset = {0, 5};
        auto &effect = f.resources.get<Effect>().log;
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));
        window.capture.x -= 2;
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));  // Decoration-only change is neither event.

        window.held = true;
        window.body.x += 10;
        window.capture.x += 10;
        f.frame(1, .02);
        CHECK(f.port.transformers.contains(1));
        const auto move = f.handle();
        CHECK(move.style == Effect::KIND && effect.motions_started.back() == MotionKind::Move);
        auto sample = effect.steps.back().motion_geometry;
        CHECK(sample && sample->active && effect.steps.back().held);
        CHECK(sample->previous.x == 120 && sample->current.x == 130 && sample->current.y == 105);
        CHECK(std::abs(sample->sample_seconds - .02) < 1e-6);

        f.module.reload();
        f.module.update_settings<Effect>({3});
        f.module.configure(f.policy);
        f.frame(1);
        CHECK(f.handle() == move && effect.motions_started.size() == 1);
        CHECK(effect.steps.back().motion_geometry->active);  // A stationary grab is still active.
        window.held = false;
        f.frame(1);
        CHECK(f.handle() == move && !effect.steps.back().motion_geometry->active);
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 96};
        CHECK(f.module.draw(move, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(effect.drawn_speed == 2);
        window.held = true;
        f.frame(1);
        CHECK(f.handle() == move && effect.motions_started.size() == 1);
        CHECK(effect.steps.back().motion_geometry->active);
        window.held = false;

        // An edge resize includes translation. It must acquire the resize
        // program, keep this sample, and receive the newly prepared generation.
        window.body.x -= 6;
        window.body.w += 6;
        window.capture.x -= 6;
        window.capture.w += 6;
        f.frame(1);
        const auto resize = f.handle();
        CHECK(resize.style == Effect::KIND && resize != move && !f.module.active(move));
        CHECK(effect.motions_started.size() == 2 && effect.motions_started.back() == MotionKind::Resize);
        sample = effect.steps.back().motion_geometry;
        CHECK(sample && sample->active && sample->previous.w == 100 && sample->current.w == 106);
        CHECK(sample->previous.x == 130 && sample->current.x == 124);
        CHECK(f.module.draw(resize, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(effect.drawn_speed == 3);
        window.resize_held = true;
        f.frame(1);
        CHECK(f.handle() == resize && effect.steps.back().motion_geometry->active);
        f.policy.movement = false;
        f.module.configure(f.policy);
        CHECK(!f.port.transformers.contains(1) && !f.module.active(resize));
    }

    void motion_override_hands_off_open_and_falls_back_without_reading_private_pixels() {
        Fixture f;
        f.policy.open_style = Effect::KIND;
        f.policy.move_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1);
        auto &window = f.port.windows.at(1);
        auto &effect = f.resources.get<Effect>().log;
        f.frame(1);
        const auto open = f.handle();
        window.layout_animated = true;
        window.body.x -= 5;
        window.capture.x -= 5;
        f.frame(1);
        CHECK(f.handle() == open && effect.motions_started.empty());
        window.held = true;
        f.frame(1);
        const auto move = f.handle();
        CHECK(move != open && !f.module.active(open) && effect.motions_started.size() == 1);

        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 97};
        window.no_screen_share = true;
        CHECK(!f.module.draw(move, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(canvas.draws.empty());
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));

        window.no_screen_share = false;
        window.animating_in = false;
        window.layout_animated = false;
        effect.accept_motion = false;
        window.body.x += 5;
        window.capture.x += 5;
        f.frame(1);
        CHECK(!f.port.transformers.contains(1) && f.log().steps.empty());

        // Missing resize override delegates to the existing built-in rules.
        window.held = false;
        window.body.w += 5;
        window.capture.w += 5;
        f.log().accepts_resize = true;
        f.frame(1);
        CHECK(f.handle().style == Style::KIND);
    }

    void motion_override_respects_the_acquired_blur_capability() {
        Fixture f;
        f.policy.resize_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1, 1, false);
        auto &window = f.port.windows.at(1);
        f.port.blurred_windows.insert(1);
        window.resize_held = true;
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));
        auto &effect = f.resources.get<Effect>().log;
        CHECK(effect.motions_started.empty());

        effect.blur_safe = true;
        f.frame(1);
        const auto resize = f.handle();
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 98};
        effect.blur_safe = false;
        f.module.reload();
        f.module.configure(f.policy);
        f.frame(1);
        CHECK(f.handle() == resize);
        CHECK(f.module.draw(resize, {.canvas = &canvas, .image = &image, .real_pass = true}));
    }

    void closing_override_replaces_mapped_state_and_restores_native_on_failure() {
        Fixture f;
        f.policy.close_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1);
        f.frame(1);
        const auto opened = f.handle();
        CHECK(opened.style == Style::KIND);
        f.module.close(1, f.port.now);
        const auto pending = f.module.pending_for(1);
        CHECK(pending && pending->style == Effect::KIND && !f.module.active(opened));
        if (!pending) return;
        const auto facts = f.fade(51);
        f.module.bind_fadeout(1, facts, {51});
        CHECK(f.module.suppresses(51));
        f.module.reload();
        CHECK(f.module.suppresses(51));
        auto &effect_log = f.resources.get<Effect>().log;
        effect_log.draw_succeeds = false;
        CHECK(effect_log.steps.empty());
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 51};
        canvas.pass_kind = PassKind::WorkspaceSource;
        CHECK(!f.module.draw(*pending, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(f.module.suppresses(51));
        CHECK(effect_log.real_draws == 0 && effect_log.steps.empty());
        canvas.pass_kind = PassKind::Live;
        effect_log.draw_succeeds = true;
        CHECK(f.module.draw(*pending, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(f.module.suppresses(51));
        effect_log.draw_succeeds = false;
        CHECK(!f.module.draw(*pending, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(!f.module.suppresses(51) && !f.module.active(*pending));
    }

    void completed_override_close_stays_suppressed_across_reload() {
        for (const bool finish_natively : {false, true}) {
            Fixture f;
            f.policy.close_style = Effect::KIND;
            f.module.configure(f.policy);
            f.window(1, 1, false);
            f.module.close(1, f.port.now);
            const auto handle = f.module.pending_for(1);
            CHECK(handle.has_value());
            if (!handle) continue;
            f.module.bind_fadeout(1, f.fade(54), {54});
            f.module.confirm_public_close_presented(*handle, f.port.now, true);
            f.resources.get<Effect>().log.running = false;
            f.frame(1);
            CHECK(!f.module.active(*handle) && f.module.suppresses(54));
            f.module.reload();
            f.module.configure(f.policy);
            CHECK(f.module.suppresses(54) && f.port.suppressed.contains(54));
            if (finish_natively) {
                f.module.native_finished(54);
            } else {
                f.policy.closing = false;
                f.module.configure(f.policy);
            }
            CHECK(!f.module.suppresses(54) && !f.port.suppressed.contains(54));
        }
    }

    void external_effect_declines_unsupported_blur_before_ownership() {
        Fixture f;
        f.policy.open_style = Effect::KIND;
        f.policy.close_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1);
        f.port.blurred_windows.insert(1);
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));
        CHECK(f.resources.get<Effect>().log.opens == 0);

        f.window(2);
        f.frame(1);
        const auto handle = f.handle(2);
        f.port.blurred_windows.insert(2);
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 55};
        CHECK(!f.module.draw(handle, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(canvas.draws.empty());

        f.module.close(1, f.port.now);
        auto fade = f.fade(55);
        fade.blurred = true;
        f.module.bind_fadeout(1, fade, {55});
        CHECK(!f.module.suppresses(55));
        CHECK(f.resources.get<Effect>().log.close_initializations == 0);
    }

    void shader_close_pre_blur_requires_the_pinned_capability() {
        Fixture f;
        f.policy.close_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1, 1, false);
        f.module.close(1, f.port.now);
        auto fade = f.fade(56);
        fade.pre_blur = true;
        f.port.fadeouts.at(56) = fade;
        f.module.bind_fadeout(1, fade, {56});
        CHECK(!f.module.suppresses(56));

        auto &effect = f.resources.get<Effect>().log;
        effect.blur_safe = true;
        f.window(2, 1, false);
        f.module.close(2, f.port.now);
        fade = f.fade(57);
        fade.pre_blur = true;
        f.port.fadeouts.at(57) = fade;
        effect.blur_safe = false;
        f.module.reload();
        f.module.configure(f.policy);
        f.module.bind_fadeout(2, fade, {57});
        CHECK(f.module.suppresses(57));

        // Shader support for native preBlur does not authorize textureBlur.
        f.port.fadeouts.at(57).blurred = true;
        f.frame(1);
        CHECK(!f.module.suppresses(57));
    }

    void external_effect_checks_privacy_at_entry_bind_and_draw() {
        Fixture f;
        f.policy.open_style = Effect::KIND;
        f.policy.close_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1);
        f.port.windows.at(1).no_screen_share = true;
        f.frame(1);
        CHECK(!f.port.transformers.contains(1));
        f.module.close(1, f.port.now);
        CHECK(!f.module.pending_for(1));

        f.window(2);
        f.frame(1);
        const auto opened = f.handle(2);
        f.port.windows.at(2).no_screen_share = true;
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 52};
        CHECK(!f.module.draw(opened, {.canvas = &canvas, .image = &image, .real_pass = true}));
        CHECK(f.resources.get<Effect>().log.real_draws == 0);
        f.port.windows.at(2).no_screen_share = false;
        f.port.windows.at(2).window = 3;  // reused identity must not expose the replacement's pixels
        CHECK(!f.module.draw(opened, {.canvas = &canvas, .image = &image, .real_pass = true}));
        f.port.windows.at(2).window = 2;
        f.module.close(2, f.port.now);
        CHECK(f.module.pending_for(2).has_value());
        f.port.windows.at(2).no_screen_share = true;
        f.module.bind_fadeout(2, f.fade(52), {52});
        CHECK(!f.module.suppresses(52));

        f.window(4);
        f.resources.get<Effect>().log.bind_succeeds = false;
        f.module.close(4, f.port.now);
        CHECK(f.module.pending_for(4).has_value());
        f.module.bind_fadeout(4, f.fade(53), {53});
        CHECK(!f.module.suppresses(53));
        CHECK(!f.module.pending_for(4));
    }

    void locked_close_replay_does_not_mutate_owned_shutdown() {
        Fixture f;
        f.policy.close_style = Effect::KIND;
        f.module.configure(f.policy);
        f.window(1, 1, false);
        f.module.close(1, f.port.now);
        const auto handle = f.module.pending_for(1);
        CHECK(handle.has_value());
        if (!handle) return;
        f.module.bind_fadeout(1, f.fade(56), {56});
        CHECK(f.module.suppresses(56));

        f.port.unlocked = false;
        FakeCanvas canvas;
        canvas.destination_live = true;
        FakeCanvas::Image image{.id = 56};
        const Draw<FakeCanvas> draw{.canvas = &canvas, .image = &image, .real_pass = true};
        CHECK(!f.module.draw(*handle, draw));
        CHECK(f.module.suppresses(56) && f.module.active(*handle));
        CHECK(canvas.draws.empty());
        canvas.destination_live = false;
        CHECK(!f.module.draw(*handle, draw));
        CHECK(!f.module.suppresses(56) && !f.module.active(*handle));
        CHECK(canvas.draws.empty());
    }

    void seated_interval_uses_native_surfaces_before_slot_end() {
        Fixture f;
        f.window(1);
        f.frame(1);
        const auto handle = f.handle();
        FakeCanvas canvas;
        FakeCanvas::Image image{7};
        const Draw<FakeCanvas> draw{.canvas = &canvas, .image = &image};
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
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        CHECK(fixture.log().opens == 1);
        CHECK(fixture.module.active(HANDLE));
        fixture.frame(1);
        CHECK(fixture.log().opens == 1);
        fixture.log().drive = Box{.x = 130, .y = 110, .w = 80, .h = 60};
        fixture.frame(1);
        CHECK(fixture.port.windows.at(1).body.x == 130);
        fixture.log().drive.reset();
        fixture.frame(1);
        CHECK(!fixture.log().steps.back().motion.has_value());
        fixture.log().running = false;
        fixture.frame(1);
        CHECK(!fixture.module.active(HANDLE));
        CHECK(fixture.log().ends == 1);
        CHECK(fixture.log().last_end == End::Completed);
        CHECK(fixture.port.windows.at(1).body.x == 100);
        CHECK(fixture.port.transformers.empty());
        fixture.port.windows.at(1).body.x += 10;
        fixture.port.windows.at(1).capture.x += 10;
        fixture.frame(1);
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
            fixture.window(1);
            fixture.frame(1);
            fixture.log().running = false;
            fixture.frame(1);
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
        Fixture fixture;
        fixture.policy.opening = false;
        fixture.module.configure(fixture.policy);
        fixture.window(1);
        fixture.frame(1);
        CHECK(fixture.port.transformers.empty());
        CHECK(fixture.log().opens == 0);
        fixture.port.windows.at(1).body.x += 10;
        fixture.port.windows.at(1).capture.x += 10;
        fixture.frame(1);
        CHECK(fixture.module.active(fixture.handle()));
        CHECK(fixture.log().opens == 0);
    }
    void monitor_frames_and_reattach() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        auto const *state = fixture.state(HANDLE);
        const auto COUNT = fixture.log().steps.size();
        fixture.frame(2);
        CHECK(fixture.log().steps.size() == COUNT);
        fixture.module.frame({.monitor = 1, .frame = fixture.frame_a, .now = fixture.port.now});
        CHECK(fixture.log().steps.size() == COUNT);
        fixture.port.transformers.erase(1);
        CHECK(fixture.module.needs_frame(1));
        FakeCanvas canvas;
        fixture.module.before_window(1, canvas);
        CHECK(fixture.handle() == HANDLE);
        CHECK(fixture.state(HANDLE) == state);
        CHECK(fixture.port.scheduled.size() >= 2);
        fixture.port.windows.at(1).monitor = 2;
        fixture.frame(1);
        CHECK(fixture.log().steps.size() == COUNT);
        fixture.frame(2);
        CHECK(fixture.log().steps.back().elapsed == 0);
        CHECK(fixture.state(HANDLE) == state);
    }
    void close_continuity_and_association() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        auto const *state = fixture.state(HANDLE);
        fixture.frame(1, 0.1);
        const double PROGRESS = state->progress;
        fixture.module.close(1, fixture.port.now);
        CHECK(fixture.module.pending_for(1) == HANDLE);
        CHECK(fixture.log().closed_state == state);
        CHECK(fixture.log().close_progress == PROGRESS);
        CHECK(fixture.log().closes == 1);
        fixture.module.close(1, fixture.port.now);
        CHECK(fixture.log().closes == 1);
        fixture.module.unmap(1);
        fixture.module.destroyed(1);
        CHECK(fixture.module.active(HANDLE));
        auto const FADE = fixture.fade(10);
        fixture.module.bind_fadeout(2, FADE, {.id = 10});
        CHECK(fixture.module.pending_for(1) == HANDLE);
        fixture.module.bind_fadeout(1, FADE, {.id = 10});
        CHECK(fixture.module.active(HANDLE));
        CHECK(fixture.module.suppresses(10));
        fixture.module.destroyed(1);
        fixture.frame(1);
        CHECK(fixture.module.active(HANDLE));
        CHECK(fixture.state(HANDLE) == state);
        fixture.log().running = false;
        fixture.frame(1);
        CHECK(!fixture.module.active(HANDLE));
        CHECK(fixture.module.suppresses(10));
        fixture.module.native_finished(10);
        CHECK(!fixture.module.suppresses(10));
    }
    void close_timeout_invalid_and_fresh() {
        Fixture fixture;
        fixture.window(1, 1, false);
        fixture.frame(1);
        fixture.module.close(1, fixture.port.now);
        const auto PENDING = fixture.module.pending_for(1);
        CHECK(PENDING.has_value());
        if (!PENDING) {
            return;
        }
        const auto HANDLE = *PENDING;
        CHECK(fixture.log().close_seed == 0);
        fixture.frame(1, 1.1);
        CHECK(!fixture.module.active(HANDLE));
        CHECK(fixture.log().last_end == End::Interrupted);
        fixture.window(2);
        fixture.frame(1);
        const auto HANDLE_TWO = fixture.handle(2);
        fixture.module.close(2, fixture.port.now);
        auto fade = fixture.fade(11);
        fade.blurred = true;
        fixture.module.bind_fadeout(2, fade, {.id = 11});
        CHECK(!fixture.module.active(HANDLE_TWO));
        CHECK(!fixture.module.suppresses(11));
    }
    void invalidation_and_restore() {
        for (int mode = 0; mode < 3; ++mode) {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            const auto HANDLE = fixture.handle();
            fixture.log().drive = Box{.x = 120, .y = 100, .w = 100, .h = 80};
            fixture.frame(1);
            fixture.port.windows.at(1).goal_body = {.x = 200, .y = 100, .w = 100, .h = 80};
            fixture.frame(1);
            if (mode == 0) {
                fixture.port.windows.at(1).fullscreen = true;
                fixture.frame(1);
            }
            if (mode == 1) {
                fixture.module.unmap(1);
            }
            if (mode == 2) {
                fixture.module.destroyed(1);
            }
            CHECK(!fixture.module.active(HANDLE));
            CHECK(fixture.log().ends == 1);
            CHECK(fixture.log().last_end == End::Interrupted);
            CHECK(fixture.port.windows.at(1).body.x == 200);
        }
    }

    void expired_windows_release_tracking_without_erasing_closes() {
        Fixture mapped;
        mapped.window(1);
        mapped.frame(1);
        const auto HANDLE = mapped.handle();
        mapped.log().drive = Box{.x = 120, .y = 100, .w = 90, .h = 70};
        mapped.frame(1);
        mapped.port.windows.erase(1);
        mapped.frame(2);
        CHECK(!mapped.module.active(HANDLE));
        CHECK(mapped.log().last_end == End::Interrupted);
        CHECK(mapped.port.visuals.empty());
        CHECK(std::ranges::find(mapped.port.forgotten, 1) != mapped.port.forgotten.end());

        Fixture pending;
        pending.window(1);
        pending.frame(1);
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
        dying.window(1);
        dying.frame(1);
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
        fixture.window(1);
        fixture.frame(1);
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
        driven.window(1);
        driven.frame(1);
        driven.log().drive = Box{.x = 120, .y = 100, .w = 90, .h = 70};
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
        ordinary.window(1);
        ordinary.frame(1);
        const auto HANDLE = ordinary.handle();
        ordinary.port.windows.at(1).hidden = true;
        ordinary.frame(1);
        CHECK(!ordinary.module.active(HANDLE));
        CHECK(ordinary.log().last_end == End::Interrupted);

        Fixture tail;
        tail.log().tail = true;
        tail.window(1);
        tail.frame(1);
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
        fixture.window(1);
        fixture.frame(1);
        CHECK(fixture.state(fixture.handle())->seed == 42);
        fixture.window(2, 1, false);
        fixture.module.close(2, fixture.port.now);
        CHECK(fixture.log().close_seed == 0);
    }
    void settings_style_reload_monitor() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        auto const *state = fixture.state(HANDLE);
        fixture.module.update_settings<Style>({3});
        fixture.frame(1, 0.1);
        CHECK(fixture.handle() == HANDLE);
        CHECK(fixture.state(HANDLE) == state);
        CHECK(std::abs(state->progress - 0.3) < 1e-8);
        fixture.module.close(1, fixture.port.now);
        auto const FADE = fixture.fade(20);
        fixture.module.bind_fadeout(1, FADE, {.id = 20});
        fixture.window(2);
        fixture.frame(1);
        auto const MAPPED = fixture.handle(2);
        fixture.policy.style = Other::KIND;
        fixture.module.configure(fixture.policy);
        CHECK(fixture.module.active(HANDLE));
        CHECK(!fixture.module.active(MAPPED));
        fixture.module.monitor_changed(2);
        CHECK(fixture.module.active(HANDLE));
        fixture.module.monitor_changed(1);
        CHECK(!fixture.module.active(HANDLE));
        fixture.window(3);
        fixture.frame(1);
        auto const NEWER = fixture.handle(3);
        fixture.module.reload();
        CHECK(!fixture.module.active(NEWER));
    }
    void stall_bounds() {
        {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            const auto HANDLE = fixture.handle();
            fixture.frame(1, 0.25);
            CHECK(!fixture.log().steps.back().stalled);
            fixture.frame(1, 0.251);
            CHECK(fixture.log().steps.back().stalled);
            fixture.frame(1, 0.1);
            fixture.frame(1, 0.251);
            CHECK(fixture.module.active(HANDLE));
            fixture.frame(1, 0.251);
            CHECK(!fixture.module.active(HANDLE));
            CHECK(fixture.log().last_end == End::Interrupted);
        }
        {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.frame(1, 2.0);
            CHECK(fixture.module.active(HANDLE));
        }
        {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.frame(1, 2.001);
            CHECK(!fixture.module.active(HANDLE));
        }
        {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.frame(1, 0.3);
            for (int i = 0; i < 10; ++i) {
                fixture.frame(1, 0.2);
            }
            fixture.frame(1, 0.3);
            fixture.frame(1, 0.3);
            CHECK(fixture.module.active(HANDLE));
        }
    }
    void effects_and_stale_draw() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        auto const OLD = fixture.handle();
        fixture.log().drive = Box{.x = 140, .y = 100, .w = 90, .h = 70};
        fixture.log().alpha = true;
        fixture.frame(1);
        CHECK(fixture.port.windows.at(1).body.x == 140);
        CHECK(!fixture.port.alpha_settled.empty());
        fixture.log().drive.reset();
        fixture.log().release = true;
        fixture.frame(1);
        CHECK(fixture.port.windows.at(1).body.x == 100);
        CHECK(fixture.module.active(OLD));
        fixture.log().release = false;
        fixture.log().snap = true;
        fixture.frame(1);
        CHECK(fixture.module.active(OLD));
        fixture.module.unmap(1);
        FakeCanvas canvas;
        FakeCanvas::Image const IMAGE{.id = 17};
        Draw<FakeCanvas> const DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {}};
        CHECK(!fixture.module.draw(OLD, DRAW));
        CHECK(canvas.draws.empty());
        fixture.window(2);
        fixture.frame(1);
        auto const REPLACEMENT = fixture.handle(2);
        CHECK(REPLACEMENT != OLD);
        CHECK(!fixture.module.draw(OLD, DRAW));
        CHECK(fixture.module.draw(REPLACEMENT, DRAW));
        CHECK(canvas.draws.size() == 1);
    }
    void close_modes_and_disable() {
        for (auto const MODE : {CloseStart::Native, CloseStart::Declined}) {
            Fixture fixture;
            fixture.log().close_result = MODE;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.module.close(1, fixture.port.now);
            if (MODE == CloseStart::Declined) {
                CHECK(!fixture.module.active(HANDLE));
                CHECK(!fixture.module.pending_for(1));
            } else {
                auto const FADE = fixture.fade(40);
                fixture.module.bind_fadeout(1, FADE, {.id = 40});
                CHECK(fixture.module.active(HANDLE));
                CHECK(!fixture.module.suppresses(40));
                fixture.module.native_finished(40);
                CHECK(!fixture.module.active(HANDLE));
                CHECK(fixture.log().last_end == End::Completed);
            }
        }
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        auto const FADE = fixture.fade(41);
        fixture.module.bind_fadeout(1, FADE, {.id = 41});
        fixture.window(2);
        fixture.frame(1);
        auto const MAPPED = fixture.handle(2);
        fixture.module.close(2, fixture.port.now);
        fixture.policy.closing = false;
        fixture.module.configure(fixture.policy);
        CHECK(!fixture.module.active(HANDLE));
        CHECK(!fixture.module.active(MAPPED));
        CHECK(!fixture.module.suppresses(41));
    }
    void close_mid_movement() {
        Fixture fixture;
        fixture.policy.opening = false;
        fixture.module.configure(fixture.policy);
        fixture.window(1);
        fixture.frame(1);
        fixture.port.windows.at(1).body.x += 15;
        fixture.port.windows.at(1).capture.x += 15;
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        auto const *state = fixture.state(HANDLE);
        fixture.frame(1, 0.1);
        auto const PROGRESS = state->progress;
        fixture.module.close(1, fixture.port.now);
        CHECK(fixture.module.pending_for(1) == HANDLE);
        CHECK(fixture.log().closed_state == state);
        CHECK(fixture.log().close_progress == PROGRESS);
        CHECK(fixture.log().closes == 1);
    }
    void tail_and_backdrop() {
        Fixture fixture;
        fixture.log().tail = true;
        fixture.log().backdrop = true;
        fixture.window(1);
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        FakeCanvas canvas;
        FakeCanvas::Image const FIRST{.id = 1, .size = {.x = 220, .y = 180}};
        FakeCanvas::Image const MATTE{.id = 2};
        fixture.module.before_window(1, canvas);
        CHECK(fixture.port.backdrop_captures.size() == 1);
        Draw<FakeCanvas> const DRAW{.canvas = &canvas,
            .image = &FIRST,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {.x = 7, .y = 8},
            .image_size = {},
            .real_pass = true,
            .frame = 1};
        CHECK(fixture.module.draw(HANDLE, DRAW));
        Draw<FakeCanvas> const SECOND{.canvas = &canvas,
            .image = &MATTE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true,
            .frame = 2};
        CHECK(fixture.module.draw(HANDLE, SECOND));
        auto retained = fixture.port.retained_mapped_image(HANDLE);
        CHECK(retained.has_value());
        if (retained) {
            CHECK(retained->image->id == 1);
            CHECK(retained->image_origin.x == 7);
        }
        fixture.port.windows.at(1).workspace_visible = false;
        fixture.frame(1);
        auto const COUNT = canvas.draws.size();
        fixture.module.after_windows(1, canvas);
        CHECK(canvas.draws.size() == COUNT);
        CHECK(fixture.port.queued_tails.size() == 1);
        if (fixture.port.queued_tails.empty()) {
            return;
        }
        const auto QUEUED = fixture.port.queued_tails.back();
        CHECK(QUEUED.image.id == 1);
        CHECK(QUEUED.draw.image_size.x == 220);
        CHECK(QUEUED.draw.output.x == 95 && QUEUED.draw.output.w == 100);
        Draw<FakeCanvas> const DEFERRED{.canvas = &canvas,
            .image = &QUEUED.image,
            .backdrop = nullptr,
            .source = QUEUED.draw.source,
            .output = QUEUED.draw.output,
            .canvas_box = QUEUED.draw.canvas_box,
            .image_origin = QUEUED.draw.image_origin,
            .image_size = QUEUED.draw.image_size,
            .scale = QUEUED.draw.scale,
            .alpha = QUEUED.draw.alpha,
            .real_pass = true,
            .monitor = QUEUED.draw.monitor,
            .window = 0,
            .frame = QUEUED.draw.frame};
        CHECK(fixture.module.draw_tail(QUEUED.handle, DEFERRED, QUEUED.draw.no_screen_share));
        CHECK(canvas.draws.back() == 1);
        fixture.port.windows.at(1).workspace_visible = true;
        fixture.log().tail = false;
        fixture.frame(1);
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        fixture.log().tail = true;
        fixture.frame(1);
        CHECK(fixture.module.draw(HANDLE, SECOND));
        retained = fixture.port.retained_mapped_image(HANDLE);
        CHECK(retained && retained->image->id == 2);
        fixture.module.end_all();
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        CHECK(!fixture.module.draw_tail(QUEUED.handle, DEFERRED, QUEUED.draw.no_screen_share));
    }
    void retained_tail_final_draw_privacy() {
        // All images are deterministic synthetic IDs. Queue locally, then change policy
        // without a model tick: the final pass must not trust the cached observation.
        for (int pass = 0; pass < 6; ++pass) {
            for (int change = 0; change < 10; ++change) {
                Fixture fixture;
                fixture.policy.during_workspace_switch = true;
                fixture.module.configure(fixture.policy);
                fixture.log().tail = true;
                fixture.window(1);
                fixture.frame(1);
                const auto HANDLE = fixture.handle();
                FakeCanvas canvas;
                const FakeCanvas::Image IMAGE{.id = 73};
                const Draw<FakeCanvas> INITIAL{.canvas = &canvas,
                    .image = &IMAGE,
                    .source = {},
                    .output = {},
                    .canvas_box = {},
                    .image_origin = {},
                    .image_size = {},
                    .real_pass = true};
                CHECK(fixture.module.draw(HANDLE, INITIAL));
                fixture.port.windows.at(1).workspace_visible = false;
                fixture.frame(1);
                fixture.module.after_windows(1, canvas);
                CHECK(fixture.port.queued_tails.size() == 1);
                if (fixture.port.queued_tails.empty()) {
                    continue;
                }
                const auto QUEUED = fixture.port.queued_tails.back();
                if (pass == 1) {
                    canvas.pass_kind = PassKind::WorkspaceSource;
                }
                if (pass == 2) {
                    canvas.pass_kind = PassKind::Inert;
                }
                if (pass == 3) {
                    canvas.rendering_capture = true;
                }
                if (pass == 4) {
                    canvas.rendering_snapshot = true;
                }
                if (pass == 5) {
                    canvas.destination_live = true;
                }
                if (change == 1) {
                    fixture.port.windows.at(1).no_screen_share = true;
                }
                if (change == 2) {
                    fixture.port.windows.erase(1);
                }
                if (change == 3) {
                    fixture.port.unobservable.insert(1);
                }
                if (change == 4) {
                    fixture.port.windows.at(1).window = 999;
                }
                if (change == 5) {
                    fixture.port.windows.at(1).mapped = false;
                }
                if (change == 6) {
                    fixture.port.unlocked = false;
                }
                if (change == 7) {
                    fixture.port.session_available = false;
                }
                if (change == 8) {
                    fixture.policy.enabled = false;
                    fixture.module.configure(fixture.policy);
                }
                if (change == 9) {
                    fixture.policy.inert = true;
                    fixture.module.configure(fixture.policy);
                }
                const Draw<FakeCanvas> DEFERRED{.canvas = &canvas,
                    .image = &QUEUED.image,
                    .source = QUEUED.draw.source,
                    .output = QUEUED.draw.output,
                    .canvas_box = QUEUED.draw.canvas_box,
                    .image_origin = QUEUED.draw.image_origin,
                    .image_size = QUEUED.draw.image_size,
                    .real_pass = true};
                const auto COUNT = canvas.draws.size();
                const bool ALLOWED = change == 0 || (change == 1 && pass == 0);
                CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share) ==
                      ALLOWED);
                CHECK(canvas.draws.size() == COUNT + (ALLOWED ? 1U : 0U));
                // Queue-time denial is independent of final-pass denial, including local
                // source disappearance. A denied capture never destroys the local tail.
                fixture.port.queued_tails.clear();
                fixture.module.after_windows(1, canvas);
                CHECK(fixture.port.queued_tails.size() == (ALLOWED ? 1U : 0U));
                if (change == 1 && pass != 0) {
                    canvas.pass_kind = PassKind::Live;
                    canvas.rendering_capture = false;
                    canvas.rendering_snapshot = false;
                    canvas.destination_live = false;
                    CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
                }
            }
        }
    }
    void retained_tail_pixel_provenance() {
        Fixture fixture;
        fixture.log().tail = true;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        fixture.port.windows.at(1).no_screen_share = true;
        FakeCanvas canvas;
        const FakeCanvas::Image PRIVATE{.id = 81}, PUBLIC{.id = 82};
        const Draw<FakeCanvas> INITIAL{.canvas = &canvas,
            .image = &PRIVATE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
        CHECK(fixture.module.draw(HANDLE, INITIAL));
        fixture.port.windows.at(1).workspace_visible = false;
        fixture.frame(1);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.port.queued_tails.size() == 1);
        if (fixture.port.queued_tails.empty()) {
            return;
        }
        const auto QUEUED = fixture.port.queued_tails.back();
        CHECK(QUEUED.draw.no_screen_share);
        fixture.port.windows.at(1).no_screen_share = false;
        const Draw<FakeCanvas> DEFERRED{.canvas = &canvas,
            .image = &QUEUED.image,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
        CHECK(fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
        canvas.pass_kind = PassKind::WorkspaceSource;
        CHECK(!fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
        fixture.port.queued_tails.clear();
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.port.queued_tails.empty());
        // Replacing the retained cache with public pixels must not declassify a
        // previously queued private image from the same live animation handle.
        canvas.pass_kind = PassKind::Live;
        fixture.port.windows.at(1).workspace_visible = true;
        fixture.log().tail = false;
        fixture.frame(1);
        fixture.log().tail = true;
        fixture.frame(1);
        auto fresh = INITIAL;
        fresh.image = &PUBLIC;
        CHECK(fixture.module.draw(HANDLE, fresh));
        canvas.pass_kind = PassKind::WorkspaceSource;
        CHECK(!fixture.module.draw_tail(HANDLE, DEFERRED, QUEUED.draw.no_screen_share));
        CHECK(fixture.module.draw_tail(HANDLE, fresh, false));
        fixture.port.windows.at(1).workspace_visible = false;
        fixture.frame(1);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.port.queued_tails.size() == 1);
        CHECK(!fixture.port.queued_tails.empty() &&
              !fixture.port.queued_tails.back().draw.no_screen_share);
    }
    void retained_tail_retention_denial() {
        Fixture fixture;
        fixture.log().tail = true;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        FakeCanvas canvas;
        const FakeCanvas::Image IMAGE{.id = 83};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
        fixture.port.unlocked = false;
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        fixture.port.unlocked = true;
        fixture.port.session_available = false;
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        fixture.port.session_available = true;
        canvas.rendering_capture = true;
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        canvas.rendering_capture = false;
        canvas.rendering_snapshot = true;
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
    void retained_tail_desktop_pixels_are_not_replayed() {
        Fixture fixture;
        fixture.log().tail = true;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        FakeCanvas canvas;
        const FakeCanvas::Image IMAGE{.id = 84};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true,
            .includes_desktop_pixels = true};
        CHECK(!fixture.port.windows.at(1).no_screen_share);
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(fixture.port.retained_mapped_image(HANDLE).has_value());
        fixture.port.windows.at(1).workspace_visible = false;
        fixture.frame(1);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.port.queued_tails.size() == 1);

        canvas.pass_kind = PassKind::Live;
        CHECK(fixture.module.draw_tail(HANDLE, DRAW, false));
        canvas.pass_kind = PassKind::WorkspaceSource;
        CHECK(!fixture.module.draw_tail(HANDLE, DRAW, false));
        canvas.pass_kind = PassKind::Live;
        canvas.rendering_capture = true;
        CHECK(!fixture.module.draw_tail(HANDLE, DRAW, false));
        canvas.rendering_capture = false;
        canvas.rendering_snapshot = true;
        CHECK(!fixture.module.draw_tail(HANDLE, DRAW, false));
    }
    void retained_tail_rechecks_final_submission() {
        for (int change = 0; change < 9; ++change) {
            Fixture fixture;
            fixture.policy.during_workspace_switch = true;
            fixture.module.configure(fixture.policy);
            fixture.log().tail = true;
            fixture.window(1);
            fixture.frame(1);
            const auto HANDLE = fixture.handle();
            FakeCanvas canvas;
            const FakeCanvas::Image IMAGE{.id = 90};
            const Draw<FakeCanvas> DRAW{.canvas = &canvas,
                .image = &IMAGE,
                .source = {},
                .output = {},
                .canvas_box = {},
                .image_origin = {},
                .image_size = {},
                .real_pass = true};
            CHECK(fixture.module.draw(HANDLE, DRAW));
            fixture.port.windows.at(1).workspace_visible = false;
            fixture.frame(1);
            // DestinationLive remains Live for state advancement, but its target
            // framebuffer is retained and must not receive protected window pixels.
            canvas.destination_live = true;
            fixture.module.after_windows(1, canvas);
            CHECK(fixture.port.queued_tails.size() == 1);
            if (fixture.port.queued_tails.empty()) {
                continue;
            }
            const auto QUEUED = fixture.port.queued_tails.back();
            fixture.log().during_draw = [&] {
                if (change == 1) {
                    fixture.port.windows.at(1).no_screen_share = true;
                }
                if (change == 2) {
                    fixture.port.windows.erase(1);
                }
                if (change == 3) {
                    fixture.port.unobservable.insert(1);
                }
                if (change == 4) {
                    fixture.port.windows.at(1).window = 999;
                }
                if (change == 5) {
                    fixture.port.unlocked = false;
                }
                if (change == 6) {
                    fixture.policy.inert = true;
                    fixture.module.configure(fixture.policy);
                }
                if (change == 7) {
                    fixture.port.visual_cache(HANDLE).image = FakeCanvas::Image{.id = 91};
                }
                if (change == 8) {
                    fixture.port.windows.at(1).mapped = false;
                }
            };
            const auto REAL_DRAWS = fixture.log().real_draws;
            int submissions = 0;
            CHECK(fixture.module.display_tail(HANDLE, DRAW, QUEUED.draw.no_screen_share, [&] {
                ++submissions;
            }) == (change == 0));
            CHECK(submissions == (change == 0 ? 1 : 0));
            CHECK(fixture.log().real_draws == REAL_DRAWS + 1);
        }
    }
    void capacity_and_fadeout_validation() {
        Fixture fixture;
        for (WindowId window = 1; window <= 65; ++window) {
            fixture.window(window);
        }
        fixture.frame(1);
        auto const FIRST = fixture.handle(1);
        auto const *first_state = fixture.state(FIRST);
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
        CHECK(!fixture.module.active(OVERFLOW));
        CHECK(!fixture.module.suppresses(117));
        auto const ORPHAN = fixture.fade(300);
        fixture.module.bind_fadeout(999, ORPHAN, {.id = 1});
        CHECK(!fixture.module.suppresses(300));
    }
    void pending_invalidation() {
        for (int mode = 0; mode < 6; ++mode) {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.module.close(1, fixture.port.now);
            auto fadeout = fixture.fade(200);
            if (mode == 0) {
                fixture.frame(1, 1.1);
            }
            if (mode == 1) {
                fixture.port.monitors.erase(1);
                fixture.module.monitor_changed(1);
            }
            if (mode == 2) {
                fixture.port.windows.at(1).scale = 2;
                fixture.module.monitor_changed(1);
            }
            if (mode == 3) {
                fadeout.monitor = 2;
            }
            if (mode == 4) {
                fadeout.image_available = false;
            }
            if (mode == 5) {
                fadeout.now = fixture.port.advance(1.1);
            }
            fixture.module.bind_fadeout(1, fadeout, {.id = 1});
            CHECK(!fixture.module.active(HANDLE));
            CHECK(!fixture.module.suppresses(200));
            CHECK(fixture.port.visuals.empty());
        }
    }
    void screen_order_and_shutdown() {
        Fixture fixture;
        fixture.log().events.clear();
        fixture.log().screen_active = true;
        fixture.window(1);
        fixture.frame(1);
        CHECK(fixture.log().events.size() >= 2);
        CHECK(fixture.log().events[0] == 1);
        CHECK(fixture.log().events[1] == 2);
        FakeCanvas canvas;
        fixture.module.before_window(1, canvas);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.log().events[2] == 3);
        CHECK(fixture.log().events[3] == 4);
        auto const MAPPED = fixture.handle();
        fixture.window(2);
        fixture.frame(1);
        auto const DYING = fixture.handle(2);
        fixture.module.close(2, fixture.port.now);
        fixture.module.bind_fadeout(2, fixture.fade(400), {.id = 2});
        fixture.log().events.clear();
        fixture.port.lifecycle_events = &fixture.log().events;
        fixture.module.detach_transformers();
        CHECK(fixture.port.transformers.empty());
        CHECK(fixture.module.active(MAPPED));
        CHECK(fixture.module.active(DYING));
        fixture.module.end_all();
        CHECK(!fixture.module.active(MAPPED));
        CHECK(!fixture.module.active(DYING));
        const auto FIRST_END =
            std::find(fixture.log().events.begin(), fixture.log().events.end(), 7);
        const auto FIRST_RELEASE =
            std::find(fixture.log().events.begin(), fixture.log().events.end(), 8);
        CHECK(!fixture.log().events.empty());
        CHECK(fixture.log().events.front() == 6);
        CHECK(FIRST_END != fixture.log().events.end());
        CHECK(FIRST_RELEASE != fixture.log().events.end());
        CHECK(FIRST_END < FIRST_RELEASE);
        CHECK(fixture.port.visuals.empty());
        CHECK(fixture.port.suppressed.empty());
        CHECK(!fixture.module.needs_frame(1));
        CHECK(fixture.log().events.back() == 5);
        fixture.module.end_all();
        CHECK(fixture.port.visuals.empty());
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
        retained.window(1);
        retained.frame(1);
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

    void bubble_disable_repaints_each_active_output_before_global_clear() {
        BubbleScreenFixture f;
        CHECK(f.module.needs_frame(1) && f.module.needs_frame(2));
        const auto before_clear = [&] {
            CHECK(f.module.needs_frame(1) && f.module.needs_frame(2));
        };
        f.port.on_damage = before_clear;
        f.port.on_schedule = before_clear;
        f.port.on_scanout = [&] { CHECK(!f.module.needs_frame(1) && !f.module.needs_frame(2)); };

        f.policy.enabled = false;
        f.module.configure(f.policy);
        CHECK(!f.module.needs_frame(1) && !f.module.needs_frame(2));
        CHECK(f.port.damage_log.size() == 2);
        CHECK(f.port.scheduled.size() == 2);
        CHECK(f.port.scanout.size() == 2);
        for (const MonitorId monitor : {1, 2}) {
            const auto &box = f.port.monitors.at(monitor).screen;
            CHECK(std::ranges::count_if(f.port.damage_log, [&](const Box &damage) {
                return damage.x == box.x && damage.y == box.y && damage.w == box.w &&
                       damage.h == box.h;
            }) == 1);
            CHECK(std::ranges::count(f.port.scheduled, monitor) == 1);
            CHECK(std::ranges::count(f.port.scanout, monitor) == 1);
        }
    }

    void nested_configure_supersedes_bubble_disable() {
        BubbleScreenFixture f;
        bool superseded = false;
        f.port.on_damage = [&] {
            CHECK(!superseded);
            if (superseded) return;
            superseded = true;
            CHECK(f.module.needs_frame(1) && f.module.needs_frame(2));
            f.policy.during_workspace_switch = true;
            f.module.configure(f.policy);
            f.resources.get<BubbleScreenStyle>().active_outputs = {3};
        };

        auto disabled = f.policy;
        disabled.enabled = false;
        f.module.configure(disabled);
        CHECK(superseded);
        CHECK(f.module.needs_frame(3));
        CHECK(f.port.scheduled.empty() && f.port.scanout.empty());
        CHECK(f.resources.get<Other>().log.events.size() == 1);
        FakeCanvas canvas;
        canvas.destination_live = true;
        CHECK(f.module.allows_draw(canvas));
    }

    void nested_configure_during_scanout_reconciles_all_cleared_outputs() {
        BubbleScreenFixture f;
        bool superseded = false;
        f.port.on_scanout = [&] {
            if (superseded) return;
            superseded = true;
            f.policy.during_workspace_switch = true;
            f.module.configure(f.policy);
            f.resources.get<BubbleScreenStyle>().active_outputs = {3};
        };

        auto disabled = f.policy;
        disabled.enabled = false;
        f.module.configure(disabled);
        CHECK(superseded);
        CHECK(f.module.needs_frame(3));
        CHECK(f.port.scanout.size() == 2);
        CHECK(std::ranges::count(f.port.scanout, 1) == 1);
        CHECK(std::ranges::count(f.port.scanout, 2) == 1);
        CHECK(f.resources.get<Other>().log.events.size() == 1);
        FakeCanvas canvas;
        canvas.destination_live = true;
        CHECK(f.module.allows_draw(canvas));
    }

    void bubble_reload_then_disable_rechecks_every_cleared_output() {
        BubbleScreenFixture f;
        // Neither output is assumed to be in the host's scanout-inhibited list.
        f.port.on_damage = [&] { CHECK(f.module.needs_frame(1) && f.module.needs_frame(2)); };
        f.port.on_schedule = f.port.on_damage;
        f.port.on_scanout = [&] { CHECK(!f.module.needs_frame(1) && !f.module.needs_frame(2)); };
        f.module.reload();
        f.policy.enabled = false;
        f.module.configure(f.policy);
        CHECK(!f.module.needs_frame(1) && !f.module.needs_frame(2));
        CHECK(f.port.damage_log.size() == 2);
        CHECK(f.port.scheduled.size() == 2);
        CHECK(f.port.scanout.size() == 2);
        for (const MonitorId monitor : {1, 2}) {
            CHECK(std::ranges::count(f.port.scheduled, monitor) == 1);
            CHECK(std::ranges::count(f.port.scanout, monitor) == 1);
        }
    }

    void nested_configure_during_style_switch_finishes_mapped_cleanup() {
        BubbleScreenFixture f;
        f.resources.get<BubbleScreenStyle>().active_outputs.clear();
        for (const WindowId id : {1, 2}) {
            Observation observation{.window = id,
                .monitor = 1,
                .workspace = 1,
                .body = {100, 100, 100, 80},
                .capture = {95, 95, 110, 90},
                .goal_body = {100, 100, 100, 80},
                .screen = {0, 0, 800, 600},
                .work_area = {0, 0, 800, 600},
                .mapped = true,
                .animating_in = true};
            f.port.windows.emplace(id, observation);
            f.module.track(id);
        }
        f.module.frame({.monitor = 1, .frame = 2, .now = f.port.advance(.016)});
        CHECK(f.port.transformers.size() == 2);
        auto switched = f.policy;
        switched.style = Other::KIND;
        bool nested = false;
        f.port.on_scanout = [&] {
            if (!nested) {
                nested = true;
                f.module.configure(switched);
            }
        };
        f.module.configure(switched);
        CHECK(nested);
        CHECK(f.port.transformers.empty());
        CHECK(f.resources.get<BubbleScreenStyle>().log.ends == 2);
        f.port.on_scanout = {};
        f.module.end_all();
    }

    void nested_configure_removing_dying_entry_preserves_mapped_cleanup() {
        BubbleScreenFixture f;
        f.resources.get<BubbleScreenStyle>().active_outputs.clear();
        f.policy.closing = true;
        f.policy.close_available = true;
        f.module.configure(f.policy);
        const auto add_window = [&](WindowId id) {
            Observation observation{.window = id,
                .monitor = 1,
                .workspace = 1,
                .body = {100, 100, 100, 80},
                .capture = {95, 95, 110, 90},
                .goal_body = {100, 100, 100, 80},
                .screen = {0, 0, 800, 600},
                .work_area = {0, 0, 800, 600},
                .mapped = true,
                .animating_in = true};
            f.port.windows.emplace(id, observation);
            f.module.track(id);
        };
        add_window(1);
        f.module.frame({.monitor = 1, .frame = 2, .now = f.port.advance(.016)});
        const auto dying = f.port.transformers.at(1);
        f.module.close(1, f.port.now);
        CHECK(f.module.pending_for(1).has_value());
        add_window(2);
        add_window(3);
        f.module.frame({.monitor = 1, .frame = 3, .now = f.port.advance(.016)});
        const auto first = f.port.transformers.at(2);
        const auto second = f.port.transformers.at(3);
        auto switched = f.policy;
        switched.style = Other::KIND;
        switched.closing = false;
        bool nested = false;
        f.port.on_scanout = [&] {
            if (!nested) {
                nested = true;
                f.module.configure(switched);
            }
        };
        f.module.configure(switched);
        CHECK(nested);
        CHECK(!f.module.active(dying));
        CHECK(!f.module.active(first));
        CHECK(!f.module.active(second));
        CHECK(f.port.transformers.empty());
        CHECK(f.resources.get<BubbleScreenStyle>().log.ends == 3);
        f.port.on_scanout = {};
        f.module.end_all();
    }

    void screen_survives_window_and_allocation_failure() {
        Fixture fixture;
        fixture.log().screen_active = true;
        fixture.window(1);
        fixture.frame(1);
        fixture.module.unmap(1);
        CHECK(fixture.module.needs_frame(1));
        CHECK(!fixture.port.scanout.empty());
        Fixture failing;
        failing.port.fail_acquire = true;
        failing.window(1);
        bool threw = false;
        try {
            failing.frame(1);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        CHECK(threw);
        CHECK(failing.resources.get<Style>().animations.size() == 0);
        CHECK(failing.port.transformers.empty());
        CHECK(failing.port.visuals.empty());
        CHECK(failing.log().ends == 1);
        CHECK(failing.log().last_end == End::Interrupted);
    }
    void pass_kind_retention_and_backdrop() {
        Fixture fixture;
        fixture.log().tail = true;
        fixture.log().backdrop = true;
        fixture.window(1);
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        FakeCanvas canvas;
        FakeCanvas::Image const IMAGE{.id = 8};
        Draw<FakeCanvas> const DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
        canvas.pass_kind = PassKind::Inert;
        fixture.module.before_window(1, canvas);
        CHECK(fixture.port.backdrop_captures.empty());
        fixture.module.draw(HANDLE, DRAW);
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        CHECK(fixture.log().draw_backdrop == 0);
        CHECK(fixture.log().draw_monitor == 1 && fixture.log().draw_window == 1);
        CHECK(fixture.log().draw_frame == fixture.frame_a);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.log().real_draws == 0);
        CHECK(fixture.log().real_screen_draws == 0);
        canvas.pass_kind = PassKind::WorkspaceSource;
        fixture.module.before_window(1, canvas);
        CHECK(fixture.port.backdrop_captures.size() == 1);
        fixture.module.draw(HANDLE, DRAW);
        CHECK(!fixture.port.retained_mapped_image(HANDLE));
        CHECK(fixture.log().draw_backdrop == 2);
        fixture.module.after_windows(1, canvas);
        CHECK(fixture.log().real_draws == 0);
        CHECK(fixture.log().real_screen_draws == 0);
        CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 0);
        CHECK(fixture.port.visual_cache(HANDLE).scratch_backdrops == 1);
        canvas.pass_kind = PassKind::Live;
        fixture.module.draw(HANDLE, DRAW);
        CHECK(fixture.port.retained_mapped_image(HANDLE).has_value());
        fixture.module.before_window(1, canvas);
        fixture.module.after_windows(1, canvas);
        fixture.module.draw(HANDLE, DRAW);
        CHECK(fixture.log().draw_backdrop == 1);
        CHECK(fixture.log().real_draws == 2);
        CHECK(fixture.log().real_screen_draws == 2);
        CHECK(fixture.port.visual_cache(HANDLE).live_backdrops == 1);
    }
    void out_of_order_and_duplicate_fadeouts() {
        Fixture fixture;
        fixture.window(1);
        fixture.window(2);
        fixture.frame(1);
        auto const FIRST = fixture.handle(1);
        auto const SECOND = fixture.handle(2);
        fixture.module.close(1, fixture.port.now);
        fixture.module.close(2, fixture.port.now);
        auto const FADEOUT = fixture.fade(500);
        fixture.module.bind_fadeout(2, FADEOUT, {.id = 2});
        CHECK(fixture.module.active(SECOND));
        CHECK(fixture.module.pending_for(1) == FIRST);
        fixture.module.bind_fadeout(1, FADEOUT, {.id = 1});
        CHECK(!fixture.module.active(FIRST));
        CHECK(fixture.module.active(SECOND));
        CHECK(fixture.module.dying_views().size() == 1);
        CHECK(fixture.module.suppresses(500));
    }
    void close_placement_and_invalid_fadeouts() {
        for (int mode = 0; mode < 4; ++mode) {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            auto const HANDLE = fixture.handle();
            fixture.module.close(1, fixture.port.now);
            auto fadeout = fixture.fade(600);
            if (mode == 0) {
                fadeout.scale = 2;
            }
            if (mode == 1) {
                fadeout.alpha = 0;
            }
            if (mode == 2) {
                fadeout.alive = false;
            }
            if (mode == 3) {
                fadeout.native_box.w = 0;
            }
            fixture.module.bind_fadeout(1, fadeout, {.id = 1});
            CHECK(!fixture.module.active(HANDLE));
            CHECK(!fixture.module.suppresses(600));
        }
        Fixture fixture;
        fixture.window(1);
        auto &window = fixture.port.windows.at(1);
        window.workspace_offset = {.x = 10, .y = 20};
        window.floating_offset = {.x = 3, .y = 4};
        window.screen = {.x = 50, .y = 30, .w = 800, .h = 600};
        window.scale = 2;
        fixture.frame(1);
        fixture.module.close(1, fixture.port.now);
        auto fadeout = fixture.fade(601);
        fadeout.scale = 2;
        fixture.module.bind_fadeout(1, fadeout, {.id = 1});
        auto const VIEWS = fixture.module.dying_views();
        CHECK(VIEWS.size() == 1);
        if (!VIEWS.empty()) {
            CHECK(VIEWS[0].source_device.x == 116);
            CHECK(VIEWS[0].source_device.y == 178);
            CHECK(VIEWS[0].source_device.w == 220);
            CHECK(VIEWS[0].source_device.h == 180);
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

    void public_only_close_leaves_restricted_fadeouts_native() {
        enum class Stop { PrivateAtClose, PrivateAtBind, PrivateImage, LockedAtBind };
        for (const auto stop : {Stop::PrivateAtClose, Stop::PrivateAtBind, Stop::PrivateImage, Stop::LockedAtBind}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            f.window(1);
            f.frame(1);
            const auto handle = f.handle();
            f.port.windows.at(1).no_screen_share = stop == Stop::PrivateAtClose;
            f.module.close(1, f.port.now);
            CHECK(f.module.pending_for(1).has_value() == (stop != Stop::PrivateAtClose));
            f.port.windows.at(1).no_screen_share = stop == Stop::PrivateAtBind;
            f.port.unlocked = stop != Stop::LockedAtBind;
            auto fade = f.fade(814);
            fade.no_screen_share = stop == Stop::PrivateImage;
            f.module.bind_fadeout(1, fade, {.id = 21});
            CHECK(!f.module.active(handle));
            CHECK(!f.module.dying_view_for(814));
            CHECK(f.port.suppress_requests == 0);
            CHECK(!f.port.find_visuals(handle));
        }
        for (const bool lock : {false, true}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            f.window(1);
            f.frame(1);
            const auto handle = f.handle();
            f.module.close(1, f.port.now);
            auto fade = f.fade(814);
            f.module.bind_fadeout(1, fade, {.id = 21});
            CHECK(f.module.dying_view_for(814).has_value());
            f.port.unlocked = !lock;
            fade.no_screen_share = !lock;
            f.port.fadeouts[814] = fade;
            f.frame(1);
            CHECK(!f.module.active(handle));
            CHECK(!f.module.suppresses(814));
            CHECK(!f.port.find_visuals(handle));
        }
    }

    void public_close_publishes_first_frame_at_bind() {
        for (const bool mapped_effect : {false, true}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            auto &log = f.resources.get<Other>().log;
            f.window(1, 1, mapped_effect);
            auto &window = f.port.windows.at(1);
            window.screen = {45, 34, 800, 600};
            window.workspace_offset = {10, 20};
            window.floating_offset = {-3, 4};
            window.scale = 1.25;
            f.frame(1);
            f.module.close(1, f.port.now);
            const auto HANDLE = f.module.pending_for(1);
            CHECK(HANDLE);
            if (!HANDLE) { continue; }
            const auto PENDING = f.module.pending_public_close(*HANDLE);
            CHECK(PENDING && PENDING->place.capture.x == 95 && PENDING->place.screen.x == 45 &&
                PENDING->source_device.x == 71.25 && PENDING->source_device.y == 106.25 && PENDING->scale == 1.25);
            CHECK(log.close_initializations == 0);
            f.frame(1, 0.32); // A pending snapshot can take longer than the style's stall threshold.
            auto fade = f.fade(815);
            fade.scale = window.scale;
            f.port.fadeouts[815] = fade;
            f.module.bind_fadeout(1, fade, {.id = 15});
            CHECK(f.module.suppresses(815));
            CHECK(!f.module.pending_public_close(*HANDLE));
            CHECK(log.close_initializations == 1);
            CHECK(log.initial_close_step && log.initial_close_step->dying && log.initial_close_step->elapsed == 0 &&
                !log.initial_close_step->stalled && log.initial_close_step->body.x == 100 &&
                log.initial_close_step->capture.y == 95 && log.initial_close_step->screen.x == 38 &&
                log.initial_close_step->screen.y == 10 && log.initial_close_step->screen.w == 800);
            const auto FIRST = f.module.dying_view_for(815);
            CHECK(FIRST && FIRST->public_close_only && FIRST->bounds_device.x == 73.75 &&
                FIRST->bounds_device.y == 110 && FIRST->bounds_device.w == 25 && FIRST->bounds_device.h == 22.5);
            FakeCanvas canvas;
            const FakeCanvas::Image IMAGE{.id = 15};
            const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
                .canvas_box = {}, .image_origin = {}, .image_size = IMAGE.size, .real_pass = true};
            const auto STEP_COUNT = log.steps.size();
            f.frame(1, 0.09); // Stock render.pre runs before the first physical composite.
            CHECK(log.steps.size() == STEP_COUNT);
            canvas.rendering_capture = true;
            CHECK(f.module.draw(*HANDLE, DRAW));
            canvas.rendering_capture = false;
            auto stale = *HANDLE;
            ++stale.generation;
            f.module.confirm_public_close_presented(stale, f.port.now, true);
            f.frame(2, 0.09);
            f.frame(1, 0.09);
            CHECK(log.steps.size() == STEP_COUNT);
            const auto STILL_FIRST = f.module.dying_view_for(815);
            CHECK(STILL_FIRST && STILL_FIRST->bounds_device.w == 25);
            canvas.draws.clear();
            CHECK(f.module.draw(*HANDLE, DRAW) && canvas.draws == std::vector<int>{15});
            f.module.confirm_public_close_presented(*HANDLE, f.port.now, true);
            CHECK(!f.port.damage_log.empty());
            if (!f.port.damage_log.empty()) {
                const auto &damage = f.port.damage_log.back();
                CHECK(damage.x == 104 && damage.y == 122 && damage.w == 20 && damage.h == 18);
            }
            f.module.bind_fadeout(1, fade, {.id = 15});
            CHECK(log.close_initializations == 1);
            f.frame(1);
            CHECK(!log.steps.empty());
            if (!log.steps.empty()) {
                const auto &step = log.steps.back();
                CHECK(step.dying && !step.stalled && std::abs(step.elapsed - 0.016) < 1e-8 &&
                    step.screen.x == 38 && step.screen.y == 10);
            }
        }
    }

    void public_close_blank_completion_is_not_native_fallback() {
        for (const bool interrupted : {false, true}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            auto &log = f.resources.get<Other>().log;
            log.initial_close = {.interrupted = interrupted};
            f.window(1);
            f.frame(1);
            const auto HANDLE = f.handle();
            f.module.close(1, f.port.now);
            const auto FADE = f.fade(816);
            f.module.bind_fadeout(1, FADE, {.id = 16});
            CHECK(log.close_initializations == 1);
            CHECK(log.last_end == (interrupted ? End::Interrupted : End::Completed));
            CHECK(!f.module.active(HANDLE) && !f.module.dying_view_for(816));
            CHECK(f.port.find_visuals(HANDLE) == nullptr);
            CHECK(f.port.suppress_requests == (interrupted ? 0 : 1));
            f.module.interrupt_public_close(HANDLE); // A stale pass must not clear a valid tombstone.
            f.module.bind_fadeout(1, FADE, {.id = 16});
            f.frame(1);
            CHECK(log.close_initializations == 1 && log.ends == 1);
            CHECK(f.module.suppresses(816) == !interrupted);
            CHECK(f.port.suppressed.contains(816) == !interrupted);
            f.module.native_finished(816);
            CHECK(!f.module.suppresses(816) && !f.port.suppressed.contains(816));
        }
    }

    void public_close_rejects_unproven_sources_before_initialization() {
        enum class Source { BadPlacement, IncorrectWorkspace, RefusedRetention, ProducerMismatch, TooLate };
        for (const auto source : {Source::BadPlacement, Source::IncorrectWorkspace, Source::RefusedRetention,
                 Source::ProducerMismatch, Source::TooLate}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            auto &log = f.resources.get<Other>().log;
            f.window(1);
            f.frame(1);
            const auto HANDLE = f.handle();
            if (source == Source::BadPlacement) { f.port.windows.at(1).workspace_offset.x = std::nan(""); }
            f.module.interrupt_public_close(HANDLE); // A mapped generation isn't a close.
            CHECK(f.module.active(HANDLE));
            f.module.close(1, f.port.now);
            auto stale = HANDLE;
            ++stale.generation;
            CHECK(!f.module.pending_public_close(stale));
            f.module.interrupt_public_close(stale);
            CHECK(f.module.pending_public_close(HANDLE));
            if (source == Source::ProducerMismatch) { f.module.interrupt_public_close(HANDLE); }
            if (source == Source::TooLate) { f.port.advance(1.1); }
            auto fade = f.fade(817);
            if (source == Source::IncorrectWorkspace) { fade.workspace_bound = true; fade.workspace = 2; }
            f.port.refuse_retention = source == Source::RefusedRetention;
            f.module.bind_fadeout(1, fade, {.id = 17});
            CHECK(log.close_initializations == 0 && log.last_end == End::Interrupted);
            CHECK(!f.module.active(HANDLE) && !f.module.pending_for(1) && !f.module.dying_view_for(817));
            CHECK(f.port.find_visuals(HANDLE) == nullptr);
            CHECK(f.port.suppress_requests == 0 && !f.module.suppresses(817));
            f.port.refuse_retention = false;
            f.module.bind_fadeout(1, f.fade(817), {.id = 17});
            CHECK(log.close_initializations == 0 && !f.module.suppresses(817));
        }
        Fixture other_style;
        other_style.window(1);
        other_style.frame(1);
        const auto HANDLE = other_style.handle();
        other_style.module.close(1, other_style.port.now);
        CHECK(!other_style.module.pending_public_close(HANDLE));
        other_style.module.interrupt_public_close(HANDLE);
        CHECK(other_style.module.pending_for(1) == HANDLE);
        other_style.module.bind_fadeout(1, other_style.fade(817), {.id = 17});
        other_style.module.interrupt_public_close(HANDLE);
        CHECK(other_style.module.active(HANDLE) && other_style.module.suppresses(817));
    }

    void public_close_interrupts_only_on_physical_draw_failure_or_revocation() {
        enum class Failure { Draw, Lock, Private, MissingOwner };
        for (const auto failure : {Failure::Draw, Failure::Lock, Failure::Private, Failure::MissingOwner}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            auto &log = f.resources.get<Other>().log;
            f.window(1);
            f.frame(1);
            const auto HANDLE = f.handle();
            f.module.close(1, f.port.now);
            f.module.bind_fadeout(1, f.fade(818), {.id = 18});
            CHECK(f.module.suppresses(818));
            FakeCanvas canvas;
            const FakeCanvas::Image IMAGE{.id = 18};
            const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
                .canvas_box = {}, .image_origin = {}, .image_size = IMAGE.size, .real_pass = true};
            if (failure == Failure::Draw) {
                log.draw_succeeds = false;
                canvas.rendering_capture = true;
                CHECK(!f.module.draw(HANDLE, DRAW)); // Capture can use PassKind::Live, but cannot cancel.
                CHECK(f.module.suppresses(818));
                canvas.rendering_capture = false;
            } else {
                log.during_draw = [&] {
                    if (failure == Failure::Lock) { f.port.unlocked = false; }
                    if (failure == Failure::Private) { f.port.fadeouts.at(818).no_screen_share = true; }
                };
            }
            f.port.scheduled.clear();
            f.port.damage_log.clear();
            if (failure == Failure::MissingOwner) { f.module.interrupt_public_close(HANDLE); }
            else { CHECK(!f.module.draw(HANDLE, DRAW)); }
            CHECK(log.last_end == End::Interrupted && log.ends == 1);
            CHECK(!f.module.active(HANDLE) && !f.module.dying_view_for(818));
            CHECK(!f.module.suppresses(818) && !f.port.suppressed.contains(818));
            CHECK(f.port.find_visuals(HANDLE) == nullptr && !f.port.damage_log.empty());
            CHECK(f.port.scheduled == std::vector<MonitorId>{1});
            f.port.unlocked = true;
            f.port.fadeouts.at(818).no_screen_share = false;
            f.module.bind_fadeout(1, f.fade(818), {.id = 18});
            CHECK(!f.module.draw(HANDLE, DRAW) && !f.module.suppresses(818));
        }
    }

    void public_close_uses_latest_physical_mapped_outcome() {
        enum class Last { Bypass, FailedMaterial, ReplayAndPopup, Native, ModelNativeOnly };
        for (const auto receipt : {Publication::Partial, Publication::Blank}) {
            for (const auto last : {Last::Bypass, Last::FailedMaterial, Last::ReplayAndPopup,
                     Last::Native, Last::ModelNativeOnly}) {
                Fixture f;
                f.policy.style = Other::KIND;
                f.module.configure(f.policy);
                auto &log = f.resources.get<Other>().log;
                log.require_publication = true;
                log.next_publication = receipt;
                f.window(1);
                f.frame(1);
                const auto HANDLE = f.handle();
                FakeCanvas canvas;
                const FakeCanvas::Image IMAGE{.id = 19};
                Draw<FakeCanvas> draw{.canvas = &canvas, .image = &IMAGE, .source = {}, .output = {},
                    .canvas_box = {}, .image_origin = {}, .image_size = IMAGE.size, .real_pass = true, .monitor = 1};
                f.module.before_window(1, canvas, 1, 30);
                CHECK(f.module.draw(HANDLE, draw));
                f.module.before_window(1, canvas, 1, 30); // Same-frame duplicate popup notification.
                f.module.before_window(1, canvas, 2, 31); // Secondary monitor.
                canvas.rendering_snapshot = true;
                f.module.before_window(1, canvas, 1, 32);
                canvas.rendering_snapshot = false;
                canvas.destination_live = true;
                f.module.before_window(1, canvas, 1, 33);
                log.draw_succeeds = false;
                CHECK(!f.module.draw(HANDLE, draw)); // A failed destination capture cannot clear it.
                canvas.destination_live = false;
                draw.monitor = 2;
                CHECK(!f.module.draw(HANDLE, draw)); // Nor can a failed neighboring-output submission.
                draw.monitor = 1;
                if (last == Last::Bypass) { f.module.before_window(1, canvas, 1, 34); }
                if (last == Last::FailedMaterial) { CHECK(!f.module.draw(HANDLE, draw)); }
                if (last == Last::Native || last == Last::ModelNativeOnly) {
                    log.native_presentation = true;
                    f.frame(1);
                    if (last == Last::Native) { f.module.before_window(1, canvas, 1, 34); }
                }
                f.module.close(1, f.port.now);
                const bool UNKNOWN = last == Last::Bypass || last == Last::FailedMaterial;
                const auto EXPECTED = UNKNOWN ? Publication::Unknown : last == Last::Native ? Publication::Native : receipt;
                CHECK(log.closed_publication == EXPECTED);
                CHECK(f.module.pending_for(1).has_value() == !UNKNOWN);
                if (UNKNOWN) {
                    f.module.bind_fadeout(1, f.fade(819), {.id = 19});
                    CHECK(!f.module.active(HANDLE) && !f.module.suppresses(819));
                }
            }
        }
    }

    void public_close_crop_is_validated_before_retention() {
        Fixture f;
        f.policy.style = Other::KIND;
        f.module.configure(f.policy);
        auto &log = f.resources.get<Other>().log;
        f.window(1);
        auto &window = f.port.windows.at(1);
        window.body = window.goal_body = {-5, 25, 100, 80};
        window.capture = {-10, 20, 110, 90};
        window.scale = 1.25;
        f.frame(1);
        const auto HANDLE = f.handle();
        log.reject_crop = CropProof{.crop = {10, 0, 80, 68}, .source = {-12.5, 25, 137.5, 112.5}, .scale = 1.25};
        f.module.close(1, f.port.now);
        auto fade = f.fade(820);
        fade.scale = 1.25;
        fade.image_size = {100, 110};
        f.port.fail_bind = true; // Reaching the retain call would throw.
        f.module.bind_fadeout(1, fade, {.id = 20});
        CHECK(log.close_initializations == 0 && log.last_end == End::Interrupted);
        CHECK(!f.module.active(HANDLE) && !f.module.suppresses(820) && f.port.suppress_requests == 0);
    }

    void public_close_hold_releases_without_a_physical_composite() {
        for (const bool no_layer : {false, true}) {
            Fixture f;
            f.policy.style = Other::KIND;
            f.module.configure(f.policy);
            auto &log = f.resources.get<Other>().log;
            f.window(1);
            f.frame(1);
            const auto HANDLE = f.handle();
            f.module.close(1, f.port.now);
            f.module.bind_fadeout(1, f.fade(821), {.id = 21});
            CHECK(f.module.suppresses(821));
            if (no_layer) { f.module.confirm_public_close_presented(HANDLE, f.port.now, false); }
            else { f.frame(1, STALL_WINDOW_SECONDS + 0.01); }
            CHECK(log.last_end == End::Interrupted && !f.module.active(HANDLE) && !f.module.suppresses(821));
            f.module.confirm_public_close_presented(HANDLE, f.port.now, true);
            CHECK(!f.module.active(HANDLE) && !f.module.suppresses(821));
        }
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
        const Draw<FakeCanvas> draw{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = IMAGE.size,
            .real_pass = true};
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
            if (VIEWS.empty()) {
                continue;
            }
            const auto VIEW = VIEWS.front();
            CHECK(VIEW.no_screen_share);
            CHECK(VIEW.ownership == MODE);
            CHECK(fixture.module.suppresses(800) == (MODE == CloseStart::Owned));
            FakeCanvas canvas;
            const FakeCanvas::Image IMAGE{.id = 80};
            const Draw<FakeCanvas> DRAW{.canvas = &canvas,
                .image = &IMAGE,
                .source = VIEW.render_box,
                .output = VIEW.bounds_device,
                .canvas_box = VIEW.bounds_device,
                .image_origin = {},
                .image_size = VIEW.image_size,
                .scale = VIEW.scale,
                .alpha = VIEW.alpha,
                .real_pass = true};
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
        if (VIEWS.empty()) {
            return;
        }
        const auto &box = VIEWS.front().bounds_device;
        CHECK(std::ranges::any_of(fixture.port.damage_log, [&](const Box &damage) {
            return damage.x == box.x && damage.y == box.y && damage.w == box.w && damage.h == box.h;
        }));
    }

    void isolated_mesh_buffers() {
        using MeshModule = CWindowAnimations<FakeWindowPort, StyleList<MeshStyle>>;
        Fixture fixture;
        fixture.window(1);
        MeshModule::Resources resources;
        MeshModule module{fixture.port, resources};
        Policy const POLICY{.style = MeshStyle::KIND, .enabled = true, .opening = true};
        module.configure(POLICY);
        module.update_settings<MeshStyle>({1});
        module.track(1);
        module.frame({.monitor = 1, .frame = 1, .now = fixture.port.now});
        auto const HANDLE = fixture.port.transformers.at(1);
        FakeCanvas canvas;
        FakeCanvas::Image const IMAGE{.id = 1};
        Draw<FakeCanvas> const DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
        CHECK(module.draw(HANDLE, DRAW));
        auto const &cache = fixture.port.visual_cache(HANDLE);
        CHECK(cache.buffers.window.size() == 1);
        CHECK(cache.scratch_buffers.window.empty());
        canvas.pass_kind = PassKind::WorkspaceSource;
        CHECK(module.draw(HANDLE, DRAW));
        CHECK(cache.buffers.window.size() == 1);
        CHECK(cache.scratch_buffers.window.size() == 1);
        canvas.pass_kind = PassKind::Inert;
        CHECK(module.draw(HANDLE, DRAW));
        CHECK(cache.buffers.window.size() == 1);
        CHECK(cache.scratch_buffers.window.size() == 2);
        module.end_all();
    }
    void independent_fadeouts_reversed() {
        Fixture fixture;
        fixture.window(1);
        fixture.window(2);
        fixture.frame(1);
        auto const FIRST = fixture.handle(1);
        auto const SECOND = fixture.handle(2);
        fixture.module.close(1, fixture.port.now);
        fixture.module.close(2, fixture.port.now);
        auto const SECOND_FADE = fixture.fade(701);
        fixture.module.bind_fadeout(2, SECOND_FADE, {.id = 2});
        auto const FIRST_FADE = fixture.fade(700);
        fixture.module.bind_fadeout(1, FIRST_FADE, {.id = 1});
        CHECK(fixture.module.active(FIRST));
        CHECK(fixture.module.active(SECOND));
        CHECK(fixture.module.suppresses(700));
        CHECK(fixture.module.suppresses(701));
        CHECK(fixture.module.dying_views().size() == 2);
    }
    void workspace_motion_and_teleport() {
        Fixture fixture;
        fixture.policy.opening = false;
        fixture.module.configure(fixture.policy);
        fixture.window(1, 1, false);
        fixture.frame(1);
        auto &window = fixture.port.windows.at(1);
        window.workspace_moving = true;
        window.workspace_offset.x = 10;
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        const auto MOTION = fixture.log().steps.back().motion;
        CHECK(MOTION.has_value());
        if (MOTION) {
            CHECK(MOTION->translated);
        }
        window.workspace_offset.x = 0;
        fixture.frame(1);
        CHECK(fixture.handle() == HANDLE);
        CHECK(fixture.log().steps.back().motion.has_value());
        window.pinned = true;
        fixture.frame(1);
        window.workspace_offset.x = 20;
        fixture.frame(1);
        CHECK(!fixture.log().steps.back().motion.has_value());
        window.body.x += 1000;
        window.capture.x += 1000;
        fixture.frame(1);
        CHECK(fixture.module.active(HANDLE));
        CHECK(!fixture.log().steps.back().motion.has_value());
    }
    void capture_failure_releases_entry() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        auto const HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        auto const FADEOUT = fixture.fade(702);
        fixture.port.fail_bind = true;
        bool threw = false;
        try {
            fixture.module.bind_fadeout(1, FADEOUT, {.id = 1});
        } catch (const std::runtime_error &) {
            threw = true;
        }
        CHECK(threw);
        CHECK(!fixture.module.active(HANDLE));
        CHECK(fixture.port.visuals.empty());
        CHECK(!fixture.module.suppresses(702));
        CHECK(!fixture.module.pending_for(1));
    }
    struct SyntheticCloseItem {
        CloseBatchKey key;
        bool owned = false;
        bool suppressed = false;
        bool image = true;
        bool private_content = false;
    };
    struct FailingCloseSpill : std::vector<SyntheticCloseItem> {
        void reserve(std::size_t) {
            throw std::bad_alloc{};
        }
    };
    template <class Spill>
    void mixed_close_batch(std::size_t count, bool capture) {
        std::vector<SyntheticCloseItem> source;
        for (std::size_t index = count; index > 0; --index) {
            source.push_back({.key = {.z_index = static_cast<int>(index % 4),
                                  .retained = index % 5 == 0,
                                  .identity = index},
                .owned = index % 3 == 0,
                .suppressed = index % 3 != 1,
                .image = index % 6 != 0,
                .private_content = index % 7 == 0});
        }
        const auto ENUMERATE = [&](auto emit) {
            for (const auto &item : source) {
                if (!capture || !item.private_content) {
                    emit(item);
                }
            }
        };
        std::vector<CloseBatchKey> visited, native, owned;
        bool nested = false;
        draw_close_batch<SyntheticCloseItem, 16, Spill>(ENUMERATE, [&](const auto &item) {
            visited.push_back(item.key);
            if (!nested) {
                nested = true;
                // A nested noncapture pass uses its own scratch. Reallocation invalidates all
                // source pointers/iterators; the outer draw must retain a value, not a borrowed
                // record.
                source.reserve(source.capacity() + 100);
                std::size_t inner_count = 0;
                draw_close_batch<SyntheticCloseItem>(
                    ENUMERATE, [&](const auto &) { ++inner_count; });
                CHECK(inner_count == (capture ? count - count / 7 : count));
            }
            const bool use_owned = item.owned && item.image;
            int preparations = 0;
            submit_close([&] { return use_owned || !item.suppressed; },
                [&] {
                    if (++preparations == 1 && item.key.identity % 4 == 1) {
                        throw std::bad_alloc{};
                    }
                    return std::optional{item.key};
                },
                [&] {
                    if (item.key.identity % 3 == 1) {
                        throw std::bad_alloc{};
                    }
                },
                [&](auto content) { (use_owned ? owned : native).push_back(*content); });
        });
        std::vector<CloseBatchKey> expected, expected_native, expected_owned;
        for (const auto &item : source) {
            if (capture && item.private_content) {
                continue;
            }
            expected.push_back(item.key);
            if (item.owned && item.image) {
                expected_owned.push_back(item.key);
            } else if (!item.suppressed) {
                expected_native.push_back(item.key);
            }
        }
        std::ranges::sort(expected);
        std::ranges::sort(expected_native);
        std::ranges::sort(expected_owned);
        CHECK(visited == expected);
        CHECK(native == expected_native);
        CHECK(owned == expected_owned);
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
                [&](const auto &) {
                    ++submissions;
                    throw std::bad_alloc{};
                });
        } catch (const std::bad_alloc &) {
            threw = true;
        }
        CHECK(threw);
        CHECK(submissions == 1);  // A partial submit never restarts collection.
    }
    void backdrop_failure_preserves_close_content() {
        for (const bool dim_queued : {false, true}) {
            std::vector<int> passes;
            submit_close([] { return true; },
                [] { return 2; },
                [&] {
                    if (dim_queued) {
                        passes.push_back(1);
                    }
                    throw std::bad_alloc{};  // Building the next optional backdrop pass.
                },
                [&](int content) { passes.push_back(content); });
            CHECK(passes == (dim_queued ? std::vector<int>{1, 2} : std::vector<int>{2}));
        }
        int preparations = 0;
        bool allowed = true;
        CHECK(!submit_close([&] { return allowed; },
            [&]() -> int {
                ++preparations;
                allowed = false;
                throw std::bad_alloc{};
            },
            [] { CHECK(false); },
            [](int) { CHECK(false); }));
        CHECK(preparations == 1);
    }
    void close_draw_rechecks_callback_invalidation() {
        for (const bool succeeded : {false, true}) {
            Fixture fixture;
            fixture.window(1);
            fixture.frame(1);
            const auto HANDLE = fixture.handle();
            fixture.module.close(1, fixture.port.now);
            fixture.module.bind_fadeout(1, fixture.fade(704), {.id = 4});
            FakeCanvas canvas;
            const FakeCanvas::Image IMAGE{.id = 4};
            const Draw<FakeCanvas> DRAW{.canvas = &canvas,
                .image = &IMAGE,
                .source = {},
                .output = {},
                .canvas_box = {},
                .image_origin = {},
                .image_size = {},
                .real_pass = true};
            fixture.log().draw_succeeds = succeeded;
            const std::weak_ptr<int> STATE = fixture.state(HANDLE)->lifetime;
            fixture.log().during_draw = [&] {
                fixture.module.end_all();
                CHECK(!STATE.expired());
                CHECK(fixture.port.find_visuals(HANDLE) != nullptr);
                // Reuse the pool and grow model storage before the outer callback returns.
                for (WindowId id = 2; id < 40; ++id) {
                    fixture.window(id);
                }
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
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        fixture.module.bind_fadeout(1, fixture.fade(705), {.id = 5});
        const std::weak_ptr<int> STATE = fixture.state(HANDLE)->lifetime;
        FakeCanvas canvas;
        const FakeCanvas::Image IMAGE{.id = 5};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas,
            .image = &IMAGE,
            .source = {},
            .output = {},
            .canvas_box = {},
            .image_origin = {},
            .image_size = {},
            .real_pass = true};
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
        CHECK(!submit_close([&] { return TICKET.valid() && associated; },
            [] { return 1; },
            [&] { associated = false; },
            [&](int pass) { submitted.push_back(pass); }));
        // A stale association skips only that item; valid siblings still submit exactly once.
        CHECK(submit_close([&] { return TICKET.valid(); },
            [] { return 2; },
            [] {},
            [&](int pass) { submitted.push_back(pass); }));
        CHECK(submitted == std::vector<int>{2});
        bool queued = false;
        CHECK(submit_close([&] { return TICKET.valid(); },
            [] { return true; },
            [] {},
            [&](bool pass) { queued = pass; }));
        CHECK(queued);
        owner.invalidate();
        CHECK(!submit_close([&] { return TICKET.valid(); },
            [] { return 3; },
            [] {},
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
            submit_close([] { return true; },
                [] { return 1; },
                [&] { ++backdrops; },
                [&](int) {
                    ++contents;
                    throw std::bad_alloc{};
                });
        } catch (const std::bad_alloc &) {
            threw = true;
        }
        CHECK(threw);
        CHECK(backdrops == 1);
        CHECK(contents == 1);
    }
    void close_submission_rechecks_model_suppression() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        fixture.module.close(1, fixture.port.now);
        const auto TICKET = fixture.module.callback_ticket();
        int native = 0, owned = 0;
        // A native item can acquire an owned association while its backdrop submits.
        CHECK(!submit_close([&] { return TICKET.valid() && !fixture.module.suppresses(706); },
            [] { return 1; },
            [&] { fixture.module.bind_fadeout(1, fixture.fade(706), {.id = 6}); },
            [&](int) { ++native; }));
        CHECK(fixture.module.suppresses(706));
        CHECK(submit_close([&] { return TICKET.valid() && !fixture.module.suppresses(707); },
            [] { return 1; },
            [] {},
            [&](int) { ++native; }));
        CHECK(native == 1);
        // An owned association removed by a callback cannot display its copied source.
        CHECK(!submit_close(
            [&] {
                const auto view = fixture.module.dying_view_for(706);
                return TICKET.valid() && view && view->handle == HANDLE;
            },
            [] { return 1; },
            [&] { fixture.module.monitor_changed(1); },
            [&](int) { ++owned; }));
        CHECK(owned == 0);
        CHECK(!fixture.module.active(HANDLE));
        fixture.module.end_all();
        CHECK(!TICKET.valid());
    }
    void workspace_switch_presentation_policy() {
        Fixture fixture;
        fixture.log().backdrop = true;
        fixture.log().tail = true;
        fixture.window(1);
        fixture.frame(1);
        const auto HANDLE = fixture.handle();
        FakeCanvas canvas;
        FakeCanvas::Image image{.id = 91};
        const Draw<FakeCanvas> DRAW{.canvas = &canvas, .image = &image, .real_pass = true};
        CHECK(fixture.module.draw(HANDLE, DRAW));
        CHECK(fixture.port.retained_mapped_image(HANDLE));

        for (const bool destination : {false, true}) {
            canvas.workspace_source = !destination;
            canvas.destination_live = destination;
            canvas.pass_kind = destination ? PassKind::Live : PassKind::WorkspaceSource;
            CHECK(!fixture.module.draw(HANDLE, DRAW)); // Default suppresses both endpoint passes.
            fixture.log().events.clear();
            fixture.port.backdrop_captures.clear();
            fixture.port.queued_tails.clear();
            const auto DRAWS = canvas.draws.size();
            fixture.module.before_window(1, canvas, 1, 1);
            fixture.module.before_windows(1, canvas);
            fixture.module.after_windows(1, canvas);
            CHECK(!fixture.module.draw(HANDLE, DRAW));
            CHECK(!fixture.module.draw_tail(HANDLE, DRAW, false));
            CHECK(canvas.draws.size() == DRAWS);
            CHECK(fixture.port.backdrop_captures.empty());
            CHECK(fixture.port.queued_tails.empty());
            CHECK(fixture.log().events.empty());
            CHECK(fixture.module.active(HANDLE));
            fixture.policy.during_workspace_switch = true;
            fixture.module.configure(fixture.policy);
            CHECK(fixture.module.draw(HANDLE, DRAW));
            fixture.policy.during_workspace_switch = false;
            fixture.module.configure(fixture.policy);
        }
        canvas.destination_live = false;
        canvas.workspace_source = false;
        canvas.pass_kind = PassKind::WorkspaceSource; // Isolated inventory preview, not a turn.
        CHECK(fixture.module.draw(HANDLE, DRAW));
        canvas.pass_kind = PassKind::Live;
        CHECK(fixture.module.draw(HANDLE, DRAW));
        canvas.native_workspace_switch = true;
        CHECK(!fixture.module.draw(HANDLE, DRAW));
        canvas.native_workspace_switch = false;
        CHECK(fixture.module.draw(HANDLE, DRAW));
        fixture.module.close(1, fixture.port.now);
        fixture.module.bind_fadeout(1, fixture.fade(704), {.id = 91});
        const auto CLOSE = fixture.module.dying_view_for(704);
        CHECK(CLOSE && fixture.module.suppresses(704));
        if (CLOSE) {
            const std::optional<Handle> CLOSE_HANDLE = CLOSE->handle;
            CHECK(workspace_native_fallback_valid(fixture.module, CLOSE_HANDLE, 704, 704, true, false));
            CHECK(!workspace_native_fallback_valid(fixture.module, CLOSE_HANDLE, 704, 705, true, false));
            CHECK(!workspace_native_fallback_valid(fixture.module, CLOSE_HANDLE, 704, 704, false, false));
            CHECK(!workspace_native_fallback_valid(fixture.module, CLOSE_HANDLE, 704, 704, true, true));
            const std::optional<Handle> NO_HANDLE;
            CHECK(workspace_native_fallback_valid(fixture.module, NO_HANDLE, 704, 704, true, false));
            CHECK(!workspace_native_fallback_valid(fixture.module, NO_HANDLE, 705, 705, true, false));
            canvas.workspace_source = true;
            canvas.pass_kind = PassKind::WorkspaceSource;
            CHECK(!fixture.module.draw(CLOSE->handle, DRAW));
            canvas.workspace_source = false;
            canvas.destination_live = true;
            canvas.pass_kind = PassKind::Live;
            CHECK(!fixture.module.draw(CLOSE->handle, DRAW));
            CHECK(fixture.module.active(CLOSE->handle) && fixture.module.suppresses(704));
            canvas.native_workspace_switch = true;
            canvas.destination_live = false;
            CHECK(!fixture.module.draw(CLOSE->handle, DRAW));
            CHECK(fixture.module.active(CLOSE->handle) && fixture.module.suppresses(704));
            canvas.native_workspace_switch = false;
            canvas.destination_live = false;
            CHECK(fixture.module.draw(CLOSE->handle, DRAW));
        }
    }
    void workspace_switch_preserves_public_close_receipt() {
        CHECK(missing_public_close_plane_failed(true, true, true));
        CHECK(!missing_public_close_plane_failed(true, true, false));
        CHECK(!missing_public_close_plane_failed(true, false, true));
        CHECK(!missing_public_close_plane_failed(false, true, true));

        Fixture fixture;
        fixture.policy.close_style = Effect::KIND;
        fixture.policy.during_workspace_switch = false;
        fixture.module.configure(fixture.policy);
        fixture.window(1, 1, false);
        fixture.module.close(1, fixture.port.now);
        const auto handle = fixture.module.pending_for(1);
        CHECK(handle);
        if (!handle) return;
        fixture.module.bind_fadeout(1, fixture.fade(708), {.id = 7});
        CHECK(fixture.module.suppresses(708));
        fixture.port.workspace_switches.insert(1);
        fixture.frame(1, 3.0);
        fixture.frame(1, 3.0);
        CHECK(fixture.module.active(*handle) && fixture.module.suppresses(708));
        CHECK(fixture.resources.get<Effect>().log.steps.empty());
        fixture.port.workspace_switches.erase(1);
        fixture.frame(1);
        CHECK(fixture.module.active(*handle) && fixture.module.suppresses(708));
        fixture.module.confirm_public_close_presented(*handle, fixture.port.now, true);
        fixture.frame(1);
        CHECK(fixture.module.active(*handle));
        CHECK(fixture.resources.get<Effect>().log.steps.size() == 1);
    }
    void workspace_switch_driven_geometry_and_planning() {
        Fixture fixture;
        fixture.policy.during_workspace_switch = false;
        fixture.module.configure(fixture.policy);
        fixture.window(1);
        fixture.window(2, 2);
        const Box goal = fixture.port.windows.at(1).goal_body;
        const Box driven{.x = 130, .y = 140, .w = 35, .h = 25};
        fixture.log().drive = driven;
        fixture.frame(1);
        fixture.frame(2);
        const auto handle = fixture.handle();
        CHECK(detail::same_box(fixture.port.windows.at(1).body, driven));
        CHECK(detail::same_box(fixture.port.windows.at(2).body, driven));

        // Source capture can happen synchronously before a turn becomes active.
        fixture.module.prepare_workspace_capture(1);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, goal));
        CHECK(detail::same_box(fixture.port.windows.at(2).body, driven));
        CHECK(fixture.module.active(handle));
        fixture.port.workspace_switches.insert(1);
        const auto steps = fixture.log().steps.size();
        fixture.frame(1);
        fixture.frame(1);
        CHECK(fixture.log().steps.size() == steps + 2);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, goal));
        CHECK(fixture.module.active(handle));

        // Native extents must remain in the planned output even when the
        // animation's bounds are smaller than its input.
        const Box capture{.x = 10, .y = 20, .w = 160, .h = 140};
        const auto bounds = fixture.module.planned_extents(handle, capture);
        CHECK(detail::same_box(bounds, capture));
        const Box smaller{.x = 10, .y = 20, .w = 30, .h = 20};
        CHECK(detail::same_box(fixture.module.planned_extents(handle, smaller),
            Box{.x = 10, .y = 20, .w = 100, .h = 80}));
        fixture.policy.during_workspace_switch = true;
        fixture.module.configure(fixture.policy);
        const auto animated = fixture.module.planned_extents(handle, capture);
        CHECK(detail::same_box(animated, Box{.x = 10, .y = 20, .w = 100, .h = 80}));
        fixture.frame(1);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, driven));

        // A native switch also releases an already-driven window without a capture.
        fixture.policy.during_workspace_switch = false;
        fixture.module.configure(fixture.policy);
        fixture.frame(1);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, goal));
        fixture.port.workspace_switches.erase(1);
        fixture.frame(1);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, driven));
        fixture.port.workspace_switches.insert(1);
        fixture.log().running = false;
        fixture.frame(1);
        CHECK(detail::same_box(fixture.port.windows.at(1).body, goal));
        CHECK(!fixture.module.active(handle));

        Fixture default_policy;
        default_policy.window(1);
        default_policy.log().drive = driven;
        default_policy.frame(1);
        default_policy.module.prepare_workspace_capture(1);
        CHECK(detail::same_box(default_policy.port.windows.at(1).body, goal));
        default_policy.port.workspace_switches.insert(1);
        default_policy.frame(1);
        CHECK(detail::same_box(default_policy.port.windows.at(1).body, goal));

        Fixture retargeted;
        retargeted.policy.during_workspace_switch = false;
        retargeted.module.configure(retargeted.policy);
        retargeted.window(1);
        retargeted.log().drive = driven;
        retargeted.frame(1);
        const Box new_goal{.x = 220, .y = 110, .w = 140, .h = 100};
        retargeted.port.windows.at(1).goal_body = new_goal;
        retargeted.module.prepare_workspace_capture(1);
        CHECK(detail::same_box(retargeted.port.windows.at(1).body, new_goal));
    }
    void close_lookup_survives_view_scratch_rebuild() {
        Fixture fixture;
        fixture.window(1);
        fixture.frame(1);
        fixture.module.close(1, fixture.port.now);
        fixture.module.bind_fadeout(1, fixture.fade(703), {.id = 1});
        const auto VIEW = fixture.module.dying_view_for(703);
        CHECK(VIEW.has_value());
        CHECK(!fixture.module.dying_view_for(0));
        CHECK(!fixture.module.dying_view_for(999));
        (void)fixture.module.dying_views();
        fixture.module.end_all();
        CHECK(!fixture.module.dying_view_for(703));
        CHECK(VIEW &&
              VIEW->fadeout == 703);  // Copied data survives scratch rebuild and entry removal.
    }
}  // namespace
int main() try {
    opening_override_pins_settings_and_yields_to_native_movement();
    opening_override_yields_to_a_single_drag_sample();
    motion_overrides_preserve_samples_and_pin_the_active_generation();
    motion_override_hands_off_open_and_falls_back_without_reading_private_pixels();
    motion_override_respects_the_acquired_blur_capability();
    closing_override_replaces_mapped_state_and_restores_native_on_failure();
    completed_override_close_stays_suppressed_across_reload();
    external_effect_declines_unsupported_blur_before_ownership();
    shader_close_pre_blur_requires_the_pinned_capability();
    external_effect_checks_privacy_at_entry_bind_and_draw();
    locked_close_replay_does_not_mutate_owned_shutdown();
    opening_movement_and_settlement();
    settled_opening_edge_survives_until_close();
    seated_interval_uses_native_surfaces_before_slot_end();
    protected_window_keeps_native_presentation();
    public_only_close_leaves_restricted_fadeouts_native();
    public_close_publishes_first_frame_at_bind();
    public_close_blank_completion_is_not_native_fallback();
    public_close_rejects_unproven_sources_before_initialization();
    retained_tail_desktop_pixels_are_not_replayed();
    public_close_interrupts_only_on_physical_draw_failure_or_revocation();
    public_close_uses_latest_physical_mapped_outcome();
    public_close_crop_is_validated_before_retention();
    public_close_hold_releases_without_a_physical_composite();
    new_opening_attempt_clears_previous_edge();
    disabled_open_and_move();
    monitor_frames_and_reattach();
    close_continuity_and_association();
    close_timeout_invalid_and_fresh();
    invalidation_and_restore();
    expired_windows_release_tracking_without_erasing_closes();
    monitorless_windows_rebaseline_without_losing_their_animation();
    hidden_windows_require_a_valid_workspace_tail();
    runtime_seed_is_injected_without_changing_fresh_close_seed();
    settings_style_reload_monitor();
    stall_bounds();
    effects_and_stale_draw();
    close_modes_and_disable();
    close_mid_movement();
    tail_and_backdrop();
    retained_tail_final_draw_privacy();
    retained_tail_pixel_provenance();
    retained_tail_retention_denial();
    retained_tail_rechecks_final_submission();
    capacity_and_fadeout_validation();
    pending_invalidation();
    screen_order_and_shutdown();
    screen_survives_window_and_allocation_failure();
    screen_damages_its_final_frame();
    bubble_disable_repaints_each_active_output_before_global_clear();
    nested_configure_supersedes_bubble_disable();
    nested_configure_during_scanout_reconciles_all_cleared_outputs();
    bubble_reload_then_disable_rechecks_every_cleared_output();
    nested_configure_during_style_switch_finishes_mapped_cleanup();
    nested_configure_removing_dying_entry_preserves_mapped_cleanup();
    screen_policy_requires_an_enabled_feature();
    out_of_order_and_duplicate_fadeouts();
    close_placement_and_invalid_fadeouts();
    pass_kind_retention_and_backdrop();
    independent_fadeouts_reversed();
    workspace_motion_and_teleport();
    capture_failure_releases_entry();
    isolated_mesh_buffers();
    dying_draw_adapter_lifecycle();
    dying_damage_follows_native_placement();
    owned_close_draw_failure_restores_native();
    refused_retention_keeps_native();
    retained_close_privacy_is_monotonic();
    queued_close_rechecks_session();
    close_batch_capacity_fallback_and_reentry();
    workspace_switch_presentation_policy();
    workspace_switch_preserves_public_close_receipt();
    workspace_switch_driven_geometry_and_planning();
    close_lookup_survives_view_scratch_rebuild();
    backdrop_failure_preserves_close_content();
    close_draw_rechecks_callback_invalidation();
    close_submission_callback_contract();
    nested_close_draw_keeps_outer_state_and_visuals();
    close_submission_rechecks_model_suppression();
    return g_failures == 0 ? 0 : 1;
} catch (const std::exception &error) {
    (void)std::fprintf(stderr, "unexpected test exception: %s\n", error.what());
    return 1;
} catch (...) {
    (void)std::fprintf(stderr, "unexpected non-standard test exception\n");
    return 1;
}
