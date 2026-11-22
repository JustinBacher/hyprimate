#include "animations/construction/ConstructionStyle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <new>
#include <utility>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "HyprAnimationCanvas.hpp"
#include "animations/construction/ConstructionArt.hpp"
#include "animations/construction/ConstructionRenderer.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
bool same_placement(Box a, Box b) noexcept {
    // rest_capture is reconstructed from animated insets; snapping can change its last ULP.
    constexpr double EPSILON = 1e-7;
    return std::abs(a.x - b.x) <= EPSILON && std::abs(a.y - b.y) <= EPSILON &&
        std::abs(a.w - b.w) <= EPSILON && std::abs(a.h - b.h) <= EPSILON;
}

// Ordinary compositor lifecycle logging. No pixel data, per-frame I/O, or private runtime switch.
void trace_event(const char *event, std::uint64_t seed) noexcept {
    try {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch());
        LOG(Log::DEBUG, "[hyprimate] construction event={} seed={} mono_ns={}", event, seed, ns.count());
    } catch (const std::bad_alloc &) {
        // Optional diagnostics must not prevent release during allocation pressure.
    }
}

using MotionSite = construction::MotionSite;

void trace_resources(SConstructionStyle &state) noexcept {
    try {
        LOG(Log::DEBUG, "[hyprimate] construction resources seed={} peak_quads={} stride_bytes={} atlas_bytes={} work_width={} work_height={} work_fourcc={}",
            state.seed, state.peak_quads, sizeof(ConstructionQuad),
            state.work_width ? 4 * construction::ATLAS_WIDTH * construction::ATLAS_HEIGHT : 0,
            state.work_width, state.work_height, state.work_fourcc);
    } catch (const std::bad_alloc &) {}
    state.peak_quads = 0;
    state.work_width = state.work_height = state.work_fourcc = 0;
}

void trace_motion_end(SConstructionStyle &state, MotionSite::Kind kind) noexcept {
    if (kind == MotionSite::Kind::None) return;
    trace_event(kind == MotionSite::Kind::Carry ? "carry-end" : "resize-end", state.seed);
    trace_resources(state);
}

Frame advance_motion(SConstructionStyle &state, const Step &step) {
    if (!state.motion) {
        if (!step.movement_enabled || !step.motion || !MotionSite::admits(*step.motion)) return {};
        try {
            state.motion = std::make_unique<MotionSite>(state.seed);
        } catch (const std::bad_alloc &) {
            return {.interrupted = true};
        }
    }
    const auto previous = state.motion->kind();
    const auto frame = state.motion->advance(step);
    const auto current = state.motion->kind();
    if (current != previous) {
        trace_motion_end(state, previous);
        if (frame.running) {
            trace_event(current == MotionSite::Kind::Carry ? "carry-start" : "resize-start", state.seed);
        } else {
            // The replacement job never presented, so it has no corresponding terminal trace.
            state.motion.reset();
        }
    }
    return frame;
}
}  // namespace

SConstructionModule::Prepared SConstructionModule::prepare_settings(const Settings &settings) {
    return {.open_seconds = settings.open.duration,
        .crew_motion = settings.movement.variant == config::Variant::CrewCarry};
}

SConstructionModule::State SConstructionModule::start(const Resources &, std::uint64_t seed) {
    return {.site = nullptr, .motion = nullptr, .seed = seed};
}

bool SConstructionModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (!is_usable(place.goal_body) || !is_usable(place.rest_capture) || !is_usable(place.work_area)) return false;
    std::unique_ptr<ConstructionSite> opening;
    try {
        opening = std::make_unique<ConstructionSite>();
        opening->build(place.goal_body, place.rest_capture, place.work_area, place.seed, resources.prepared.open_seconds);
    } catch (const std::bad_alloc &) {
        return false;
    }
    if (!opening->active()) return false;
    if (state.motion) {
        trace_motion_end(state, state.motion->kind());
        state.motion.reset();
    }
    state.site = std::move(opening);
    state.seed = place.seed;
    state.native_seated = false;
    state.monitor = place.monitor;
    state.workspace = place.workspace;
    effects.snap_to_goal = true;
    trace_event("open", state.seed);
    effects.settle_native_open_alpha = true;
    return true;
}

Frame SConstructionModule::advance(State &state, const Step &step, Resources &resources, Effects &effects) {
    if (!state.site) {
        if (!resources.prepared.crew_motion) return {.interrupted = true};
        return advance_motion(state, step);
    }
    // Opening has priority: the framework's rising-open tick may still carry a motion
    // observation taken before the existing snap-to-goal and observation reread.
    // Give layout back on the very first step after snap-to-goal, not at the end of the clip.
    effects.release_body = true;
    if (step.dying || !step.open_enabled || step.stalled || !std::isfinite(step.elapsed) ||
        step.elapsed < 0 || step.elapsed > STALL_SECONDS || step.monitor != state.monitor ||
        step.workspace != state.workspace || !same_placement(step.body, state.site->body()) ||
        !same_placement(step.capture, state.site->capture())) return {.interrupted = true};
    state.site->step(step);
    if (state.site->batch().overflow) return {.interrupted = true};
    // An identity result from draw() would still be flattened by the host. Keep the
    // seated interval active, but request native presentation before pass assembly.
    const bool native = state.site->active() &&
        construction::is_capture_identity(state.site->batch(), state.site->capture());
    if (native && !state.native_seated) {
        trace_event("native-seated", state.seed);
        state.native_seated = true;
    }
    return {.running = state.site->active(), .bounds = state.site->bounds(), .native_presentation = native};
}

CloseStart SConstructionModule::close(State &, const Place &, Resources &) {
    // The slice never associates with a retained image, even when closing interrupts the hoist.
    return CloseStart::Declined;
}
void SConstructionModule::end(State &state, Resources &, End) noexcept {
    if (state.site) {
        trace_event("end", state.seed);
        trace_resources(state);
    }
    if (state.motion) trace_motion_end(state, state.motion->kind());
    state.motion.reset();
    state.site.reset();
}

bool SConstructionModule::moves(const Motion &motion, const Resources &resources) noexcept {
    return resources.prepared.crew_motion && MotionSite::admits(motion);
}

bool SConstructionModule::draw(State &state, const Draw<CHyprAnimationCanvas> &draw, Resources &resources) {
    if ((!state.site && !state.motion) || !draw.canvas || !draw.image || !*draw.image) return false;
    if (!state.site && !resources.prepared.crew_motion) return false;
    const auto &batch = state.site ? state.site->batch() : state.motion->batch();
    const Box body = state.site ? state.site->body() : state.motion->body();
    const Box capture = state.site ? state.site->capture() : state.motion->capture();
    try {
        if (!resources.renderer) resources.renderer = std::make_shared<CConstructionRenderer>();
        const std::size_t quads = batch.count;
        const bool drawn = resources.renderer->draw(batch, body, capture, draw);
        if (drawn) {
            const auto &layer = draw.canvas->layer();
            state.peak_quads = std::max(state.peak_quads, quads);
            if (draw.canvas_box.w * draw.canvas_box.h > state.work_width * state.work_height) {
                state.work_width = static_cast<std::uint32_t>(draw.canvas_box.w);
                state.work_height = static_cast<std::uint32_t>(draw.canvas_box.h);
                state.work_fourcc = layer->m_drmFormat;
            }
        }
        return drawn;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
