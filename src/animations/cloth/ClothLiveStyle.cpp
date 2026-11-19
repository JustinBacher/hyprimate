#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/math/Box.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/cloth/ClothLiveState.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"
#include "animations/shared/LiveSheetRender.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "config_generated.hpp"

CBox live_extents(const SClothStyle &state, const CBox &box) {
    return live_sheet_extents(state.sheet, box);
}

Render::SWindowTransformBuffer render_live(SClothStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    [[maybe_unused]] const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SClothResources &resources) {
    return render_live_sheet(
        state.sheet, state.cloth.opacity(), state.cloth.shaded(), ctx, input, geometry, resources.shade);
}

bool SClothLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window,
    SClothResources &resources) {
    if (const auto FRAME = hyprimate::resolve_style_open_edge(
            runtime.edge_input_for(window, true), resources.last->open)) {
        entry.open_edge = FRAME->edge;
    }
    const CBox CURRENT = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox GOAL = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    const CBox REST{GOAL.x + (CAPTURE.x - CURRENT.x),
        GOAL.y + (CAPTURE.y - CURRENT.y),
        GOAL.w + (CAPTURE.w - CURRENT.w),
        GOAL.h + (CAPTURE.h - CURRENT.h)};
    const auto USABLE = [](const CBox &box) {
        return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
               std::isfinite(box.h) && box.w > 0.0 && box.h > 0.0;
    };
    if (!USABLE(GOAL) || !USABLE(REST)) {
        return false;
    }
    runtime.m_neighbours.clear();
    runtime.gather_contact_obstacles(window);
    auto *transformer = runtime.acquire_transformer(window);
    const auto &cloth = resources.prepared.open;
    auto &state = std::get<SClothStyle>(transformer->m_style);
    state.cloth.reset();
    state.cloth.begin(hyprimate::Transition::Open,
        cloth,
        runtime.m_next_seed++,
        to_box(REST),
        runtime.m_neighbours,
        std::nullopt);
    state.sheet.capture_size = {.x = REST.w, .y = REST.h};
    state.cloth.frame(state.sheet.capture_size, *state.sheet.frame);
    if (state.cloth.active()) {
        g_pHyprRenderer->damageBox(CAPTURE);
        window->positionAnimation()->setValueAndWarp(GOAL.pos());
        window->sizeAnimation()->setValueAndWarp(GOAL.size());
        CPluginRuntime::rebaseline(entry, window);
    }
    return true;
}

void SClothLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    [[maybe_unused]] const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double elapsed,
    bool enabled,
    bool moving,
    [[maybe_unused]] Resources &resources) {
    auto &state = std::get<SClothStyle>(transformer.m_style);
    if (moving && !state.cloth.moving()) {
        state.cloth.begin_movement(
            state.movement, runtime.m_next_seed++, decision.previous_capture);
        state.sheet.capture_size = {
            .x = decision.previous_capture.w,
            .y = decision.previous_capture.h,
        };
        state.cloth.frame(state.sheet.capture_size, *state.sheet.frame);
    }
    if (enabled && state.cloth.moving()) {
        const auto &capture = decision.current_capture;
        state.cloth.update_movement(capture, elapsed);
        state.sheet.capture_size = {.x = capture.w, .y = capture.h};
        state.cloth.frame(state.sheet.capture_size, *state.sheet.frame);
    }
}

void SClothLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &snapshot,
    [[maybe_unused]] const Resources &resources) {
    const auto *cloth =
        (transformer != nullptr) ? std::get_if<SClothStyle>(&transformer->m_style) : nullptr;
    snapshot.handoff = (cloth != nullptr) && cloth->cloth.active();
    snapshot.transition_busy = (cloth != nullptr) && cloth->cloth.active() && !snapshot.handoff;
    snapshot.handoff_moving = (cloth != nullptr) && cloth->cloth.moving();
}

void SClothLiveStyle::step([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled,
    [[maybe_unused]] Resources &resources) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style);
    if (!enabled && cloth.cloth.moving()) {
        cancelMovement(transformer);
    }
    if (!cloth.cloth.active()) {
        return;
    }
    cloth.sheet.capture_size = size;
    cloth.cloth.advance(elapsed);
    cloth.cloth.frame(size, *cloth.sheet.frame);
}

void SClothLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style).cloth;
    if (cloth.active() && !cloth.moving()) {
        cloth.reset();
    }
}

void SClothLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &cloth = std::get<SClothStyle>(transformer.m_style);
    if (cloth.cloth.moving()) {
        cloth.cloth.reset();
        cloth.cloth.frame(cloth.sheet.capture_size, *cloth.sheet.frame);
    }
}

bool SClothLiveStyle::deforming(const SClothStyle &state) noexcept {
    return state.cloth.active();
}

bool SClothLiveStyle::builtFor(const SClothStyle &state, const Resources &resources) noexcept {
    return state.movement == resources.prepared.movement;
}

bool SClothLiveStyle::available(const SClothStyle &state) noexcept {
    return static_cast<bool>(state.sheet.frame);
}

SClothStyle SClothLiveStyle::initial(const Resources &resources, [[maybe_unused]] std::uint64_t seed) {
    return SClothStyle{.movement = resources.prepared.movement};
}

double SClothLiveStyle::strength(const Resources &resources) {
    return resources.prepared.movement.strength;
}
bool SClothLiveStyle::usable(const Resources &resources) {
    const auto &params = resources.prepared.movement;
    return std::isfinite(params.flutter) && std::isfinite(params.shading) &&
           std::isfinite(params.variation);
}

SClothLiveStyle::Prepared SClothLiveStyle::prepare_settings(const Settings &settings) {
    const auto &movement = settings.movement;
    const auto &open = settings.open;
    const auto &close = settings.close;
    return {
        .movement = {.strength = movement.strength, .flutter = movement.flutter,
            .shading = movement.shading, .variation = movement.variation, .shine = movement.shine,
            .springiness = movement.springiness, .gravity = movement.gravity},
        .open = {.duration = open.duration, .flutter = open.flutter, .shading = open.shading,
            .variation = open.variation, .shine = open.shine},
        .close = {.duration = close.duration, .flutter = close.flutter, .shading = close.shading,
            .variation = close.variation, .shine = close.shine},
    };
}
void SClothLiveStyle::prepare(const Settings &settings, Resources &resources, [[maybe_unused]] bool selected) {
    resources.update(settings, prepare_settings);
}
bool SClothLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated;
}
