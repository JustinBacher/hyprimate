#include "window/shader/ShaderStyle.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string_view>
#include <utility>

#include <hyprland/src/debug/log/Logger.hpp>

#include "shader/GlesRuntime.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
    constexpr std::array EVENTS{shader::Event::Open, shader::Event::Close,
        shader::Event::Move, shader::Event::Resize};
    constexpr std::array EVENT_NAMES{"open", "close", "move", "resize"};
    constexpr std::size_t OPEN = 0, CLOSE = 1, MOVE = 2, RESIZE = 3;

    constexpr std::size_t motion_index(MotionKind kind) noexcept {
        return kind == MotionKind::Move ? MOVE : RESIZE;
    }

    bool motion_event(shader::Event event) noexcept {
        return event == shader::Event::Move || event == shader::Event::Resize;
    }

    void trace_failure(shader::Event event, std::string_view reason) noexcept {
        try {
            LOG(Log::WARN, "[hyprimate] shader {} using native fallback: {}",
                EVENT_NAMES[static_cast<std::size_t>(event)], reason);
        } catch (const std::bad_alloc &) {}
    }

    bool begin(SShaderStyle &state, const Place &place, const SShaderModule::Selection &settings,
        shader::Event event) try {
        if (!settings.program || settings.duration <= 0 || !is_usable(place.capture)) return false;
        auto renderer = std::make_unique<shader::WindowRenderer>(settings.program);
        std::string error;
        if (!renderer->resize(place.capture, place.scale, error)) {
            trace_failure(event, error);
            return false;
        }
        state = SShaderStyle{.renderer = std::move(renderer), .seed = place.seed,
            .duration = settings.duration, .event = event};
        if (place.near.cursor) {
            state.pointer_x = static_cast<float>((place.near.cursor->x - place.capture.x) / place.capture.w);
            state.pointer_y = static_cast<float>((place.near.cursor->y - place.capture.y) / place.capture.h);
            state.pointer_valid = std::isfinite(state.pointer_x) && std::isfinite(state.pointer_y);
            if (!state.pointer_valid) state.pointer_x = state.pointer_y = 0.5F;
        }
        if (event == shader::Event::Open) {
            try {
                LOG(Log::DEBUG, "[hyprimate] shader open source copies=0 copy_bytes=0");
            } catch (const std::bad_alloc &) {}
        }
        return true;
    } catch (const std::exception &error) {
        trace_failure(event, error.what());
        return false;
    }
    std::array<float, 4> geometry(const Box &box) noexcept {
        return {static_cast<float>(box.x), static_cast<float>(box.y),
            static_cast<float>(box.w), static_cast<float>(box.h)};
    }

    hyprimate::animation::Frame tick(SShaderStyle &state, const Step &step) noexcept {
        const bool MOTION = motion_event(state.event);
        const auto DELTA = static_cast<float>(step.elapsed);
        const auto PREVIOUS = state.elapsed;
        state.elapsed = MOTION ? PREVIOUS + DELTA : std::min(state.duration, PREVIOUS + DELTA);
        const auto *sample = step.motion_geometry ? &*step.motion_geometry : nullptr;
        const bool SETTLING = MOTION && sample && !sample->active;
        if (MOTION) {
            state.settle_elapsed = SETTLING ? std::min(state.duration, state.settle_elapsed + DELTA) : 0;
        }
        shader::Frame frame{.tick = ++state.tick, .seed = state.seed, .event = state.event,
            .progress = (MOTION ? state.settle_elapsed : state.elapsed) / state.duration,
            .elapsed = state.elapsed, .delta = MOTION ? DELTA : state.elapsed - PREVIOUS,
            .duration = state.duration,
            .width = static_cast<float>(step.capture.w), .height = static_cast<float>(step.capture.h),
            .scale = static_cast<float>(step.scale), .pointer_x = state.pointer_x,
            .pointer_y = state.pointer_y, .pointer_valid = state.pointer_valid,
            .fullscreen = step.fullscreen,
            .geometry = geometry(sample ? sample->current : step.body),
            .previous_geometry = geometry(sample ? sample->previous : step.body),
            .held = MOTION && (state.event == shader::Event::Move ? step.held : step.resize_held),
            .settling = SETTLING};
        if (sample && sample->active && sample->sample_seconds > 0) {
            const auto &from = sample->previous;
            const auto &to = sample->current;
            frame.velocity = geometry({(to.x - from.x) / sample->sample_seconds,
                (to.y - from.y) / sample->sample_seconds, (to.w - from.w) / sample->sample_seconds,
                (to.h - from.h) / sample->sample_seconds});
            for (auto &component : frame.velocity) {
                if (!std::isfinite(component)) component = 0;
            }
        }
        state.renderer->advance(frame);
        return {.running = true, .bounds = state.renderer->bounds({0, 0, step.capture.w, step.capture.h})};
    }
}

std::array<std::string, SShaderModule::EVENT_COUNT> SShaderModule::refresh(Resources &resources,
    std::array<Request, EVENT_COUNT> requests, bool force) {
    auto next = resources.prepared;
    std::array<std::string, EVENT_COUNT> errors;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto &request = requests[i];
        if (!force && resources.requested[i] == request) continue;
        const bool selected = !request.path.empty();
        if (!selected || request.duration == 0) {
            next[i] = {.program = {}, .duration = 0, .selected = selected};
        } else {
            std::shared_ptr<shader::WindowProgram> candidate;
            try {
                if (!std::isfinite(request.duration) || request.duration < 0 || request.duration > 60000) {
                    errors[i] = "invalid effect duration";
                } else {
                    candidate = shader::prepare_window_program(EVENTS[i],
                        request.path, request.parameters, errors[i]);
                }
            } catch (const std::exception &error) {
                errors[i] = error.what();
            }
            if (candidate) {
                next[i] = {std::move(candidate), static_cast<float>(request.duration * 0.001), true};
            } else {
                // A failed reload must not replace a valid program or its duration.
                // An invalid first request still directs this event to native fallback.
                next[i].selected = true;
            }
        }
        resources.requested[i] = std::move(request);
    }
    resources.update(next, prepare_settings);
    return errors;
}

SShaderModule::Prepared SShaderModule::prepare_settings(const Settings &settings) {
    return settings;
}

SShaderModule::State SShaderModule::start(const Resources &, std::uint64_t seed) {
    return {.renderer = {}, .seed = seed};
}

bool SShaderModule::open(State &state, const Place &place, Resources &resources, Effects &) {
    // Keep the compositor's fade live. The borrowed raster includes its alpha,
    // so any draw that falls back already has the correct native presentation.
    return begin(state, place, resources.prepared[OPEN], EVENTS[OPEN]);
}

bool SShaderModule::can_begin_motion(const Resources &resources, MotionKind kind, bool blurred) noexcept {
    const auto &selection = resources.prepared[motion_index(kind)];
    return selection.program && selection.duration > 0 &&
           (!blurred || shader::window_program_supports_blur(selection.program));
}

bool SShaderModule::supports_open_blur(const Resources &resources) noexcept {
    return shader::window_program_supports_blur(resources.prepared[OPEN].program);
}

bool SShaderModule::mapped_blur_safe(const State &state) noexcept {
    return state.renderer && state.renderer->supports_blur();
}

bool SShaderModule::close_blur_safe(const State &state) noexcept {
    return mapped_blur_safe(state);
}

bool SShaderModule::begin_motion(State &state, const Place &place, Resources &resources, MotionKind kind) {
    const auto INDEX = motion_index(kind);
    return begin(state, place, resources.prepared[INDEX], EVENTS[INDEX]);
}

CloseStart SShaderModule::close(State &state, const Place &place, Resources &resources) {
    return begin(state, place, resources.prepared[CLOSE], EVENTS[CLOSE]) ? CloseStart::Owned : CloseStart::Declined;
}

bool SShaderModule::bind_close_source(State &state, const Box &source,
    const CHyprAnimationCanvas::Image &image) noexcept {
    try {
        std::string error;
        if (state.renderer && state.renderer->bind_close_source(source, image, error)) return true;
        trace_failure(shader::Event::Close, error);
    } catch (const std::exception &error) {
        trace_failure(shader::Event::Close, error.what());
    }
    return false;
}

hyprimate::animation::Frame SShaderModule::initial_close_frame(State &state, const Step &step,
    Resources &) noexcept {
    return state.renderer ? tick(state, step) : hyprimate::animation::Frame{.interrupted = true};
}

hyprimate::animation::Frame SShaderModule::advance(State &state, const Step &step, Resources &,
    Effects &) {
    const bool CLOSING = state.event == shader::Event::Close;
    const bool MOTION = motion_event(state.event);
    if (!state.renderer || state.failed || (state.event == shader::Event::Open && !step.open_enabled) ||
        (MOTION && (!step.movement_enabled || !step.motion_geometry))) {
        return {.interrupted = true};
    }
    if (MOTION ? (!step.motion_geometry->active && state.settle_elapsed >= state.duration)
               : state.elapsed >= state.duration) return {};
    if (!CLOSING) {
        try {
            std::string error;
            if (!state.renderer->resize(step.capture, step.scale, error)) {
                trace_failure(state.event, error);
                return {.interrupted = true};
            }
        } catch (const std::exception &error) {
            trace_failure(state.event, error.what());
            return {.interrupted = true};
        }
    }
    return tick(state, step);
}

bool SShaderModule::draw(State &state, const Draw<CHyprAnimationCanvas> &draw, Resources &) {
    if (!state.renderer || state.failed || draw.canvas->pass() == PassKind::Inert) return false;
    const bool live = draw.real_pass && !draw.canvas->captures_pixels() && !draw.canvas->blur_mask();
    try {
        std::string error;
        if (state.renderer->draw(draw, state.event == shader::Event::Close, error)) return true;
        if (live) trace_failure(state.event, error);
    } catch (const std::exception &error) {
        if (live) trace_failure(state.event, error.what());
    }
    // Mirror/readonly failures must not abort the authoritative live instance.
    if (live) state.failed = true;
    return false;
}

void SShaderModule::end(State &state, Resources &, End) noexcept {
    state.renderer.reset();
}
