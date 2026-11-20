#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "animations/paper/PaperStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
    PaperParams material(const config::SPaperTransition &settings) {
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
        return {.duration = settings.duration,
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
            .simulation_quality = QUALITY.at(std::to_underlying(settings.simulationQuality))};
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
        double left = std::numeric_limits<double>::infinity(), top = left, right = -left,
               bottom = -left;
        double nearest = 3, lateral = 0;
        for (const auto &point : surface.points) {
            nearest = std::min(nearest, 3.0 - point.position.z);
            lateral = std::max({lateral, std::abs(point.position.x), std::abs(point.position.y)});
            const double SCALE = 3. / std::max(.25, 3. - point.position.z);
            const double X = capture.w * (.5 + point.position.x * SCALE * LENGTH / surface.size.x);
            const double Y = capture.h * (.5 + point.position.y * SCALE * LENGTH / surface.size.y);
            left = std::min(left, X);
            right = std::max(right, X);
            top = std::min(top, Y);
            bottom = std::max(bottom, Y);
        }
        // Include both lateral displacement and the perspective change from a curved
        // edge moving toward the eye. Use the same displacement limit as reconstruction.
        nearest = std::max(nearest - PAPER_PATCH_BULGE, .25);
        const double PAD =
            2.0 + PAPER_PATCH_BULGE * (3 / nearest) * (1 + lateral / nearest) * LENGTH *
                      std::max(capture.w / surface.size.x, capture.h / surface.size.y);
        return {.x = left - PAD,
            .y = top - PAD,
            .w = right - left + 2 * PAD,
            .h = bottom - top + 2 * PAD};
    }
}  // namespace

SPaperModule::Prepared SPaperModule::prepare_settings(const Settings &settings) {
    return {.open = material(settings.open), .close = material(settings.close)};
}

SPaperModule::State SPaperModule::start(const Resources &, std::uint64_t) {
    return {};
}

bool SPaperModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (!is_usable(place.rest_capture) || !is_usable(place.goal_body)) {
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
    effects.snap_to_goal = true;
    effects.settle_native_open_alpha = true;
    return true;
}

Frame SPaperModule::advance(State &state, const Step &step, Resources &resources, Effects &) {
    if (!step.dying && !step.open_enabled) {
        return {.interrupted = true};
    }
    const auto STATUS = resources.workers.present(state.run, step.elapsed, state.display);
    if (STATUS != PaperPlayback::Running) {
        return {.interrupted = STATUS == PaperPlayback::Interrupted};
    }
    return {.running = true, .bounds = bounds(state.display, step.capture)};
}

CloseStart SPaperModule::close(State &state, const Place &place, Resources &resources) {
    if (!is_usable(place.capture)) {
        return CloseStart::Declined;
    }
    if (state.run && (state.display.faces.empty() || state.display.opacity <= 0)) {
        // A map that never became visible has nothing to fold. Suppress its native
        // snapshot without flashing a flat sheet or loading another solver.
        resources.workers.cancel(state.run);
        return CloseStart::Owned;
    }
    const Vec2 SIZE{place.capture.w, place.capture.h};
    if (!state.run) {
        flat_capture(state.display, SIZE);
    }
    return resources.workers.begin(state.run,
               Transition::Close,
               resources.prepared.close,
               place.seed,
               SIZE,
               cursor_uv(place, place.capture))
               ? CloseStart::Owned
               : CloseStart::Declined;
}

void SPaperModule::end(State &state, Resources &resources, End) noexcept {
    resources.workers.cancel(state.run);
}
