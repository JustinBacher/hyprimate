#include <variant>

#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "SmokeLiveState.hpp"
#include "SmokeRoute.hpp"

CBox live_extents(const SSmokeStyle &state, const CBox &box) {
    const auto bounds = state.smoke.bounds({box.w, box.h});
    return CBox{box.x + bounds.x, box.y + bounds.y, bounds.w, bounds.h}.expand(2.0);
}

Render::SWindowTransformBuffer render_live(SSmokeStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    [[maybe_unused]] const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SSmokeResources &resources) {
    if (!state.field) {
        state.field = makeShared<CSmokeFieldHandle>();
    }
    const auto layer = resources.renderer->render(ctx,
        SSmokeDraw{
            .window = input.framebuffer->getTexture(),
            .field = state.field,
            .frame = state.smoke.frame(
                {geometry.source.w / geometry.scale, geometry.source.h / geometry.scale}),
            .canvas = geometry.canvas.size(),
            .source = geometry.source,
            .texture_origin = {input.box.x, input.box.y},
            .texture_size = geometry.texture_size,
            .output_origin = {geometry.canvas.x, geometry.canvas.y},
        });
    if (!layer) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }
    return {.framebuffer = layer, .box = geometry.canvas};
}

bool SSmokeLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window,
    SSmokeResources &resources) {
    const auto current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const auto capture = window->getFullWindowBoundingBox();
    const hyprimate::Vec2 size{goal.w + capture.w - current.w, goal.h + capture.h - current.h};
    if (!hyprimate::is_usable(to_box(goal)) || !hyprimate::is_usable({0, 0, size.x, size.y})) {
        return false;
    }
    auto *transformer = runtime.acquire_transformer(window);
    auto &state = std::get<SSmokeStyle>(transformer->m_style);
    state.smoke.begin(hyprimate::Transition::Open,
        resources.prepared.open_duration,
        size,
        hyprimate::smoke_route(
            resources.last->open.direction, runtime.edge_input_for(window, true)));
    // Smoke owns geometry and opacity, but focus, workspace and user opacity remain
    // compositor-owned.
    g_pHyprRenderer->damageBox(capture);
    window->positionAnimation()->setValueAndWarp(goal.pos());
    window->sizeAnimation()->setValueAndWarp(goal.size());
    auto &fade = window->presentation().alpha(Desktop::View::WINDOW_ALPHA_FADE);
    fade->setValueAndWarp(fade->goal());
    CPluginRuntime::rebaseline(entry, window);
    return true;
}

void SSmokeLiveStyle::movement([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    [[maybe_unused]] double elapsed,
    [[maybe_unused]] bool enabled,
    bool moving,
    [[maybe_unused]] Resources &resources) {
    if (!moving) {
        return;
    }
    const auto &now = decision.current_capture;
    const auto &before = decision.previous_capture;
    std::get<SSmokeStyle>(transformer.m_style)
        .smoke.drive({now.x + now.w * 0.5 - before.x - before.w * 0.5,
                         now.y + now.h * 0.5 - before.y - before.h * 0.5},
            !decision.workspace_moving && window->positionAnimation()->isBeingAnimated(),
            {.x = now.w - before.w, .y = now.h - before.h});
}

void SSmokeLiveStyle::snapshot([[maybe_unused]] const CAnimationTransformer *transformer,
    hyprimate::WindowSnapshot &snapshot,
    [[maybe_unused]] const Resources &resources) {
    // Opening and movement share a material state, so neither blocks the other.
    snapshot.transition_busy = false;
}

void SSmokeLiveStyle::step([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled,
    Resources &resources) {
    auto &style = std::get<SSmokeStyle>(transformer.m_style);
    auto &smoke = style.smoke;
    if (!enabled) {
        smoke.stop_motion();
    }
    auto params = smoke.opening() ? resources.prepared.opening : resources.prepared.movement;
    if (!enabled) {
        params.strength = 0;
    }
    smoke.advance(elapsed, size, params);
    if (!smoke.active()) {
        style.field.reset();
    }
}

void SSmokeLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &style = std::get<SSmokeStyle>(transformer.m_style);
    style.smoke.reset();
    style.field.reset();
}

void SSmokeLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    std::get<SSmokeStyle>(transformer.m_style).smoke.stop_motion();
}

bool SSmokeLiveStyle::available([[maybe_unused]] const SSmokeStyle &state) noexcept {
    return true;
}

bool SSmokeLiveStyle::deforming(const SSmokeStyle &state) noexcept {
    return state.smoke.active();
}

bool SSmokeLiveStyle::builtFor([[maybe_unused]] const SSmokeStyle &state,
    [[maybe_unused]] const Resources &resources) noexcept {
    return true;
}

SSmokeStyle SSmokeLiveStyle::initial([[maybe_unused]] const Resources &resources, std::uint64_t seed) {
    SSmokeStyle state;
    state.smoke.seed(seed);
    return state;
}

double SSmokeLiveStyle::strength(const Resources &resources) {
    return resources.prepared.movement.strength;
}

bool SSmokeLiveStyle::usable([[maybe_unused]] const Resources &resources) {
    return true;
}

SSmokeLiveStyle::Prepared SSmokeLiveStyle::prepare_settings(const Settings &settings) {
    const auto &movement = settings.movement;
    const hyprimate::SmokeParams params{
        .strength = movement.strength,
        .turbulence = movement.turbulence,
        .trail = movement.trail,
        .reform = movement.reform,
        .settle_speed = movement.settleSpeed,
        .mass = movement.mass,
        .roundness = movement.roundness,
        .density = settings.shared.density,
        .variation = settings.shared.variation,
    };
    auto opening = params;
    opening.turbulence = settings.open.turbulence;
    auto closing = params;
    closing.turbulence = settings.close.turbulence;
    // Close previously used SmokeParams' default settle speed, not the movement setting.
    closing.settle_speed = hyprimate::SmokeParams{}.settle_speed;
    return {.movement = params,
        .opening = opening,
        .closing = closing,
        .open_duration = settings.open.duration,
        .close_duration = settings.close.duration};
}

void SSmokeLiveStyle::prepare(const Settings &settings, Resources &resources, bool selected) {
    resources.update(settings, prepare_settings);
    if (selected) resources.renderer->warm_up();
}
bool SSmokeLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated || decision.resized;
}
