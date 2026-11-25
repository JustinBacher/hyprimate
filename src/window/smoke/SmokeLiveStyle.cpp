#include <algorithm>

#include "window/smoke/SmokeRenderer.hpp"
#include "window/smoke/SmokeRoute.hpp"
#include "window/smoke/SmokeStyle.hpp"

namespace {
    hyprimate::EdgeInput edge_input(const hyprimate::animation::Place &place, bool opening) {
        return {.body = opening ? place.goal_body : place.body,
            .capture = opening ? place.rest_capture : place.capture,
            .screen = place.screen,
            .work_area = place.work_area,
            .neighbours = place.near.tiled_goals,
            .origin = std::nullopt,
            .last_window = place.near.alone};
    }
}  // namespace

SSmokeResources::SSmokeResources([[maybe_unused]] SSharedStyleResources &shared)
    : renderer(std::make_shared<CSmokeRenderer>()) {
}

SSmokeModule::State SSmokeModule::start(
    [[maybe_unused]] const Resources &resources, std::uint64_t seed) {
    State state;
    state.smoke.seed(seed);
    return state;
}

bool SSmokeModule::open(State &state,
    const hyprimate::animation::Place &place,
    Resources &resources,
    hyprimate::animation::Effects &effects) {
    if (resources.prepared.open_duration <= 0 || !hyprimate::is_usable(place.goal_body) ||
        !hyprimate::is_usable(place.rest_capture)) {
        return false;
    }
    state.smoke.begin(hyprimate::Transition::Open,
        resources.prepared.open_duration,
        {place.rest_capture.w, place.rest_capture.h},
        hyprimate::smoke_route(resources.prepared.open_direction, edge_input(place, true)));
    effects.snap_to_goal = true;
    effects.settle_native_open_alpha = true;
    return true;
}

bool SSmokeModule::moves(
    const hyprimate::animation::Motion &motion, const Resources &resources) noexcept {
    return resources.prepared.movement.strength > 0 && (motion.translated || motion.resized);
}

hyprimate::animation::Frame SSmokeModule::advance(State &state,
    const hyprimate::animation::Step &step,
    Resources &resources,
    [[maybe_unused]] hyprimate::animation::Effects &effects) {
    auto &smoke = state.smoke;
    const bool POINTER_DRAG = !step.dying && step.movement_enabled && step.held;
    if (!step.dying && !step.open_enabled && smoke.opening()) {
        smoke.reset();
    }
    if (!step.dying && !step.movement_enabled) {
        smoke.stop_motion();
    }
    if (!step.dying && step.movement_enabled && step.motion && moves(*step.motion, resources)) {
        const auto &motion = *step.motion;
        const auto &now = motion.capture;
        const auto &before = motion.from_capture;
        smoke.drive({now.x + now.w * 0.5 - before.x - before.w * 0.5,
                        now.y + now.h * 0.5 - before.y - before.h * 0.5},
            motion.layout_animated && !motion.workspace_moving &&
                !(POINTER_DRAG && resources.prepared.movement.drag_follow == 1.0),
            {now.w - before.w, now.h - before.h});
    }
    auto params = smoke.opening() ? resources.prepared.opening : resources.prepared.movement;
    if (step.dying) {
        params = state.close_params;
    }
    if (!step.dying && !step.movement_enabled) {
        params.strength = 0;
    }
    const hyprimate::Vec2 size{step.capture.w, step.capture.h};
    smoke.advance(std::min(step.elapsed, hyprimate::STALL_SECONDS), size, params, POINTER_DRAG);
    if (!smoke.active()) {
        return {};
    }
    auto bounds = smoke.bounds(size);
    bounds.x -= 2.0;
    bounds.y -= 2.0;
    bounds.w += 4.0;
    bounds.h += 4.0;
    return {.running = true, .bounds = bounds};
}

hyprimate::animation::CloseStart SSmokeModule::close(
    State &state, const hyprimate::animation::Place &place, Resources &resources) {
    if (resources.prepared.close_duration <= 0 || !hyprimate::is_usable(place.capture)) {
        return hyprimate::animation::CloseStart::Declined;
    }
    state.close_params = resources.prepared.closing;
    state.smoke.begin(hyprimate::Transition::Close,
        resources.prepared.close_duration,
        {.x = place.capture.w, .y = place.capture.h},
        hyprimate::smoke_route(resources.prepared.close_direction, edge_input(place, false)));
    return hyprimate::animation::CloseStart::Owned;
}

void SSmokeModule::end(State &state,
    [[maybe_unused]] Resources &resources,
    [[maybe_unused]] hyprimate::animation::End why) noexcept {
    state.field.reset();
    state.capture_field.reset();
}

bool SSmokeModule::warm(Resources &resources) {
    return resources.renderer->warm_up();
}

SSmokeModule::Prepared SSmokeModule::prepare_settings(const Settings &settings) {
    const auto &movement = settings.movement;
    const hyprimate::SmokeParams PARAMS{
        .strength = movement.strength,
        .turbulence = movement.turbulence,
        .trail = movement.trail,
        .trail_length = movement.trailLength,
        .trail_lifetime = movement.trailLifetime / 1000.0,
        .trail_thickness = movement.trailThickness,
        .reform = movement.reform,
        .settle_speed = movement.settleSpeed,
        .mass = movement.mass,
        .roundness = movement.roundness,
        .color = settings.shared.color,
        .density = settings.shared.density,
        .variation = settings.shared.variation,
        .drag_follow = movement.dragFollow,
    };
    auto opening = PARAMS;
    opening.turbulence = settings.open.turbulence;
    auto closing = PARAMS;
    closing.turbulence = settings.close.turbulence;
    closing.settle_speed = hyprimate::SmokeParams{}.settle_speed;
    return {.movement = PARAMS,
        .opening = opening,
        .closing = closing,
        .open_duration = settings.open.duration / 1000.0,
        .close_duration = settings.close.duration / 1000.0,
        .open_direction = settings.open.direction,
        .close_direction = settings.close.direction};
}
