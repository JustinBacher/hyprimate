#include "Check.hpp"
#include "animations/liquid/LiquidStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

int main() {
    SLiquidResources resources;
    SLiquidModule::Settings settings{};
    settings.open.strength = 0.8;
    settings.close.strength = 0.8;
    settings.movement.strength = 0.8;
    resources.update(settings, SLiquidModule::prepare_settings);
    const Place place{.body = {300, 200, 400, 300}, .capture = {290, 190, 420, 320},
        .goal_body = {300, 200, 400, 300}, .rest_capture = {290, 190, 420, 320},
        .screen = {0, 0, 1920, 1080}, .work_area = {0, 0, 1920, 1080},
        .near = {}, .opening_edge = std::nullopt};
    auto state = SLiquidModule::start(resources, 42);
    Effects effects;
    CHECK(SLiquidModule::open(state, place, resources, effects));
    CHECK(effects.opening_edge.has_value());
    Step step{.elapsed = 1.0 / 60.0, .body = place.body, .capture = place.capture,
        .goal_body = place.goal_body, .rest_capture = place.rest_capture,
        .screen = place.screen, .work_area = place.work_area,
        .motion = std::nullopt, .movement_enabled = true, .open_enabled = true};
    effects = {};
    CHECK(SLiquidModule::advance(state, step, resources, effects).running);
    CHECK(effects.drive_body.has_value());
    const auto edge = state.transition.frame().edge;
    settings.movement.elasticity = 0.93;
    resources.update(settings, SLiquidModule::prepare_settings);
    effects = {};
    SLiquidModule::advance(state, step, resources, effects);
    CHECK(state.mesh.params().elasticity == settings.movement.elasticity);
    CHECK(state.transition.frame().edge == edge);
    CHECK(SLiquidModule::close(state, place, resources) == CloseStart::Owned);
    CHECK(state.transition.active());
    CHECK(state.transition.frame().edge == edge);
    const auto latched = state.close_strength;
    settings.close.strength = 0.1;
    resources.update(settings, SLiquidModule::prepare_settings);
    step.dying = true;
    effects = {};
    SLiquidModule::advance(state, step, resources, effects);
    CHECK(state.close_strength == latched);
    CHECK(!effects.drive_body.has_value());
    MeshBuffers<MeshVertex> buffers;
    const MeshTarget<MeshVertex> target{.source = {0, 0, 420, 320},
        .output = {-20, -20, 460, 360}, .image_origin = {0, 0},
        .image_size = {420, 320}, .scale = 1, .buffers = buffers};
    const auto mesh = SLiquidModule::mesh(state, target, resources);
    CHECK(mesh.window == &buffers.window);
    CHECK(buffers.window.size() == LIQUID_RENDER_VERTEX_COUNT);
    SLiquidModule::end(state, resources, End::Completed);

    auto moving = SLiquidModule::start(resources, 7);
    Step motion_step = step;
    motion_step.dying = false;
    motion_step.motion = Motion{.from_body = place.body, .from_capture = place.capture,
        .body = {350, 200, 400, 300}, .capture = place.capture, .translated = true};
    effects = {};
    SLiquidModule::advance(moving, motion_step, resources, effects);
    CHECK(!moving.mesh.settled());
    Place invalid = place;
    invalid.screen = {};
    invalid.work_area = {};
    CHECK(SLiquidModule::close(moving, invalid, resources) == CloseStart::Native);
    CHECK(!moving.owned);

    auto stalled = SLiquidModule::start(resources, 9);
    effects = {};
    CHECK(SLiquidModule::open(stalled, place, resources, effects));
    step.dying = false;
    step.elapsed = STALL_SECONDS + 0.01;
    step.stalled = true;
    effects = {};
    SLiquidModule::advance(stalled, step, resources, effects);
    CHECK(effects.release_body);
    CHECK(!stalled.transition.active());
    return g_failures == 0 ? 0 : 1;
}
