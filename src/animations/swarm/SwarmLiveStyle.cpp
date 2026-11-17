#include <optional>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "animations/swarm/SwarmConfig.hpp"
#include "animations/swarm/SwarmRenderer.hpp"

using hyprimate::Transition;

namespace {

    // Where the window is (or, opening, where it will rest) on its monitor; nullopt without a
    // monitor.
    std::optional<hyprimate::SwarmPlacement> placementOf(const PHLWINDOW &window, bool atGoal) {
        const auto monitor = window->m_monitor.lock();
        if (!monitor) return std::nullopt;
        const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox capture = window->getFullWindowBoundingBox();
        if (!atGoal)
            return hyprimate::SwarmPlacement{
                toBox(capture), toBox(current), toBox(monitor->logicalBox())};
        // Decoration margins (border, shadow) do not scale with native popin, so they carry over to
        // the rest box.
        const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const CBox rest{goal.x + (capture.x - current.x),
            goal.y + (capture.y - current.y),
            goal.w + (capture.w - current.w),
            goal.h + (capture.h - current.h)};
        return hyprimate::SwarmPlacement{toBox(rest), toBox(goal), toBox(monitor->logicalBox())};
    }

}  // namespace

CBox liveExtents(const SSwarmStyle &state, const CBox &box) {
    const hyprimate::Box bounds = state.swarm.bounds();
    // Nothing on screen yet (or any more): a token box keeps the window itself from being drawn in
    // its place.
    if (!(bounds.w > 0.0 && bounds.h > 0.0)) return CBox{box.x, box.y, 1.0, 1.0};
    return CBox{box.x + bounds.x, box.y + bounds.y, bounds.w, bounds.h}.expand(2.0);
}

Render::SWindowTransformBuffer renderLive(SSwarmStyle &state,
    Render::CRenderContext &ctx,
    const Render::SWindowTransformBuffer &in,
    const Render::SWindowTransformContext &,
    const SLiveRenderGeometry &g,
    const SP<CSolidFill> &,
    const SP<CPaperShade> &,
    const SP<CSwarmSprites> &sprites) {
    const Render::SWindowTransformBuffer failed{
        .framebuffer = in.framebuffer, .box = in.box, .success = false};
    const auto *sheet = sprites ? sprites->sheet(state.creature) : nullptr;
    const auto atlas = sheet ? sprites->texture(state.creature) : SP<Render::ITexture>{};
    if (!sheet || !atlas) return failed;

    fillSwarmMeshes(
        state.swarm, *sheet, g.source, g.output, {in.box.x, in.box.y}, g.textureSize, state.meshes);
    const auto layer = renderSwarmLayer(ctx,
        in.framebuffer->getTexture(),
        atlas,
        swarmLayerSize(g.source),
        state.meshes,
        sprites->surface());
    if (!layer) return failed;
    // Requested while the layer is held, so the pool cannot return the same buffer twice.
    const auto out = g_pHyprRenderer->getWorkBuffer(ctx, g.canvas.size());
    if (!out) return failed;

    Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto drawState = ctx.saveDrawState();
    auto &renderData = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, out);
    renderData.damage = CRegion{0.0, 0.0, g.canvas.w, g.canvas.h};
    renderData.fbSize = g.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (!state.meshes.pieces.empty()) {
        Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
        textureData.damage = &renderData.damage;
        textureData.allowCustomUV = true;
        textureData.allowDim = false;  // the layer is already dimmed
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, layer->getTexture(), g.localOutput, state.meshes.pieces, textureData);
    }
    return {.framebuffer = out, .box = g.canvas};
}

bool SSwarmLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window) {
    const auto sides = runtime.swarmSides(window, true);
    const auto placement = placementOf(window, true);
    const auto creature = swarmCreature(runtime.m_config.open.swarm);
    if (!placement || sides.empty() || !runtime.m_sprites->sheet(creature)) return false;

    auto *transformer = runtime.acquireTransformer(window);
    auto &state = std::get<SSwarmStyle>(transformer->m_style);
    state.creature = creature;
    state.swarm.reset();
    state.swarm.begin(
        Transition::Open, swarmParams(runtime.m_config.open.swarm), runtime.m_nextSeed++, *placement, sides);
    if (state.swarm.active()) {
        // The swarm assembles the window where it will rest, so the native popin is taken straight
        // to its goal.
        const CBox goal = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        g_pHyprRenderer->damageBox(window->getFullWindowBoundingBox());
        window->positionAnimation()->setValueAndWarp(goal.pos());
        window->sizeAnimation()->setValueAndWarp(goal.size());
        runtime.rebaseline(entry, window);
    }
    return true;
}

void SSwarmLiveStyle::movement(CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    double,
    bool enabled,
    bool moving) {
    auto &state = std::get<SSwarmStyle>(transformer.m_style);
    auto placement = placementOf(window, false);
    if (!placement) return;
    placement->capture = decision.currentCapture;
    placement->body = decision.currentBox;
    if (moving && !state.swarm.active()) {
        // Formed where the window was, so the first update carries this frame's movement.
        state.creature = swarmCreature(runtime.m_config.movement.swarm);
        state.swarm.reset();
        state.swarm.beginMovement(swarmParams(runtime.m_config.movement.swarm),
            runtime.m_nextSeed++,
            {decision.previousCapture, decision.previousBox, placement->screen});
    }
    // An opening follows its window even with movement off; its agents are in flight either way.
    if (state.swarm.opening() || (enabled && state.swarm.moving()))
        state.swarm.updateMovement(*placement, decision.workspaceDelta);
}

void SSwarmLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &s) {
    const auto *state = transformer ? std::get_if<SSwarmStyle>(&transformer->m_style) : nullptr;
    s.handoff = state && state->swarm.active();
    s.transitionBusy = false;
    s.handoffMoving = state && state->swarm.moving();
}

void SSwarmLiveStyle::step(CPluginRuntime &,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2,
    bool enabled) {
    auto &state = std::get<SSwarmStyle>(transformer.m_style);
    if (!enabled && state.swarm.moving()) cancelMovement(transformer);
    const auto target = g_layoutManager->dragController()->target();
    const bool held = enabled && target && target->window() == transformer.m_window.lock() &&
                      g_layoutManager->dragController()->mode() == MBIND_MOVE;
    state.swarm.advance(elapsed, held);
}

void SSwarmLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &swarm = std::get<SSwarmStyle>(transformer.m_style).swarm;
    if (swarm.opening()) swarm.reset();
}

void SSwarmLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &swarm = std::get<SSwarmStyle>(transformer.m_style).swarm;
    if (swarm.moving()) swarm.reset();
}

bool SSwarmLiveStyle::deforming(const CAnimationTransformer &transformer) noexcept {
    return std::get<SSwarmStyle>(transformer.m_style).swarm.active();
}

// Swarm settings are read when an animation begins, so a state is never stale for them.
bool SSwarmLiveStyle::builtFor(const CAnimationTransformer &,
    hyprimate::config::Style style,
    hyprimate::MaterialParams,
    hyprimate::ClothMovementParams) noexcept {
    return style == hyprimate::config::Style::Swarm;
}

bool SSwarmLiveStyle::available(const CAnimationTransformer &) noexcept {
    return true;
}

SStyleState SSwarmLiveStyle::initial(
    hyprimate::MaterialParams, hyprimate::ClothMovementParams, std::uint64_t) {
    return SSwarmStyle{};
}

// Movement has no strength setting: the window either holds together or is carried by its swarm.
double SSwarmLiveStyle::strength(const CPluginRuntime &) {
    return 1.0;
}
bool SSwarmLiveStyle::usable(const CPluginRuntime &runtime) {
    return runtime.m_sprites->sheet(swarmCreature(runtime.m_config.movement.swarm)) != nullptr;
}
