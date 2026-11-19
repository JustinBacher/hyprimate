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
    const double top = step.body.y - step.capture.y;
    const double right = step.capture.x + step.capture.w - step.body.x - step.body.w;
    const double bottom = step.capture.y + step.capture.h - step.body.y - step.body.h;
    const auto half = transition.bead_half({step.rest_capture.w, step.rest_capture.h});
    const auto centre = transition.bead_centre();
    return {step.rest_capture.x + 0.5 * step.rest_capture.w + centre.x - half.x + LEFT,
        step.rest_capture.y + 0.5 * step.rest_capture.h + centre.y - half.y + top,
        std::max(2.0, 2.0 * half.x - LEFT - right),
        std::max(2.0, 2.0 * half.y - top - bottom)};
}
} // namespace

SLiquidModule::Prepared SLiquidModule::prepare_settings(const Settings &settings) {
    const auto color = [](std::uint32_t argb) {
        const auto channel = [argb](unsigned int shift) {
            return static_cast<float>((argb >> shift) & 0xFFU) / 255.F;
        };
        return Rgba{channel(16), channel(8), channel(0), channel(24)};
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
        .open_color = color(settings.open.color),
        .close_color = color(settings.close.color)};
}

SLiquidModule::State SLiquidModule::start(const Resources &resources, std::uint64_t seed) {
    return State{.mesh = LiquidMesh{resources.prepared.movement, seed}, .origin = std::nullopt};
}

bool SLiquidModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    const auto &prepared = resources.prepared;
    if (!resources.last || !(prepared.open_strength > 0.0)) return false;
    const auto frame = resolve_style_open_edge(edge_input(place, true, std::nullopt),
        resources.last->open);
    if (!frame) return false;
    EdgeTransition transition;
    transition.begin(Transition::Open, *frame, prepared.open, prepared.open_strength);
    if (!transition.active()) return false;
    state.transition = transition;
    state.origin = frame->edge;
    effects.opening_edge = frame->edge;
    state.box_driven = true;
    state.grid.invalidate();
    return true;
}

LiquidPose SLiquidModule::pose_for(const State &state) {
    const Vec2 bead = state.box_driven ? state.transition.bead_centre() : Vec2{};
    return {.mesh = state.mesh, .transition = state.transition,
        .compensation = state.compensation, .open_offset = {-bead.x, -bead.y},
        .displacement_gain = state.displacement_gain, .box_pose = state.box_driven};
}

Frame SLiquidModule::advance(State &state, const Step &step, Resources &resources, Effects &effects) {
    const auto &prepared = resources.prepared;
    if (!state.dying && state.mesh.params() != prepared.movement)
        state.mesh.set_params(prepared.movement);

    if (!state.dying && !step.open_enabled && state.box_driven) {
        state.transition.reset();
        state.box_driven = false;
        effects.release_body = true;
    }
    if (!state.dying && !step.movement_enabled) state.mesh.reset();
    if (!state.dying && step.movement_enabled && step.motion && step.motion->translated) {
        const auto &motion = *step.motion;
        state.mesh.record_translation({motion.body.x - motion.from_body.x,
            motion.body.y - motion.from_body.y}, {step.capture.w, step.capture.h},
            prepared.movement_strength);
    }

    const Vec2 size{step.capture.w, step.capture.h};
    if (state.dying && !state.owned) {
        state.mesh.advance(step.elapsed, size);
        state.grid.invalidate();
    } else {
        advance_liquid_pose(state.mesh, state.transition, state.grid, step.elapsed, size,
            LiquidStrength{state.dying ? state.close_strength : prepared.movement_strength},
            LiquidDrive{!state.dying && step.movement_enabled ? prepared.emerge_drag : 0.0});
    }
    if (state.box_driven && !state.transition.active()) {
        state.box_driven = false;
        effects.release_body = true;
    }

    Box capture = step.capture;
    if (state.box_driven) {
        const auto driven = driven_box(step, state.transition);
        effects.drive_body = driven;
        capture.w += driven.w - step.body.w;
        capture.h += driven.h - step.body.h;
    } else if (effects.release_body) {
        capture = step.rest_capture;
    }
    const Vec2 pose_size{capture.w, capture.h};
    auto pose = pose_for(state);
    state.displacement_gain = fit_liquid_pose_gain(pose, pose_size, nullptr);
    state.grid.invalidate();
    pose = pose_for(state);
    Box bounds = liquid_render_extents(pose, {0.0, 0.0, capture.w, capture.h}, state.grid);
    expand_box(bounds, state.dying ? 16.0 : 4.0);
    if (const auto visible = liquid_visible_box(pose, {0.0, 0.0, capture.w, capture.h}))
        bounds = intersect_boxes(bounds, *visible);
    const bool running = state.dying && state.owned ? state.transition.active()
        : state.transition.active() || !state.mesh.settled();
    return {.running = running, .bounds = bounds};
}

CloseStart SLiquidModule::close(State &state, const Place &place, Resources &resources) {
    state.dying = true;
    state.close_strength = resources.prepared.close_strength;
    if (state.box_driven && is_usable(place.body) && is_usable(place.goal_body)) {
        constexpr double max_scale = 64.0;
        state.compensation = {std::clamp(place.goal_body.w / place.body.w, 1.0 / max_scale, max_scale),
            std::clamp(place.goal_body.h / place.body.h, 1.0 / max_scale, max_scale)};
    }
    state.box_driven = false;
    const bool continuing = state.transition.active();
    const auto frame = continuing ? std::optional<EdgeFrame>{state.transition.frame()}
        : resources.last ? resolve_style_close_edge(
            edge_input(place, false, state.origin ? state.origin : place.opening_edge),
            resources.last->close) : std::nullopt;
    if (frame && std::isfinite(state.close_strength) && state.close_strength > 0.0) {
        state.transition.begin(Transition::Close, *frame, resources.prepared.close,
            state.close_strength, resources.prepared.close_splash);
        state.owned = state.transition.active();
    } else {
        state.transition.reset();
    }
    state.grid.invalidate();
    if (state.owned) return CloseStart::Owned;
    return state.mesh.settled() ? CloseStart::Declined : CloseStart::Native;
}

void SLiquidModule::end(State &, Resources &, End) noexcept {}

bool SLiquidModule::moves(const Motion &motion, const Resources &resources) noexcept {
    return motion.translated && resources.prepared.movement_strength > 0.0;
}
