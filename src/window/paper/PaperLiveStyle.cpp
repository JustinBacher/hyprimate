#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "window/paper/PaperStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
    PaperParams material(const config::SPaperTransition &settings, double inertia) {
        constexpr std::array ORIGINS{PaperOrigin::Center,
            PaperOrigin::Random,
            PaperOrigin::Cursor,
            PaperOrigin::Left,
            PaperOrigin::Right,
            PaperOrigin::Top,
            PaperOrigin::Bottom};
        constexpr std::array FORCES{
            PaperForce::Inward, PaperForce::Directional, PaperForce::Rotational};
        constexpr std::array QUALITY{
            PaperQuality::Adaptive, PaperQuality::Low, PaperQuality::Medium, PaperQuality::High};
        return {.duration = settings.duration / 1000.0,
            .crumpliness = settings.crumpliness,
            .shading = settings.shading,
            .variation = settings.variation,
            .fold_density = settings.foldDensity,
            .paper_stiffness = settings.paperStiffness,
            .crumple_size = settings.crumpleSize,
            .rotation_intensity = settings.rotationIntensity,
            .unfold_tension = settings.unfoldTension,
            .settling = settings.settling,
            .crease_persistence = settings.creasePersistence,
            .crumple_origin = ORIGINS.at(std::to_underlying(settings.crumpleOrigin)),
            .force_direction = FORCES.at(std::to_underlying(settings.forceDirection)),
            .simulation_quality = QUALITY.at(std::to_underlying(settings.simulationQuality)),
            .inertia = inertia};
    }

    Vec2 cursor_uv(const Place &place, Box capture) {
        if (!place.near.cursor) {
            return {.x = 0.5, .y = 0.5};
        }
        return {.x = std::clamp((place.near.cursor->x - capture.x) / capture.w, 0., 1.),
            .y = std::clamp((place.near.cursor->y - capture.y) / capture.h, 0., 1.)};
    }

    void flat_capture(PaperSurface &surface, Vec2 size) {
        const double LENGTH = std::sqrt(size.x * size.y);
        const double X = size.x * .5 / LENGTH, Y = size.y * .5 / LENGTH;
        surface.size = size;
        surface.points = {
            {{-X, -Y, 0}, {0, 0}}, {{X, -Y, 0}, {1, 0}}, {{X, Y, 0}, {1, 1}}, {{-X, Y, 0}, {0, 1}}};
        surface.faces = {{{0, 1, 2}, {}}, {{0, 2, 3}, {}}};
        surface.shading = 0;
        surface.opacity = 1;
    }

    Box bounds(const PaperSurface &surface, Box capture) {
        // A map with no published pose still needs a scheduled frame on this monitor.
        if (surface.points.empty()) {
            return {.x = capture.w * .5, .y = capture.h * .5, .w = 1, .h = 1};
        }
        const double LENGTH = std::sqrt(surface.size.x * surface.size.y);
        const double UNIT = std::sqrt(capture.w * capture.h);
        const Vec2 axes{LENGTH * capture.w / surface.size.x, LENGTH * capture.h / surface.size.y};
        const Vec2 center{.5 * capture.w, .5 * capture.h};
        double left = std::numeric_limits<double>::infinity(), top = left, right = -left,
               bottom = -left;
        double nearest = 3, lateral = 0;
        for (const auto &point : surface.points) {
            nearest = std::min(nearest, 3.0 - point.position.z);
            lateral = std::max({lateral, std::abs(point.position.x), std::abs(point.position.y)});
            const double SCALE = 3. / std::max(.25, 3. - point.position.z);
            const double dx = point.position.x * axes.x, dy = point.position.y * axes.y;
            const double X = center.x + dx * SCALE, Y = center.y + dy * SCALE;
            left = std::min(left, X);
            right = std::max(right, X);
            top = std::min(top, Y);
            bottom = std::max(bottom, Y);
            if (surface.shadow_strength > 0) {
                // The material projects the shadow onto the desktop, not through the camera.
                const double height = UNIT * std::max(0., point.position.z);
                const double shadow_x = center.x + dx + .32 * height;
                const double shadow_y = center.y + dy + .42 * height;
                left = std::min(left, shadow_x);
                right = std::max(right, shadow_x);
                top = std::min(top, shadow_y);
                bottom = std::max(bottom, shadow_y);
            }
        }
        // Include both lateral displacement and the perspective change from a curved
        // edge moving toward the eye. Use the same displacement limit as reconstruction.
        nearest = std::max(nearest - PAPER_PATCH_BULGE, .25);
        const double PATCH = surface.interaction_mesh ? 0. : PAPER_PATCH_BULGE;
        const double PAD =
            2.0 + PATCH * ((3 / nearest) * (1 + lateral / nearest) * std::max(axes.x, axes.y) +
                              (surface.shadow_strength > 0 ? .42 * UNIT : 0.));
        return {.x = left - PAD,
            .y = top - PAD,
            .w = right - left + 2 * PAD,
            .h = bottom - top + 2 * PAD};
    }
    Box running_bounds(SPaperStyle &state, Box capture) {
        const auto &surface = state.surface();
        const auto *const POINTS = surface.points.data();
        if (!state.running_bounds || state.running_bounds_points != POINTS ||
            state.bounds_capture.x != capture.w || state.bounds_capture.y != capture.h) {
            state.running_bounds = bounds(surface, capture);
            state.running_bounds_points = POINTS;
            state.bounds_capture = {capture.w, capture.h};
        }
        return *state.running_bounds;
    }

    void invalidate_bounds(SPaperStyle &state) noexcept {
        state.running_bounds.reset();
        state.running_bounds_points = nullptr;
    }
}  // namespace

SPaperModule::Prepared SPaperModule::prepare_settings(const Settings &settings) {
    const auto &shared = settings.shared;
    const auto &move = settings.move;
    const auto &resize = settings.resize;
    return {.open = material(settings.open, shared.inertia),
        .close = material(settings.close, shared.inertia),
        .interaction = {.stiffness = shared.stiffness,
            .inertia = shared.inertia,
            .settle_damping = shared.settleDamping,
            .style = move.style == config::PaperMoveStyle::Glide ? PaperMoveStyle::Glide
                                                                 : PaperMoveStyle::Flutter,
            .flutter = {.air_resistance = move.flutter.airResistance,
                .intensity = move.flutter.intensity,
                .frequency = move.flutter.frequency,
                .lift = move.flutter.lift,
                .torsion = move.flutter.torsion},
            .glide = {.friction = move.glide.friction,
                .corner_lift = move.glide.cornerLift,
                .edge_curl = move.glide.edgeCurl,
                .shadow_separation = move.glide.shadowSeparation},
            .resize = {.fold_intensity = resize.foldIntensity,
                .curl_intensity = resize.curlIntensity,
                .crease_density = resize.creaseDensity}},
        .shading = static_cast<float>(shared.shading)};
}

SPaperModule::State SPaperModule::start(const SimulationResources &, std::uint64_t) {
    return {};
}

bool SPaperModule::open(
    State &state, const Place &place, SimulationResources &resources, Effects &effects) {
    if (resources.prepared.open.duration <= 0 || !is_usable(place.rest_capture) ||
        !is_usable(place.goal_body)) {
        return false;
    }
    if (!resources.workers.begin(state.run,
            Transition::Open,
            resources.prepared.open,
            place.seed,
            {place.rest_capture.w, place.rest_capture.h},
            cursor_uv(place, place.rest_capture))) {
        return false;
    }
    invalidate_bounds(state);
    effects.snap_to_goal = true;
    effects.settle_native_open_alpha = true;
    return true;
}

Frame SPaperModule::advance(
    State &state, const Step &step, SimulationResources &resources, Effects &) {
    if (state.run) {
        if (!step.dying && !step.open_enabled) {
            invalidate_bounds(state);
            return {.interrupted = true};
        }
        // The worker publishes completed frames only by swapping its distinct retained
        // writing/pending buffers into display. present unlocks before returning, so the
        // worker may reuse and reallocate the old display buffer; keep its address as an
        // integer snapshot. Non-empty live buffers cannot alias, even when storage recurs.
        // Empty vectors may share an address; their bounds use the same fallback.
        const auto PREVIOUS_POINTS = reinterpret_cast<std::uintptr_t>(state.display.points.data());
        const auto STATUS = resources.workers.present(state.run, step.elapsed, state.display);
        const bool DISPLAY_CHANGED =
            PREVIOUS_POINTS != reinterpret_cast<std::uintptr_t>(state.display.points.data());
        if (DISPLAY_CHANGED) {
            invalidate_bounds(state);
        }
        // Only a newly presented worker frame can replace the movement handoff.
        // An interrupted present leaves display untouched, even if it has faces.
        if (state.closing_from && DISPLAY_CHANGED && !state.display.faces.empty()) {
            state.closing_from.reset();
            invalidate_bounds(state);
        }
        if (STATUS == PaperPlayback::Running) {
            return {.running = true, .bounds = running_bounds(state, step.capture)};
        }
        if (STATUS == PaperPlayback::Interrupted || step.dying) {
            invalidate_bounds(state);
            return {.interrupted = STATUS == PaperPlayback::Interrupted};
        }
        // Opening owns its mesh until it has unfolded. A continuing drag can take
        // ownership on the same frame; it never samples a partially packed mesh.
        resources.workers.cancel(state.run);
        invalidate_bounds(state);
        if (!step.motion || !moves(*step.motion, resources)) {
            return {};
        }
    }
    invalidate_bounds(state);
    if (step.dying || !step.movement_enabled ||
        !state.interaction.advance(state.display, resources.prepared.interaction, step)) {
        return {};
    }
    state.display.shading = resources.prepared.shading;
    return {.running = true, .bounds = bounds(state.display, step.capture)};
}

CloseStart SPaperModule::close(State &state, const Place &place, SimulationResources &resources) {
    if (resources.prepared.close.duration <= 0 || !is_usable(place.capture)) {
        return CloseStart::Declined;
    }
    if (state.run && (state.display.faces.empty() || state.display.opacity <= 0)) {
        // A map that never became visible has nothing to fold. Suppress its native
        // snapshot without flashing a flat sheet or loading another solver.
        invalidate_bounds(state);
        resources.workers.cancel(state.run);
        return CloseStart::Owned;
    }
    const Vec2 SIZE{place.capture.w, place.capture.h};
    if (!state.run && state.display.interaction_mesh) {
        invalidate_bounds(state);
        state.closing_from = std::make_shared<PaperSurface>(std::move(state.display));
    } else if (!state.run) {
        flat_capture(state.display, SIZE);
        invalidate_bounds(state);
    }
    const bool STARTED = resources.workers.begin(state.run,
        Transition::Close,
        resources.prepared.close,
        place.seed,
        SIZE,
        cursor_uv(place, place.capture),
        state.closing_from);
    if (STARTED) {
        invalidate_bounds(state);
    }
    return STARTED ? CloseStart::Owned : CloseStart::Declined;
}

void SPaperModule::end(State &state, SimulationResources &resources, End) noexcept {
    resources.workers.cancel(state.run);
    invalidate_bounds(state);
}
