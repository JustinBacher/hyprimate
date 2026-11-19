#include <cmath>
#include <cstdint>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/math/Box.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/paper/PaperLiveState.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/LiveSheetRender.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "config_generated.hpp"
using hyprimate::Vec2;

CBox live_extents(const SPaperStyle &state, const CBox &box) {
    return live_sheet_extents(state.sheet, box);
}

Render::SWindowTransformBuffer render_live(SPaperStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &input,
    [[maybe_unused]] const Render::SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SPaperResources &resources) {
    return render_live_sheet(
        state.sheet, state.crumple.opacity(), state.crumple.shaded(), ctx, input, geometry, resources.shade);
}

bool SPaperLiveStyle::open(CPluginRuntime &runtime,
    [[maybe_unused]] CPluginRuntime::STracked &tracked,
    const PHLWINDOW &window, SPaperResources &resources) {
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    auto *transformer = runtime.acquire_transformer(window);
    auto &state = std::get<SPaperStyle>(transformer->m_style);
    state.crumple.reset();
    state.crumple.begin(hyprimate::Transition::Open, resources.prepared.open, runtime.m_next_seed++);
    state.sheet.capture_size = {.x = CAPTURE.w, .y = CAPTURE.h};
    state.crumple.frame(state.sheet.capture_size, *state.sheet.frame);
    return true;
}

void SPaperLiveStyle::movement([[maybe_unused]] CPluginRuntime &runtime,
    [[maybe_unused]] CAnimationTransformer &transformer,
    [[maybe_unused]] const PHLWINDOW &window,
    [[maybe_unused]] const hyprimate::TrackDecision &decision,
    [[maybe_unused]] double elapsed,
    [[maybe_unused]] bool enabled,
    [[maybe_unused]] bool moving,
    [[maybe_unused]] Resources &resources) {
}

void SPaperLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &snapshot,
    [[maybe_unused]] const Resources &resources) {
    if (transformer != nullptr) {
        const auto *paper = std::get_if<SPaperStyle>(&transformer->m_style);
        snapshot.transition_busy = (paper != nullptr) && paper->crumple.active();
    }
}

void SPaperLiveStyle::step([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    [[maybe_unused]] bool enabled,
    [[maybe_unused]] Resources &resources) {
    auto &paper = std::get<SPaperStyle>(transformer.m_style);
    if (!paper.crumple.active()) {
        return;
    }
    paper.sheet.capture_size = size;
    paper.crumple.advance(elapsed);
    paper.crumple.frame(size, *paper.sheet.frame);
}

void SPaperLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &paper = std::get<SPaperStyle>(transformer.m_style);
    if (paper.crumple.active()) {
        paper.crumple.reset();
    }
}

void SPaperLiveStyle::cancelMovement([[maybe_unused]] CAnimationTransformer &transformer) {
}

bool SPaperLiveStyle::deforming(const SPaperStyle &state) noexcept {
    return state.crumple.active();
}

bool SPaperLiveStyle::builtFor([[maybe_unused]] const SPaperStyle &state,
    [[maybe_unused]] const Resources &resources) noexcept {
    return true;
}

bool SPaperLiveStyle::available(const SPaperStyle &state) noexcept {
    return static_cast<bool>(state.sheet.frame);
}

SPaperStyle SPaperLiveStyle::initial([[maybe_unused]] const Resources &resources,
    [[maybe_unused]] std::uint64_t seed) {
    return SPaperStyle{};
}

double SPaperLiveStyle::strength([[maybe_unused]] const Resources &resources) {
    return 0.0;
}
bool SPaperLiveStyle::usable([[maybe_unused]] const Resources &resources) { return true; }
SPaperLiveStyle::Prepared SPaperLiveStyle::prepare_settings(const Settings &settings) {
    return {
        .open = {.duration = settings.open.duration,
            .crumpliness = settings.open.crumpliness,
            .shading = settings.open.shading,
            .variation = settings.open.variation},
        .close = {.duration = settings.close.duration,
            .crumpliness = settings.close.crumpliness,
            .shading = settings.close.shading,
            .variation = settings.close.variation},
    };
}
void SPaperLiveStyle::prepare(const Settings &settings, Resources &resources, [[maybe_unused]] bool selected) {
    resources.update(settings, prepare_settings);
}
bool SPaperLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated;
}
