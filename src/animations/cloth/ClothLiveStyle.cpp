#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "animations/cloth/ClothStyle.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"

namespace {
using namespace hyprimate;
using namespace hyprimate::animation;

EdgeInput edge_input(const Place &place, bool opening, std::optional<Edge> origin) {
    return {.body = opening ? place.goal_body : place.body,
        .capture = opening ? place.rest_capture : place.capture,
        .screen = place.screen, .work_area = place.work_area,
        .neighbours = place.near.tiled_goals, .origin = origin, .last_window = place.near.alone};
}

Box bounds(const SheetFrame &frame, Vec2 size) {
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double max_x = -min_x;
    double max_y = -min_x;
    for (const auto &point : frame.position) {
        const double POS_X = size.x * (0.5 + point.x);
        const double POS_Y = size.y * (0.5 + point.y);
        min_x = std::min(min_x, POS_X);
        min_y = std::min(min_y, POS_Y);
        max_x = std::max(max_x, POS_X);
        max_y = std::max(max_y, POS_Y);
    }
    return {.x = min_x - 4, .y = min_y - 4, .w = std::max(1.0, max_x - min_x) + 8,
        .h = std::max(1.0, max_y - min_y) + 8};
}
}

SClothModule::Prepared SClothModule::prepare_settings(const Settings &settings) {
    const auto &movement = settings.movement;
    const auto &open = settings.open;
    const auto &close = settings.close;
    return {
        .movement = {.strength = movement.strength, .flutter = movement.flutter,
            .shading = movement.shading, .variation = movement.variation, .shine = movement.shine,
            .springiness = movement.springiness, .gravity = movement.gravity},
        .open = {.duration = open.duration / 1000.0, .flutter = open.flutter, .shading = open.shading,
            .variation = open.variation, .shine = open.shine},
        .close = {.duration = close.duration / 1000.0, .flutter = close.flutter, .shading = close.shading,
            .variation = close.variation, .shine = close.shine},
    };
}

SClothModule::State SClothModule::start(const Resources &resources, std::uint64_t seed) {
    State state;
    state.movement = resources.prepared.movement;
    state.seed = seed;
    return state;
}

bool SClothModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (resources.prepared.open.duration <= 0 || !is_usable(place.goal_body) ||
        !is_usable(place.rest_capture) || !resources.last) {
        return false;
    }
    const auto EDGE = resolve_style_open_edge(edge_input(place, true, std::nullopt), resources.last->open);
    ClothFlutter opening;
    opening.begin(Transition::Open, resources.prepared.open, place.seed,
        place.rest_capture, place.near.obstacles);
    if (!opening.active()) {
        return false;
    }
    state.origin = EDGE ? std::optional{EDGE->edge} : std::nullopt;
    state.cloth = std::move(opening);
    state.capture_size = {.x = place.rest_capture.w, .y = place.rest_capture.h};
    state.cloth.frame(state.capture_size, *state.frame);
    effects.snap_to_goal = true;
    return true;
}

bool SClothModule::moves(const Motion &motion, const Resources &resources) noexcept {
    const auto &params = resources.prepared.movement;
    return motion.translated && params.strength > 0 && std::isfinite(params.strength) &&
        std::isfinite(params.flutter) && std::isfinite(params.shading) &&
        std::isfinite(params.variation);
}

Frame SClothModule::advance(State &state, const Step &step, Resources &resources, [[maybe_unused]] Effects &effects) {
    if (!step.dying && state.movement != resources.prepared.movement) {
        state.movement = resources.prepared.movement;
        state.cloth.set_movement_params(state.movement);
    }
    if (!step.dying && ((!step.movement_enabled && state.cloth.moving()) ||
            (!step.open_enabled && state.cloth.active() && !state.cloth.moving()))) {
        state.cloth.reset();
    }
    if (!step.dying && step.movement_enabled && step.motion &&
        moves(*step.motion, resources) && !state.cloth.moving()) {
        state.cloth.begin_movement(state.movement, state.seed, step.motion->from_capture);
    }
    if (!step.dying && state.cloth.moving() && step.motion) {
        state.cloth.update_movement(step.motion->capture, step.elapsed);
    }
    if (!state.cloth.active()) {
        return {};
    }
    // Cloth aborts a stalled run; the model handles closing as completed.
    state.cloth.advance(step.elapsed);
    if (!state.cloth.active()) {
        return {};
    }
    state.capture_size = {.x = step.capture.w, .y = step.capture.h};
    state.cloth.frame(state.capture_size, *state.frame);
    return {.running = true, .bounds = bounds(*state.frame, state.capture_size)};
}

CloseStart SClothModule::close(State &state, const Place &place, Resources &resources) {
    if (resources.prepared.close.duration <= 0 || !resources.last) {
        return CloseStart::Declined;
    }
    const auto EDGE = resolve_style_close_edge(edge_input(place, false, state.origin), resources.last->close);
    if (!EDGE) {
        return CloseStart::Declined;
    }
    state.cloth.begin(Transition::Close, resources.prepared.close, place.seed, place.capture,
        place.near.obstacles, ClothCloseGeometry{.edge = EDGE->edge, .screen = place.screen});
    if (!state.cloth.active()) {
        return CloseStart::Declined;
    }
    state.capture_size = {.x = place.capture.w, .y = place.capture.h};
    state.cloth.frame(state.capture_size, *state.frame);
    return CloseStart::Owned;
}

void SClothModule::end(State &state, [[maybe_unused]] Resources &resources, [[maybe_unused]] End why) noexcept {
    state.cloth.reset();
}
