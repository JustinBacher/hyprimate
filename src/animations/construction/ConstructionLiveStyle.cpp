#include "animations/construction/ConstructionStyle.hpp"

#include <chrono>
#include <cmath>
#include <new>

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
}  // namespace

SConstructionModule::Prepared SConstructionModule::prepare_settings(const Settings &settings) {
    return {settings.open.duration};
}

SConstructionModule::State SConstructionModule::start(const Resources &, std::uint64_t seed) {
    return {.site = nullptr, .seed = seed};
}

bool SConstructionModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (!is_usable(place.goal_body) || !is_usable(place.rest_capture) || !is_usable(place.work_area)) return false;
    try {
        state.site = std::make_unique<ConstructionSite>();
        state.seed = place.seed;
        state.site->build(place.goal_body, place.rest_capture, place.work_area, state.seed, resources.prepared.open_seconds);
    } catch (const std::bad_alloc &) {
        state.site.reset();
        return false;
    }
    if (!state.site->active()) return false;
    state.monitor = place.monitor;
    state.workspace = place.workspace;
    effects.snap_to_goal = true;
    trace_event("open", state.seed);
    effects.settle_native_open_alpha = true;
    return true;
}

Frame SConstructionModule::advance(State &state, const Step &step, Resources &, Effects &effects) {
    // Give layout back on the very first step after snap-to-goal, not at the end of the clip.
    effects.release_body = true;
    if (!state.site || step.dying || !step.open_enabled || step.stalled || !std::isfinite(step.elapsed) ||
        step.elapsed < 0 || step.elapsed > STALL_SECONDS || step.monitor != state.monitor ||
        step.workspace != state.workspace || !same_placement(step.body, state.site->body()) ||
        !same_placement(step.capture, state.site->capture())) return {.interrupted = true};
    state.site->step(step);
    if (state.site->batch().overflow) return {.interrupted = true};
    return {.running = state.site->active(), .bounds = state.site->bounds()};
}

CloseStart SConstructionModule::close(State &, const Place &, Resources &) {
    // The slice never associates with a retained image, even when closing interrupts the hoist.
    return CloseStart::Declined;
}
void SConstructionModule::end(State &state, Resources &, End) noexcept {
    if (state.site) {
        trace_event("end", state.seed);
        try {
            LOG(Log::DEBUG, "[hyprimate] construction resources seed={} peak_quads={} stride_bytes={} atlas_bytes={} work_width={} work_height={} work_fourcc={}",
                state.seed, state.peak_quads, sizeof(ConstructionQuad),
                state.work_width ? 4 * construction::ATLAS_WIDTH * construction::ATLAS_HEIGHT : 0,
                state.work_width, state.work_height, state.work_fourcc);
        } catch (const std::bad_alloc &) {}
    }
    state.site.reset();
}

bool SConstructionModule::moves(const Motion &, const Resources &) noexcept { return false; }

bool SConstructionModule::draw(State &state, const Draw<CHyprAnimationCanvas> &draw, Resources &resources) {
    if (!state.site || !draw.image || !*draw.image) return false;
    try {
        if (!resources.renderer) resources.renderer = std::make_shared<CConstructionRenderer>();
        const bool drawn = resources.renderer->draw(*state.site, draw);
        const auto &batch = state.site->batch();
        if (drawn) {
            const auto &layer = draw.canvas->layer();
            state.peak_quads = std::max(state.peak_quads, batch.count);
            if (draw.canvas_box.w * draw.canvas_box.h > state.work_width * state.work_height) {
                state.work_width = static_cast<std::uint32_t>(draw.canvas_box.w);
                state.work_height = static_cast<std::uint32_t>(draw.canvas_box.h);
                state.work_fourcc = layer->m_drmFormat;
            }
        }
        if (drawn && !state.drew_identity && batch.count == 1 && batch.quads[0].surface == ConstructionSurface::Client) {
            trace_event("identity", state.seed);
            state.drew_identity = true;
        }
        return drawn;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
