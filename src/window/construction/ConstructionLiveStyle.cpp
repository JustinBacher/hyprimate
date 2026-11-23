#include "window/construction/ConstructionStyle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <new>
#include <utility>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/construction/ConstructionArt.hpp"
#include "window/construction/ConstructionRenderer.hpp"

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
    switch (kind) {
        case MotionSite::Kind::Carry: trace_event("carry-end", state.seed); break;
        case MotionSite::Kind::Resize: trace_event("resize-end", state.seed); break;
        case MotionSite::Kind::Crane: trace_event("crane-end", state.seed); break;
        case MotionSite::Kind::Pieces: trace_event("pieces-end", state.seed); break;
        case MotionSite::Kind::None: return;
    }
    trace_resources(state);
}

Frame advance_motion(SConstructionStyle &state, const Step &step, construction::TransportVariant transport,
    double worker_size) {
    if (!state.motion) {
        if (!step.movement_enabled || !step.motion || !MotionSite::admits(*step.motion)) return {};
        try {
            state.motion = std::make_unique<MotionSite>(state.seed, transport, worker_size);
        } catch (const std::bad_alloc &) {
            return {.interrupted = true};
        }
    }
    const auto previous = state.motion->kind();
    const auto frame = state.motion->advance(step);
    state.native_seated = frame.native_presentation;
    state.monitor = step.monitor;
    state.workspace = step.workspace;
    const auto current = state.motion->kind();
    if (current != previous) {
        trace_motion_end(state, previous);
        if (frame.running) {
            switch (current) {
                case MotionSite::Kind::Carry: trace_event("carry-start", state.seed); break;
                case MotionSite::Kind::Resize: trace_event("resize-start", state.seed); break;
                case MotionSite::Kind::Crane: trace_event("crane-start", state.seed); break;
                case MotionSite::Kind::Pieces: trace_event("pieces-start", state.seed); break;
                case MotionSite::Kind::None: break;
            }
        } else {
            // The replacement job never presented, so it has no corresponding terminal trace.
            state.motion.reset();
        }
    }
    return frame;
}
}  // namespace

SConstructionModule::Prepared SConstructionModule::prepare_settings(const Settings &settings) {
    using Mode = construction::TransportVariant;
    Mode mode = Mode::Native;
    switch (settings.movement.variant) {
        case config::Variant::Native: break;
        case config::Variant::CrewCarry: mode = Mode::CrewCarry; break;
        case config::Variant::Crane: mode = Mode::Crane; break;
        case config::Variant::PieceByPiece: mode = Mode::PieceByPiece; break;
    }
    return {.open_seconds = settings.open.duration / 1000.0, .close_seconds = settings.close.duration / 1000.0,
        .worker_size = settings.shared.workerSize, .transport = mode};
}

SConstructionModule::State SConstructionModule::start(const Resources &, std::uint64_t seed) {
    return {.seed = seed};
}

bool SConstructionModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (resources.prepared.open_seconds <= 0 || !is_usable(place.goal_body) || !is_usable(place.rest_capture) || !is_usable(place.work_area)) return false;
    std::unique_ptr<ConstructionSite> opening;
    try {
        opening = std::make_unique<ConstructionSite>();
        opening->build(place.goal_body, place.rest_capture, place.work_area, place.seed,
            resources.prepared.open_seconds, resources.prepared.worker_size);
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
    if (state.close_site) return state.close_site->advance(step);
    state.drawn_art_current = false;
    if (!state.site) {
        if (resources.prepared.transport == construction::TransportVariant::Native) return {.interrupted = true};
        return advance_motion(state, step, resources.prepared.transport, resources.prepared.worker_size);
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

void SConstructionModule::invalidate_mapped_publication(State &state, Resources &) noexcept {
    state.publication.known = false;
    state.drawn_art_current = false;
}

void SConstructionModule::mapped_native_publication(State &state, Box body, Box capture,
    Box source_device, double scale, Resources &) noexcept {
    state.publication.body = body;
    state.publication.capture = capture;
    state.publication.known = true;
    state.drawn_native = true;
    state.drawn_source_device = source_device;
    state.drawn_scale = scale;
}

bool SConstructionModule::accepts_close_crop(const State &state, Box crop, Box source_device,
    double scale) noexcept {
    if (!state.close_site || (state.publication.known &&
        (!same_placement(source_device, state.drawn_source_device) || scale != state.drawn_scale))) return false;
    return state.close_site->can_capture(crop, scale);
}

CloseStart SConstructionModule::close(State &state, const Place &place, Resources &resources) {
    if (resources.prepared.close_seconds <= 0) return CloseStart::Declined;
    std::optional<std::span<const ConstructionQuad>> visible;
    if (state.site || state.motion) {
        const auto &last = state.publication;
        if (!last.known || state.monitor != place.monitor || state.workspace != place.workspace ||
            !same_placement(last.body, place.body) || !same_placement(last.capture, place.capture))
            return CloseStart::Declined;
        if (!state.drawn_native) {
            // No crane in closing. Do not replace an attached rig or an unpresented
            // newer work crew with an unrelated set of airborne workers.
            if (!state.drawn_art_current || (state.motion && state.motion->kind() == MotionSite::Kind::Crane))
                return CloseStart::Declined;
            visible = std::span(last.faces).first(last.count);
        }
    }
    try {
        auto closing = std::make_unique<construction::CloseSite>();
        if (!closing->start(place.body, place.capture, visible, place.seed, resources.prepared.close_seconds,
                resources.prepared.worker_size))
            return CloseStart::Declined;
        if (visible && !closing->depart(state.site ? state.site->batch() : state.motion->batch()))
            return CloseStart::Declined;
        if (state.motion) trace_motion_end(state, state.motion->kind());
        if (state.site) {
            trace_event("end", state.seed);
            trace_resources(state);
        }
        state.motion.reset();
        state.site.reset();
        state.seed = place.seed;
        state.close_site = std::move(closing);
        return CloseStart::Owned;
    } catch (const std::bad_alloc &) {
        return CloseStart::Declined;
    }
}

Frame SConstructionModule::initial_close_frame(State &state, const Step &step, Resources &) noexcept {
    if (!state.close_site) return {.interrupted = true};
    state.close_started = true;
    trace_event("regional-close-start", state.seed);
    return state.close_site->advance(step);
}
void SConstructionModule::end(State &state, Resources &, End) noexcept {
    if (state.site) {
        trace_event("end", state.seed);
        trace_resources(state);
    }
    if (state.motion) trace_motion_end(state, state.motion->kind());
    if (state.close_started) trace_event("regional-close-end", state.seed);
    state.close_site.reset();
    state.motion.reset();
    state.site.reset();
}

bool SConstructionModule::moves(const Motion &motion, const Resources &resources) noexcept {
    return resources.prepared.transport != construction::TransportVariant::Native && MotionSite::admits(motion);
}

bool SConstructionModule::draw(State &state, const Draw<CHyprAnimationCanvas> &draw, Resources &resources) {
    if (!draw.canvas || !draw.image || !*draw.image) return false;
    if (state.close_site) {
        try {
            if (!resources.renderer) resources.renderer = std::make_shared<CConstructionRenderer>();
            return resources.renderer->draw(*state.close_site, draw);
        } catch (const std::bad_alloc &) {
            return false;
        }
    }
    if (!state.site && !state.motion) return false;
    if (!state.site && resources.prepared.transport == construction::TransportVariant::Native) return false;
    const auto &batch = state.site ? state.site->batch() : state.motion->batch();
    const Box body = state.site ? state.site->body() : state.motion->body();
    const Box capture = state.site ? state.site->capture() : state.motion->capture();
    try {
        if (!resources.renderer) resources.renderer = std::make_shared<CConstructionRenderer>();
        const std::size_t quads = batch.count;
        const bool drawn = resources.renderer->draw(batch, body, capture, draw);
        if (drawn) {
            if (draw.real_pass && !draw.canvas->captures_pixels() && draw.monitor == state.monitor) {
                const double x = draw.image_origin.x - draw.source.x;
                const double y = draw.image_origin.y - draw.source.y;
                const double left = std::max(0.0, x), top = std::max(0.0, y);
                state.publication.remember(batch, body, capture, {left / draw.scale, top / draw.scale,
                    (std::min(draw.source.w, x + draw.image_size.x) - left) / draw.scale,
                    (std::min(draw.source.h, y + draw.image_size.y) - top) / draw.scale});
                state.drawn_native = false;
                state.drawn_art_current = true;
                state.drawn_source_device = draw.source;
                state.drawn_scale = draw.scale;
            }
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
