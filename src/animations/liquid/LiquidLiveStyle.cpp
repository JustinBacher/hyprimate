#include "animations/liquid/LiquidStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "animations/shared/EdgeStyleConfig.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
EdgeInput edge_input(const Place &place, bool opening, std::optional<Edge> origin) {
    return {.body = opening ? place.goal_body : place.body,
        .capture = opening ? place.rest_capture : place.capture,
        .screen = place.screen,
        .work_area = place.work_area,
        .neighbours = place.near.tiled_goals,
        .origin = origin,
        .last_window = place.near.alone};
}

Box driven_box(const Step &step, const EdgeTransition &transition) {
    const double LEFT = step.body.x - step.capture.x;
    const double TOP = step.body.y - step.capture.y;
    const double RIGHT = step.capture.x + step.capture.w - step.body.x - step.body.w;
    const double BOTTOM = step.capture.y + step.capture.h - step.body.y - step.body.h;
    const auto HALF = transition.bead_half({.x = step.rest_capture.w, .y = step.rest_capture.h});
    const auto CENTRE = transition.bead_centre();
    return {.x = step.rest_capture.x + (0.5 * step.rest_capture.w) + CENTRE.x - HALF.x + LEFT,
        .y = step.rest_capture.y + (0.5 * step.rest_capture.h) + CENTRE.y - HALF.y + TOP,
        .w = std::max(2.0, 2.0 * HALF.x - LEFT - RIGHT),
        .h = std::max(2.0, 2.0 * HALF.y - TOP - BOTTOM)};
}
} // namespace

SLiquidModule::Prepared SLiquidModule::prepare_settings(const Settings &settings) {
    const auto COLOR = [](std::uint32_t argb) {
        const auto CHANNEL = [argb](unsigned int shift) {
            return static_cast<float>((argb >> shift) & 0xFFU) / 255.F;
        };
        return Rgba{.r = CHANNEL(16), .g = CHANNEL(8), .b = CHANNEL(0), .a = CHANNEL(24)};
    };
    return {.movement = {.viscosity = settings.movement.viscosity,
                .elasticity = settings.movement.elasticity,
                .momentum = settings.movement.momentum,
                .variation = settings.movement.variation},
        .open = {.elasticity = settings.open.elasticity, .momentum = settings.open.momentum},
        .close = {.elasticity = settings.close.elasticity, .momentum = settings.close.momentum},
        .movement_strength = settings.movement.strength,
        .open_strength = settings.open.strength,
        .close_strength = settings.close.strength,
        .emerge_drag = settings.open.emergeDrag,
        .close_splash = settings.close.splash,
        .open_color = COLOR(settings.open.color),
        .close_color = COLOR(settings.close.color)};
}

SLiquidModule::State SLiquidModule::start(const Resources &resources, std::uint64_t seed) {
    return State{.mesh = LiquidMesh{resources.prepared.movement, seed}, .origin = std::nullopt};
}

bool SLiquidModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    const auto &prepared = resources.prepared;
    if (!resources.last || !(prepared.open_strength > 0.0)) {
        return false;
    }
    const auto FRAME = resolve_style_open_edge(edge_input(place, true, std::nullopt),
        resources.last->open);
    if (!FRAME) {
        return false;
    }
    EdgeTransition transition;
    transition.begin(Transition::Open, *FRAME, prepared.open, prepared.open_strength);
    if (!transition.active()) {
        return false;
    }
    state.transition = transition;
    state.origin = FRAME->edge;
    effects.opening_edge = FRAME->edge;
    state.box_driven = true;
    state.grid.invalidate();
    return true;
}

LiquidPose SLiquidModule::pose_for(const State &state) {
    const Vec2 BEAD = state.box_driven ? state.transition.bead_centre() : Vec2{};
    return {.mesh = state.mesh, .transition = state.transition,
        .compensation = state.compensation, .open_offset = {.x = -BEAD.x, .y = -BEAD.y},
        .displacement_gain = state.displacement_gain, .box_pose = state.box_driven};
}

Frame SLiquidModule::advance(State &state, const Step &step, Resources &resources, Effects &effects) {
    const auto &prepared = resources.prepared;
    if (!state.dying && state.mesh.params() != prepared.movement) {
        state.mesh.set_params(prepared.movement);
    }

    if (!state.dying && !step.open_enabled && state.box_driven) {
        state.transition.reset();
        state.box_driven = false;
        effects.release_body = true;
    }
    if (!state.dying && !step.movement_enabled) {
        state.mesh.reset();
    }
    if (!state.dying && step.movement_enabled && step.motion && step.motion->translated) {
        const auto &motion = *step.motion;
        state.mesh.record_translation({.x = motion.body.x - motion.from_body.x,
            .y = motion.body.y - motion.from_body.y}, {.x = step.capture.w, .y = step.capture.h},
            prepared.movement_strength);
    }

    const Vec2 SIZE{.x = step.capture.w, .y = step.capture.h};
    if (state.dying && !state.owned) {
        state.mesh.advance(step.elapsed, SIZE);
        state.grid.invalidate();
    } else {
        advance_liquid_pose(state.mesh, state.transition, state.grid, step.elapsed, SIZE,
            LiquidStrength{state.dying ? state.close_strength : prepared.movement_strength},
            LiquidDrive{!state.dying && step.movement_enabled ? prepared.emerge_drag : 0.0});
    }
    if (state.box_driven && !state.transition.active()) {
        state.box_driven = false;
        effects.release_body = true;
    }

    Box capture = step.capture;
    if (state.box_driven) {
        const auto DRIVEN = driven_box(step, state.transition);
        effects.drive_body = DRIVEN;
        capture.w += DRIVEN.w - step.body.w;
        capture.h += DRIVEN.h - step.body.h;
    } else if (effects.release_body) {
        capture = step.rest_capture;
    }
    const Vec2 POSE_SIZE{.x = capture.w, .y = capture.h};
    auto pose = pose_for(state);
    state.displacement_gain = fit_liquid_pose_gain(pose, POSE_SIZE, nullptr);
    state.grid.invalidate();
    pose = pose_for(state);
    Box bounds = liquid_render_extents(pose, {.x = 0.0, .y = 0.0, .w = capture.w, .h = capture.h}, state.grid);
    expand_box(bounds, state.dying ? 16.0 : 4.0);
    if (const auto VISIBLE = liquid_visible_box(pose, {.x = 0.0, .y = 0.0, .w = capture.w, .h = capture.h})) {
        bounds = intersect_boxes(bounds, *VISIBLE);
    }
    const bool RUNNING = state.dying && state.owned ? state.transition.active()
        : state.transition.active() || !state.mesh.settled();
    return {.running = RUNNING, .bounds = bounds};
}

CloseStart SLiquidModule::close(State &state, const Place &place, Resources &resources) {
    state.dying = true;
    state.close_strength = resources.prepared.close_strength;
    if (state.box_driven && is_usable(place.body) && is_usable(place.goal_body)) {
        constexpr double MAX_SCALE = 64.0;
        state.compensation = {.x = std::clamp(place.goal_body.w / place.body.w, 1.0 / MAX_SCALE, MAX_SCALE),
            .y = std::clamp(place.goal_body.h / place.body.h, 1.0 / MAX_SCALE, MAX_SCALE)};
    }
    state.box_driven = false;
    std::optional<EdgeFrame> frame;
    if (state.transition.active()) {
        frame = state.transition.frame();
    } else if (resources.last) {
        frame = resolve_style_close_edge(
            edge_input(place, false, state.origin ? state.origin : place.opening_edge),
            resources.last->close);
    }
    if (frame && std::isfinite(state.close_strength) && state.close_strength > 0.0) {
        state.transition.begin(Transition::Close, *frame, resources.prepared.close,
            state.close_strength, resources.prepared.close_splash);
        state.owned = state.transition.active();
    } else {
        state.transition.reset();
    }
    state.grid.invalidate();
    if (state.owned) {
        return CloseStart::Owned;
    }
    return state.mesh.settled() ? CloseStart::Declined : CloseStart::Native;
}

void SLiquidModule::end([[maybe_unused]] State &state, [[maybe_unused]] Resources &resources, [[maybe_unused]] End why) noexcept {}

bool SLiquidModule::moves(const Motion &motion, const Resources &resources) noexcept {
    return motion.translated && resources.prepared.movement_strength > 0.0;
}
