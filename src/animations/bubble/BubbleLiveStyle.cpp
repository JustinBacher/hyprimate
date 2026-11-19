#include "animations/bubble/BubbleStyle.hpp"

namespace {
hyprimate::BubbleShape bubble_shape(hyprimate::config::Shape shape) {
    switch (shape) {
        case hyprimate::config::Shape::Ellipse: return hyprimate::BubbleShape::Ellipse;
        case hyprimate::config::Shape::Circle: return hyprimate::BubbleShape::Circle;
        case hyprimate::config::Shape::Pillow: return hyprimate::BubbleShape::Pillow;
    }
    return hyprimate::BubbleShape::Pillow;
}
}

SBubbleModule::Prepared SBubbleModule::prepare_settings(const Settings &settings) {
    const auto &movement = settings.movement;
    const auto &open = settings.open;
    return {
        .movement = {.strength = movement.strength,
            .microbubbles = movement.microbubbles,
            .trail = movement.trail,
            .roundness = movement.roundness,
            .shape = bubble_shape(movement.shape),
            .shape_speed = movement.shapeSpeed,
            .transparency = movement.transparency},
        .formation = {.count = open.count,
            .birthRate = open.birthRate,
            .sizeVariation = open.sizeVariation,
            .growthVariation = open.growthVariation,
            .interaction = open.interaction,
            .damping = open.damping,
            .buoyancy = open.buoyancy,
            .linger = open.linger},
        .open_duration = open.duration,
        .close_duration = settings.close.duration,
    };
}

SBubbleModule::State SBubbleModule::start([[maybe_unused]] const Resources &resources, std::uint64_t seed) {
    State state;
    state.bubble.seed_motion(seed);
    return state;
}

bool SBubbleModule::open(State &state, const hyprimate::animation::Place &place,
    Resources &resources, hyprimate::animation::Effects &effects) {
    if (!hyprimate::is_usable(place.goal_body) || !hyprimate::is_usable(place.rest_capture)) {
        return false;
    }
    state.bubble.begin(hyprimate::Transition::Open,
        hyprimate::BubbleDuration{resources.prepared.open_duration}, place.seed,
        {.x = place.rest_capture.w, .y = place.rest_capture.h}, resources.prepared.formation);
    if (!state.bubble.active()) {
        return false;
    }
    state.shell_impacts_deposited = state.window_impacts_deposited = false;
    state.impacts.start();
    effects.snap_to_goal = true;
    effects.settle_native_open_alpha = true;
    return true;
}

bool SBubbleModule::moves(const hyprimate::animation::Motion &motion,
    const Resources &resources) noexcept {
    return motion.translated && resources.prepared.movement.strength > 0;
}

hyprimate::animation::Frame SBubbleModule::advance(State &state,
    const hyprimate::animation::Step &step, Resources &resources,
    [[maybe_unused]] hyprimate::animation::Effects &effects) {
    if (step.dying) {
        state.bubble.advance(step.elapsed);
    } else {
        if (!step.open_enabled && state.bubble.opening()) {
            state.bubble.reset();
            state.impacts.cancel();
        }
        if (!step.movement_enabled && state.bubble.stirred()) {
            state.bubble.stop_motion();
            state.wake = 0;
        }
        if (step.movement_enabled && step.motion && moves(*step.motion, resources) &&
            (!state.bubble.active() || state.bubble.landed())) {
            const auto &motion = *step.motion;
            state.bubble.drive({.x = motion.body.x + (motion.body.w * .5) -
                    (motion.from_body.x + (motion.from_body.w * .5)),
                .y = motion.body.y + (motion.body.h * .5) -
                    (motion.from_body.y + (motion.from_body.h * .5))},
                motion.layout_animated && !motion.workspace_moving);
        }
        const hyprimate::Box BODY{.x = step.body.x - (step.capture.x + (step.capture.w * .5)),
            .y = step.body.y - (step.capture.y + (step.capture.h * .5)),
            .w = step.body.w, .h = step.body.h};
        state.bubble.advance(step.elapsed, {.x = step.capture.w, .y = step.capture.h}, BODY,
            step.movement_enabled ? resources.prepared.movement : hyprimate::BubbleMotionParams{});
    }
    state.impacts.advance(step.elapsed);
    const bool RUNNING = state.bubble.active() || (!step.dying && state.bubble.stirred());
    if (!state.bubble.active() && state.impacts.running()) {
        state.impacts.release();
    }
    auto bounds = state.bubble.bounds({.x = step.capture.w, .y = step.capture.h});
    if (hyprimate::is_usable(bounds)) {
        bounds.x -= 2;
        bounds.y -= 2;
        bounds.w += 4;
        bounds.h += 4;
    }
    return {.running = RUNNING, .bounds = bounds,
        .backdrop = step.dying || state.bubble.opening() || state.bubble.stirred()};
}

hyprimate::animation::CloseStart SBubbleModule::close(State &state,
    const hyprimate::animation::Place &place, Resources &resources) {
    state.bubble.begin(hyprimate::Transition::Close,
        hyprimate::BubbleDuration{resources.prepared.close_duration}, place.seed,
        {.x = place.capture.w, .y = place.capture.h});
    if (!state.impacts.running()) {
        state.impacts.start();
    }
    return hyprimate::animation::CloseStart::Owned;
}

void SBubbleModule::end(State &state, [[maybe_unused]] Resources &resources, hyprimate::animation::End why) noexcept {
    if (why == hyprimate::animation::End::Interrupted) {
        state.impacts.cancel();
    } else {
        state.impacts.release();
    }
}

void SBubbleModule::warm(Resources &resources) {
    resources.renderer->warm_up();
}

bool SBubbleModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw, Resources &resources) {
    if (!*draw.image || draw.scale <= 0) {
        return false;
    }
    const hyprimate::Vec2 SIZE{.x = draw.source.w / draw.scale, .y = draw.source.h / draw.scale};
    const auto FRAME = state.bubble.frame(SIZE);
    const auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
        ? resources.renderer : resources.scratch_renderer;
    draw.canvas->layer() = renderer->render(draw.canvas->context(), SBubbleDraw{
        .window = (*draw.image)->getTexture(),
        .wallpaper = draw.backdrop != nullptr ? *draw.backdrop : SP<SBubbleWallpaper>{},
        .frame = FRAME,
        .canvas = {draw.canvas_box.w, draw.canvas_box.h},
        .source = {draw.source.x, draw.source.y, draw.source.w, draw.source.h},
        .texture_origin = draw.image_origin,
        .texture_size = draw.image_size,
        .output_origin = {.x = draw.canvas_box.x, .y = draw.canvas_box.y},
        .wake = {},
        .pass = draw.canvas->pass(),
    });
    if (!draw.canvas->layer()) {
        return false;
    }
    if (draw.real_pass && draw.canvas->pass() == hyprimate::animation::PassKind::Live) {
        const hyprimate::Vec2 CENTRE{.x = (draw.source.x + (draw.source.w * .5)) / draw.scale,
            .y = (draw.source.y + (draw.source.h * .5)) / draw.scale};
        deposit_bubble_impacts(state, resources.vestige, FRAME, draw.monitor, CENTRE);
        const auto &motion = state.bubble.motion();
        if (!motion.trail().empty() || !motion.releases().empty()) {
            state.wake = resources.vestige.trail(draw.monitor, draw.window, state.wake, CENTRE,
                motion.trail(), motion.releases());
            state.bubble.clear_emissions();
        }
    }
    return true;
}

void SBubbleModule::Screen::tick(Resources &resources,
    const hyprimate::animation::ScreenFacts &facts) {
    resources.vestige.set_enabled(facts.enabled);
    if (facts.enabled && facts.monitor != 0) {
        resources.vestige.tick(facts);
    }
}

bool SBubbleModule::Screen::needs_frame(const Resources &resources,
    hyprimate::animation::MonitorId monitor) {
    return monitor != 0 && resources.vestige.active(monitor);
}

void SBubbleModule::Screen::under_window(Resources &resources,
    const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw) {
    const bool LIVE = draw.canvas->pass() == hyprimate::animation::PassKind::Live && draw.real_pass;
    const auto &renderer = LIVE ? resources.renderer : resources.scratch_renderer;
    resources.vestige.queue_under(draw.canvas->context(), draw.screen, draw.window, renderer, LIVE);
}

void SBubbleModule::Screen::after_windows(Resources &resources,
    const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw) {
    const bool LIVE = draw.canvas->pass() == hyprimate::animation::PassKind::Live && draw.real_pass;
    const auto &renderer = LIVE ? resources.renderer : resources.scratch_renderer;
    resources.vestige.queue(draw.canvas->context(), draw.screen, renderer, LIVE);
}

void SBubbleModule::Screen::clear(Resources &resources,
    std::optional<hyprimate::animation::MonitorId> monitor) {
    resources.vestige.clear(monitor);
}
