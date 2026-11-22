#include "Check.hpp"
#include "FakeWindowPort.hpp"
#include "window/WindowAnimations.hpp"
#include "window/liquid/LiquidStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
    void opening_emerges_and_returns_to_layout_goal() {
        using Module = CWindowAnimations<test::FakeWindowPort, StyleList<SLiquidModule>>;
        test::FakeWindowPort port;
        Module::Resources resources;
        Module animations(port, resources);
        animations.configure({.style = SLiquidModule::KIND, .enabled = true, .opening = true});
        SLiquidModule::Settings settings{};
        settings.open.duration = -1.0F;
        settings.open.strength = 0.65F;
        animations.update_settings<SLiquidModule>(settings);

        constexpr Box SCREEN{.x = 0, .y = 0, .w = 800, .h = 600};
        constexpr Box BODY{.x = 300, .y = 350, .w = 200, .h = 160};
        port.monitors.emplace(
            1, ScreenFacts{.monitor = 1, .now = {}, .screen = SCREEN, .work_area = SCREEN});
        auto &window = port.windows[1];
        window = {.window = 1,
            .monitor = 1,
            .workspace = 1,
            .body = BODY,
            .capture = BODY,
            .goal_body = BODY,
            .screen = SCREEN,
            .work_area = SCREEN,
            .mapped = true,
            .animating_in = true};
        animations.track(1);
        animations.frame({.monitor = 1, .frame = 1, .now = port.advance(1.0 / 60)});
        CHECK(window.body.y >= SCREEN.h - 1);
        window.animating_in = false;  // Warping ends Hyprland's native open flag.

        bool emerged = false;
        for (FrameId frame = 2; frame <= 120 && !port.transformers.empty(); ++frame) {
            animations.frame({.monitor = 1, .frame = frame, .now = port.advance(1.0 / 60)});
            emerged |= window.body.y > BODY.y + 30 && window.body.y < SCREEN.h - 10;
        }
        CHECK(emerged);
        CHECK(port.transformers.empty());
        CHECK(window.body.x == BODY.x && window.body.y == BODY.y);
        CHECK(window.body.w == BODY.w && window.body.h == BODY.h);
        animations.end_all();
    }
}  // namespace

int main() {
    opening_emerges_and_returns_to_layout_goal();
    SLiquidResources resources;
    SLiquidModule::Settings settings{};
    settings.open.duration = -1.0F;  // Unset duration from read_config.
    settings.close.duration = -1.0F;
    settings.open.strength = 0.8;
    settings.close.strength = 0.8;
    settings.movement.strength = 0.8;
    resources.update(settings, SLiquidModule::prepare_settings);
    const Place PLACE{.body = {.x = 300, .y = 200, .w = 400, .h = 300},
        .capture = {.x = 290, .y = 190, .w = 420, .h = 320},
        .goal_body = {.x = 300, .y = 200, .w = 400, .h = 300},
        .rest_capture = {.x = 290, .y = 190, .w = 420, .h = 320},
        .screen = {.x = 0, .y = 0, .w = 1920, .h = 1080},
        .work_area = {.x = 0, .y = 0, .w = 1920, .h = 1080},
        .near = {},
        .opening_edge = std::nullopt};
    auto state = SLiquidModule::start(resources, 42);
    Effects effects;
    CHECK(SLiquidModule::open(state, PLACE, resources, effects));
    CHECK(effects.opening_edge.has_value());
    Step step{.elapsed = 1.0 / 60.0,
        .body = PLACE.body,
        .capture = PLACE.capture,
        .goal_body = PLACE.goal_body,
        .rest_capture = PLACE.rest_capture,
        .screen = PLACE.screen,
        .work_area = PLACE.work_area,
        .motion = std::nullopt,
        .movement_enabled = true,
        .open_enabled = true};
    effects = {};
    CHECK(SLiquidModule::advance(state, step, resources, effects).running);
    CHECK(effects.drive_body.has_value());
    const auto EDGE = state.transition.frame().edge;
    settings.movement.elasticity = 0.93;
    resources.update(settings, SLiquidModule::prepare_settings);
    effects = {};
    SLiquidModule::advance(state, step, resources, effects);
    CHECK(state.mesh.params().elasticity == settings.movement.elasticity);
    CHECK(state.transition.frame().edge == EDGE);
    CHECK(SLiquidModule::close(state, PLACE, resources) == CloseStart::Owned);
    CHECK(state.transition.active());
    CHECK(state.transition.frame().edge == EDGE);
    const auto LATCHED = state.close_strength;
    settings.close.strength = 0.1;
    resources.update(settings, SLiquidModule::prepare_settings);
    step.dying = true;
    effects = {};
    SLiquidModule::advance(state, step, resources, effects);
    CHECK(state.close_strength == LATCHED);
    CHECK(!effects.drive_body.has_value());
    MeshBuffers<MeshVertex> buffers;
    const MeshTarget<MeshVertex> TARGET{.source = {.x = 0, .y = 0, .w = 420, .h = 320},
        .output = {.x = -20, .y = -20, .w = 460, .h = 360},
        .image_origin = {.x = 0, .y = 0},
        .image_size = {.x = 420, .y = 320},
        .scale = 1,
        .buffers = &buffers};
    const auto MESH = SLiquidModule::mesh(state, TARGET, resources);
    CHECK(MESH.window == &buffers.window);
    CHECK(buffers.window.size() == LIQUID_RENDER_VERTEX_COUNT);
    SLiquidModule::end(state, resources, End::Completed);

    auto moving = SLiquidModule::start(resources, 7);
    Step motion_step = step;
    motion_step.dying = false;
    motion_step.motion = Motion{.from_body = PLACE.body,
        .from_capture = PLACE.capture,
        .body = {.x = 350, .y = 200, .w = 400, .h = 300},
        .capture = PLACE.capture,
        .translated = true};
    effects = {};
    SLiquidModule::advance(moving, motion_step, resources, effects);
    CHECK(!moving.mesh.settled());
    Place invalid = PLACE;
    invalid.screen = {};
    invalid.work_area = {};
    CHECK(SLiquidModule::close(moving, invalid, resources) == CloseStart::Native);
    CHECK(!moving.owned);

    auto stalled = SLiquidModule::start(resources, 9);
    effects = {};
    CHECK(SLiquidModule::open(stalled, PLACE, resources, effects));
    step.dying = false;
    step.elapsed = STALL_SECONDS + 0.01;
    step.stalled = true;
    effects = {};
    SLiquidModule::advance(stalled, step, resources, effects);
    CHECK(effects.release_body);
    CHECK(!stalled.transition.active());

    settings.open.duration = 0.0F;
    settings.close.duration = 0.0F;
    resources.update(settings, SLiquidModule::prepare_settings);
    auto native_open = SLiquidModule::start(resources, 10);
    effects = {};
    CHECK(!SLiquidModule::open(native_open, PLACE, resources, effects));
    CHECK(!native_open.transition.active() && !effects.opening_edge);
    CHECK(SLiquidModule::close(moving, PLACE, resources) == CloseStart::Declined);

    settings.open.duration = 150.5F;
    settings.close.duration = 75.25F;
    resources.update(settings, SLiquidModule::prepare_settings);
    auto timed = SLiquidModule::start(resources, 11);
    CHECK(SLiquidModule::open(timed, PLACE, resources, effects));
    step.elapsed = 0.08;
    step.stalled = false;
    effects = {};
    CHECK(SLiquidModule::advance(timed, step, resources, effects).running);
    CHECK(effects.drive_body.has_value());
    effects = {};
    SLiquidModule::advance(timed, step, resources, effects);
    CHECK(effects.release_body);
    CHECK(SLiquidModule::close(timed, PLACE, resources) == CloseStart::Owned);
    step.dying = true;
    CHECK(!SLiquidModule::advance(timed, step, resources, effects).running);
    return g_failures == 0 ? 0 : 1;
}
