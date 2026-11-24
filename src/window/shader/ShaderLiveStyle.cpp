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
    constexpr std::size_t OPEN = 0, CLOSE = 1;

    void trace_failure(bool closing, std::string_view reason) noexcept {
        try {
            LOG(Log::WARN, "[hyprimate] shader {} using native fallback: {}",
                closing ? "close" : "open", reason);
        } catch (const std::bad_alloc &) {}
    }

    bool begin(SShaderStyle &state, const Place &place, const SShaderModule::Selection &settings,
        bool closing) try {
        if (!settings.program || settings.duration <= 0 || !is_usable(place.capture)) return false;
        auto renderer = std::make_unique<shader::WindowRenderer>(settings.program);
        std::string error;
        if (!renderer->resize(place.capture, place.scale, error)) {
            trace_failure(closing, error);
            return false;
        }
        state = SShaderStyle{.renderer = std::move(renderer), .seed = place.seed,
            .duration = settings.duration, .closing = closing};
        if (place.near.cursor) {
            state.pointer_x = static_cast<float>((place.near.cursor->x - place.capture.x) / place.capture.w);
            state.pointer_y = static_cast<float>((place.near.cursor->y - place.capture.y) / place.capture.h);
            state.pointer_valid = std::isfinite(state.pointer_x) && std::isfinite(state.pointer_y);
            if (!state.pointer_valid) state.pointer_x = state.pointer_y = 0.5F;
        }
        if (!closing) {
            try {
                LOG(Log::DEBUG, "[hyprimate] shader open source copies=0 copy_bytes=0");
            } catch (const std::bad_alloc &) {}
        }
        return true;
    } catch (const std::exception &error) {
        trace_failure(closing, error.what());
        return false;
    }

    hyprimate::animation::Frame tick(SShaderStyle &state, const Step &step) noexcept {
        const auto previous = state.elapsed;
        state.elapsed = static_cast<float>(std::min<double>(state.duration, previous + step.elapsed));
        state.renderer->advance(shader::Frame{.tick = ++state.tick, .seed = state.seed,
            .event = state.closing ? shader::Event::Close : shader::Event::Open,
            .progress = state.elapsed / state.duration, .elapsed = state.elapsed,
            .delta = state.elapsed - previous, .duration = state.duration,
            .width = static_cast<float>(step.capture.w), .height = static_cast<float>(step.capture.h),
            .scale = static_cast<float>(step.scale), .pointer_x = state.pointer_x,
            .pointer_y = state.pointer_y, .pointer_valid = state.pointer_valid,
            .fullscreen = step.fullscreen});
        return {.running = true, .bounds = state.renderer->bounds({0, 0, step.capture.w, step.capture.h})};
    }
}

std::array<std::string, 2> SShaderModule::refresh(Resources &resources,
    std::array<Request, 2> requests, bool force) {
    auto next = resources.prepared;
    std::array<std::string, 2> errors;
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
                    candidate = shader::prepare_window_program(i == OPEN ? shader::Event::Open : shader::Event::Close,
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
    return begin(state, place, resources.prepared[OPEN], false);
}

CloseStart SShaderModule::close(State &state, const Place &place, Resources &resources) {
    return begin(state, place, resources.prepared[CLOSE], true) ? CloseStart::Owned : CloseStart::Declined;
}

bool SShaderModule::bind_close_source(State &state, const Box &source,
    const CHyprAnimationCanvas::Image &image) noexcept {
    try {
        std::string error;
        if (state.renderer && state.renderer->bind_close_source(source, image, error)) return true;
        trace_failure(true, error);
    } catch (const std::exception &error) {
        trace_failure(true, error.what());
    }
    return false;
}

hyprimate::animation::Frame SShaderModule::initial_close_frame(State &state, const Step &step,
    Resources &) noexcept {
    return state.renderer ? tick(state, step) : hyprimate::animation::Frame{.interrupted = true};
}

hyprimate::animation::Frame SShaderModule::advance(State &state, const Step &step, Resources &,
    Effects &) {
    if (!state.renderer || state.failed || (!state.closing && !step.open_enabled)) {
        return {.interrupted = true};
    }
    if (state.elapsed >= state.duration) return {};
    if (!state.closing) {
        try {
            std::string error;
            if (!state.renderer->resize(step.capture, step.scale, error)) {
                trace_failure(false, error);
                return {.interrupted = true};
            }
        } catch (const std::exception &error) {
            trace_failure(false, error.what());
            return {.interrupted = true};
        }
    }
    return tick(state, step);
}

bool SShaderModule::draw(State &state, const Draw<CHyprAnimationCanvas> &draw, Resources &) {
    if (!state.renderer || state.failed || draw.canvas->pass() == PassKind::Inert ||
        draw.canvas->blur_mask()) return false;
    const bool live = draw.real_pass && !draw.canvas->captures_pixels();
    try {
        std::string error;
        if (state.renderer->draw(draw, state.closing, error)) return true;
        if (live) trace_failure(state.closing, error);
    } catch (const std::exception &error) {
        if (live) trace_failure(state.closing, error.what());
    }
    // Mirror/readonly failures must not abort the authoritative live instance.
    if (live) state.failed = true;
    return false;
}

void SShaderModule::end(State &state, Resources &, End) noexcept {
    state.renderer.reset();
}
