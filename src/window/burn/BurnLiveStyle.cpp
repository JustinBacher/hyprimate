#include "window/burn/BurnStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

SBurnModule::Prepared SBurnModule::prepare_settings(const Settings &settings) {
    const BurnParams common{.variation = settings.shared.variation,
        .ember_density = settings.shared.emberDensity,
        .fragment_density = settings.shared.fragmentDensity,
        .smoke_density = settings.movement.smokeDensity,
        .wind_direction = settings.shared.windDirection,
        .wind_strength = settings.shared.windStrength,
        .gravity_strength = settings.shared.gravityStrength,
        .curl = settings.close.curlIntensity,
        .scorch_lifetime = settings.shared.scorchLifetime,
        .healing_duration = settings.shared.healingDuration};
    auto opening = common;
    opening.duration = settings.open.duration / 1000.0;
    auto closing = common;
    switch (settings.close.debrisDirection) {
        case config::DebrisDirection::Natural:
            closing.direction = BurnDirection::Natural;
            break;
        case config::DebrisDirection::Fall:
            closing.direction = BurnDirection::Fall;
            break;
        case config::DebrisDirection::Rise:
            closing.direction = BurnDirection::Rise;
            break;
        case config::DebrisDirection::Wind:
            closing.direction = BurnDirection::Wind;
            break;
    }
    closing.duration = settings.close.duration / 1000.0;
    closing.consume = settings.close.closingStyle == config::ClosingStyle::Consume;
    return {.open = opening, .close = closing};
}
SBurnModule::State SBurnModule::start(const Resources &, std::uint64_t seed) {
    State state;
    state.seed = seed;
    return state;
}
bool SBurnModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (resources.prepared.open.duration <= 0 || !is_usable(place.rest_capture)) return false;
    state.seed = place.seed;
    if (!state.simulation) state.simulation = std::make_unique<BurnSimulation>();
    state.simulation->open(place.rest_capture, resources.prepared.open, state.seed);
    effects.snap_to_goal = true;
    effects.settle_native_open_alpha = true;
    return true;
}
Frame SBurnModule::advance(State &state, const Step &step, Resources &resources, Effects &) {
    if (!step.dying && state.simulation && !step.open_enabled) return {.interrupted = true};
    if (!state.simulation) {
        if (step.movement_enabled && step.motion) {
            resources.ground.stamp(
                {.capture = step.capture, .progress = 1, .closing = false, .seed = state.seed},
                resources.prepared.open,
                step,
                false);
        }
        return {.bounds = {0, 0, step.capture.w, step.capture.h}};
    }
    state.simulation->advance(step.elapsed, step.capture);
    resources.ground.stamp(state.simulation->front(), state.simulation->params(), step);
    auto bounds = state.simulation->bounds();
    if (step.dying && is_usable(step.screen)) {
        // Owned fadeouts are rendered on their capture output. Windblown ash may
        // leave it; don't allocate an offscreen framebuffer for its full flight.
        const double right = std::min(bounds.x + bounds.w, step.screen.x + step.screen.w);
        const double bottom = std::min(bounds.y + bounds.h, step.screen.y + step.screen.h);
        bounds.x = std::max(bounds.x, step.screen.x);
        bounds.y = std::max(bounds.y, step.screen.y);
        bounds.w = std::max(0.0, right - bounds.x);
        bounds.h = std::max(0.0, bottom - bounds.y);
    }
    bounds.x -= step.capture.x;
    bounds.y -= step.capture.y;
    return {.running = state.simulation->active(), .bounds = bounds};
}
CloseStart SBurnModule::close(State &state, const Place &place, Resources &resources) {
    if (resources.prepared.close.duration <= 0 || !is_usable(place.capture))
        return CloseStart::Declined;
    if (!state.simulation) {
        state.seed = place.seed;
        state.simulation = std::make_unique<BurnSimulation>();
    }
    state.simulation->close(place.capture, resources.prepared.close, state.seed);
    return CloseStart::Owned;
}
void SBurnModule::end(State &state, Resources &resources, End why) noexcept {
    if (why == End::Interrupted) resources.ground.cancel(state.seed);
    state.simulation.reset();
}
bool SBurnModule::moves(const Motion &motion, const Resources &) noexcept {
    return !motion.workspace_shifted && (motion.translated || motion.resized);
}
