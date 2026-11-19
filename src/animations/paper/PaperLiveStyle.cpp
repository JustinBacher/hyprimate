#include <algorithm>
#include <cstdint>
#include <limits>

#include "animations/paper/PaperStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

SPaperModule::Prepared SPaperModule::prepare_settings(const Settings &settings) {
    return {
        .open = {.duration = settings.open.duration,
            .crumpliness = settings.open.crumpliness,
            .shading = settings.open.shading,
            .variation = settings.open.variation},
        .close = {.duration = settings.close.duration,
            .crumpliness = settings.close.crumpliness,
            .shading = settings.close.shading,
            .variation = settings.close.variation},
    };
}

SPaperModule::State SPaperModule::start(const Resources &, std::uint64_t) {
    return {};
}

bool SPaperModule::open(State &state, const Place &place, Resources &resources, Effects &) {
    state.crumple.begin(Transition::Open, resources.prepared.open, place.seed);
    state.capture_size = {.x = place.capture.w, .y = place.capture.h};
    state.crumple.frame(state.capture_size, *state.frame);
    return state.crumple.active();
}

Frame SPaperModule::advance(State &state, const Step &step, Resources &, Effects &) {
    if (!step.dying && !step.open_enabled) {
        state.crumple.reset();
    } else {
        state.crumple.advance(step.elapsed);
    }
    if (!state.crumple.active()) {
        return {};
    }
    state.capture_size = {.x = step.capture.w, .y = step.capture.h};
    state.crumple.frame(state.capture_size, *state.frame);

    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (const auto &point : state.frame->position) {
        const double X = state.capture_size.x * (0.5 + point.x);
        const double Y = state.capture_size.y * (0.5 + point.y);
        min_x = std::min(min_x, X);
        min_y = std::min(min_y, Y);
        max_x = std::max(max_x, X);
        max_y = std::max(max_y, Y);
    }
    return {.running = true,
        .bounds = {.x = min_x - 4.0, .y = min_y - 4.0,
            .w = std::max(1.0, max_x - min_x) + 8.0,
            .h = std::max(1.0, max_y - min_y) + 8.0}};
}

CloseStart SPaperModule::close(State &state, const Place &place, Resources &resources) {
    state.capture_size = {.x = place.capture.w, .y = place.capture.h};
    state.crumple.begin(Transition::Close, resources.prepared.close, place.seed);
    state.crumple.frame(state.capture_size, *state.frame);
    return state.crumple.active() ? CloseStart::Owned : CloseStart::Declined;
}

void SPaperModule::end(State &state, Resources &, End) noexcept {
    state.crumple.reset();
}
