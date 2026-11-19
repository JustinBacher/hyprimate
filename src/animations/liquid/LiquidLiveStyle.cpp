#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>

#include "AnimationTransformer.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"
#include "PluginRuntime.hpp"
#include "WindowTracker.hpp"
#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeStyleConfig.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "config_generated.hpp"

using namespace Render;
using hyprimate::Vec2;
// Liquid mesh vertices are passed directly to Hyprland's renderer.
static_assert(sizeof(SMeshRenderVertex) == sizeof(hyprimate::LiquidVertex));
static_assert(offsetof(SMeshRenderVertex, x) == offsetof(hyprimate::LiquidVertex, x));
static_assert(offsetof(SMeshRenderVertex, y) == offsetof(hyprimate::LiquidVertex, y));
static_assert(offsetof(SMeshRenderVertex, u) == offsetof(hyprimate::LiquidVertex, u));
static_assert(offsetof(SMeshRenderVertex, v) == offsetof(hyprimate::LiquidVertex, v));
namespace {
    void refresh_pose(SLiquidStyle &state, Vec2 capture_size);
    void restore_box(SLiquidStyle &state, const PHLWINDOWREF &window_ref);
    void drive_box(SLiquidStyle &state, const PHLWINDOWREF &window_ref);
}  // namespace

hyprimate::LiquidPose live_pose(const SLiquidStyle &state) {
    if (!state.box_driven) {
        return {
            .mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .open_offset = {.x = 0.0, .y = 0.0},
            .displacement_gain = state.displacement_gain,
            .box_pose = false,
        };
    }
    const Vec2 BEAD = state.transition.bead_centre();
    return {
        .mesh = state.mesh,
        .transition = state.transition,
        .compensation = {.x = 1.0, .y = 1.0},
        .open_offset = {.x = -BEAD.x, .y = -BEAD.y},
        .displacement_gain = state.displacement_gain,
        .box_pose = true,
    };
}

CBox live_extents(const SLiquidStyle &state, const CBox &box) {
    const auto POSE = live_pose(state);
    const auto LOGICAL = to_box(box);
    auto extents = hyprimate::liquid_render_extents(POSE, LOGICAL, state.grid);
    hyprimate::expand_box(extents, 4.0);
    if (const auto VISIBLE = hyprimate::liquid_visible_box(POSE, LOGICAL)) {
        extents = hyprimate::intersect_boxes(extents, *VISIBLE);
    }
    return to_cbox(extents);
}

SWindowTransformBuffer render_live(SLiquidStyle &state,
    CRenderContext &ctx,
    const SWindowTransformBuffer &input,
    const SWindowTransformContext &context,
    const SLiveRenderGeometry &geometry,
    SLiquidResources &resources) {
    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, geometry.canvas.size());
    if (!OUT) {
        return {.framebuffer = input.framebuffer, .box = input.box, .success = false};
    }

    const auto POSE = live_pose(state);
    const auto LOGICAL = to_box(context.currentBox);
    const auto VISIBLE = hyprimate::liquid_visible_box(POSE, LOGICAL);
    const auto MENISCUS = hyprimate::liquid_meniscus_extents(POSE, LOGICAL);
    const SP<ITexture> SOLID = MENISCUS ? resources.open_solid.texture() : SP<ITexture>{};
    const CBox LIQUID_BOX =
        MENISCUS ? pixelBoxForLogical(to_cbox(*MENISCUS), geometry.scale) : CBox{};
    hyprimate::fill_liquid_render_mesh(POSE,
        to_box(geometry.source),
        geometry.scale,
        to_box(geometry.output),
        Vec2{.x = input.box.x, .y = input.box.y},
        geometry.texture_size,
        state.grid,
        state.vertices);
    if (SOLID) {
        hyprimate::fill_liquid_meniscus_mesh(POSE,
            to_box(geometry.source),
            geometry.scale,
            to_box(LIQUID_BOX),
            state.meniscus_vertices);
    }

    const GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    auto &render_data = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    render_data.damage = CRegion{0.0, 0.0, geometry.canvas.w, geometry.canvas.h};
    render_data.fbSize = geometry.canvas.size();
    g_pHyprRenderer->setProjectionType(ctx, RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (VISIBLE) {
        CBox clip = pixelBoxForLogical(to_cbox(*VISIBLE), geometry.scale);
        clip.translate(-geometry.canvas.pos());
        render_data.damage.intersect(CRegion{clip});
    }
    GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &render_data.damage;
    texture_data.a = 1.F;
    texture_data.allowCustomUV = true;
    if (SOLID) {
        GL::g_pHyprOpenGL->renderTextureMesh(ctx,
            SOLID,
            LIQUID_BOX.copy().translate(-geometry.canvas.pos()),
            state.meniscus_vertices,
            texture_data);
    }
    GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, input.framebuffer->getTexture(), geometry.local_output, state.vertices, texture_data);
    return {.framebuffer = OUT, .box = geometry.canvas};
}

bool SLiquidLiveStyle::open(
    CPluginRuntime &runtime, CPluginRuntime::STracked &entry, const PHLWINDOW &window,
    SLiquidResources &resources) {
    const auto FRAME = hyprimate::resolve_style_open_edge(
        runtime.edge_input_for(window, true), resources.last->open);
    if (!FRAME) {
        return false;
    }
    entry.open_edge = FRAME->edge;
    const auto &prepared = resources.prepared;
    if (prepared.open_strength <= 0.0) {
        return false;
    }
    auto *transformer = runtime.acquire_transformer(window);
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    auto &state = std::get<SLiquidStyle>(transformer->m_style);
    state.transition.begin(hyprimate::Transition::Open,
        *FRAME,
        prepared.open,
        prepared.open_strength);
    state.box_driven = state.transition.active();
    state.warped = false;
    if (state.box_driven) {
        state.goal_body = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    }
    refresh_pose(state, {.x = CAPTURE.w, .y = CAPTURE.h});
    return true;
}

void SLiquidLiveStyle::movement([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    const PHLWINDOW &window,
    const hyprimate::TrackDecision &decision,
    [[maybe_unused]] double elapsed,
    bool enabled,
    bool moving,
    Resources &resources) {
    const CBox CAPTURE = window->getFullWindowBoundingBox();
    const auto &current = decision.current_box;
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    if (state.mesh.params() != resources.prepared.movement) {
        state.mesh.set_params(resources.prepared.movement);
    }
    if (moving) {
        state.mesh.record_translation(
            {.x = current.x - decision.previous_box.x, .y = current.y - decision.previous_box.y},
            {.x = CAPTURE.w, .y = CAPTURE.h},
            resources.prepared.movement_strength);
    } else if (enabled) {
        state.mesh.advance(0.0, {.x = CAPTURE.w, .y = CAPTURE.h});
    } else {
        state.mesh.reset();
    }
    refresh_pose(state, {.x = CAPTURE.w, .y = CAPTURE.h});
}

void SLiquidLiveStyle::snapshot(
    const CAnimationTransformer *transformer, hyprimate::WindowSnapshot &snapshot,
    [[maybe_unused]] const Resources &resources) {
    const auto *state =
        (transformer != nullptr) ? std::get_if<SLiquidStyle>(&transformer->m_style) : nullptr;
    snapshot.transition_busy = (state != nullptr) && state->transition.active();
}

void SLiquidLiveStyle::step([[maybe_unused]] CPluginRuntime &runtime,
    CAnimationTransformer &transformer,
    double elapsed,
    hyprimate::Vec2 size,
    bool enabled,
    Resources &resources) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    if (state.mesh.params() != resources.prepared.movement) {
        state.mesh.set_params(resources.prepared.movement);
    }
    if (!enabled) {
        state.mesh.reset();
    }
    hyprimate::advance_liquid_pose(state.mesh,
        state.transition,
        state.grid,
        elapsed,
        size,
        hyprimate::LiquidStrength{resources.prepared.movement_strength},
        hyprimate::LiquidDrive{enabled ? resources.prepared.emerge_drag : 0.0});
    refresh_pose(state, size);
    drive_box(state, transformer.m_window);
}
namespace {
    void restore_box(SLiquidStyle &state, const PHLWINDOWREF &window_ref) {
        if (!state.box_driven || !state.warped) {
            return;
        }
        state.warped = false;
        const auto WINDOW = window_ref.lock();
        if (!WINDOW || !(state.goal_body.w > 0.0) || !(state.goal_body.h > 0.0)) {
            return;
        }
        WINDOW->positionAnimation()->setValueAndWarp(state.goal_body.pos());
        WINDOW->sizeAnimation()->setValueAndWarp(state.goal_body.size());
    }

    void drive_box(SLiquidStyle &state, const PHLWINDOWREF &window_ref) {
        if (!state.box_driven) {
            return;
        }
        if (!state.transition.active()) {
            restore_box(state, window_ref);
            state.box_driven = false;
            return;
        }
        const auto WINDOW = window_ref.lock();
        if (!WINDOW) {
            return;
        }

        auto &position_var = WINDOW->positionAnimation();
        auto &size_var = WINDOW->sizeAnimation();
        // The layout retargets the real box through the same variables. A goal other than the one
        // this wrote is the layout's.
        if (state.warped &&
            (position_var->goal() != state.warp_position || size_var->goal() != state.warp_size)) {
            state.goal_body = CBox{position_var->goal(), size_var->goal()};
        }
        if (!(state.goal_body.w > 0.0) || !(state.goal_body.h > 0.0)) {
            return;
        }

        // Decorations (border, shadow) do not scale with the box, so the live margins hold at any
        // size.
        const CBox CURRENT = WINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const CBox CAPTURE = WINDOW->getFullWindowBoundingBox();
        const double LEFT = CURRENT.x - CAPTURE.x;
        const double TOP = CURRENT.y - CAPTURE.y;
        const double RIGHT = (CAPTURE.x + CAPTURE.w) - (CURRENT.x + CURRENT.w);
        const double BOTTOM = (CAPTURE.y + CAPTURE.h) - (CURRENT.y + CURRENT.h);
        if (!(std::isfinite(LEFT) && std::isfinite(TOP) && std::isfinite(RIGHT) &&
                std::isfinite(BOTTOM))) {
            return;
        }

        const CBox REST = {state.goal_body.x - LEFT,
            state.goal_body.y - TOP,
            state.goal_body.w + LEFT + RIGHT,
            state.goal_body.h + TOP + BOTTOM};
        const Vec2 HALF = state.transition.bead_half({.x = REST.w, .y = REST.h});
        const Vec2 CENTRE = state.transition.bead_centre();
        const double CENTRE_X = REST.x + (0.5 * REST.w) + CENTRE.x;
        const double CENTRE_Y = REST.y + (0.5 * REST.h) + CENTRE.y;
        constexpr double MIN_SIDE = 2.0;
        const double WIDTH = std::max(MIN_SIDE, (2.0 * HALF.x) - LEFT - RIGHT);
        const double HEIGHT = std::max(MIN_SIDE, (2.0 * HALF.y) - TOP - BOTTOM);
        const Vector2D POSITION{CENTRE_X - HALF.x + LEFT, CENTRE_Y - HALF.y + TOP};
        const Vector2D SIZE{WIDTH, HEIGHT};

        position_var->setValueAndWarp(POSITION);
        size_var->setValueAndWarp(SIZE);
        state.warp_position = POSITION;
        state.warp_size = SIZE;
        state.warped = true;
    }
    void refresh_pose(SLiquidStyle &state, Vec2 capture_size) {
        // Gain only: the shared grid is built by the pose that draws it, so nothing is pre-filled
        // here.
        const hyprimate::LiquidPose FIT{
            .mesh = state.mesh,
            .transition = state.transition,
            .compensation = {.x = 1.0, .y = 1.0},
            .open_offset = {.x = 0.0, .y = 0.0},
            .displacement_gain = 1.0,
            .box_pose = state.box_driven,
        };
        state.displacement_gain = hyprimate::fit_liquid_pose_gain(FIT, capture_size, nullptr);
        state.grid.invalidate();
    }
}  // namespace

void SLiquidLiveStyle::cancelOpening(CAnimationTransformer &transformer) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    restore_box(state, transformer.m_window);
    state.transition.reset();
    state.box_driven = false;
    state.displacement_gain = 1.0;
    state.grid.invalidate();
}

void SLiquidLiveStyle::cancelMovement(CAnimationTransformer &transformer) {
    auto &state = std::get<SLiquidStyle>(transformer.m_style);
    state.mesh.reset();
    state.grid.invalidate();
}

bool SLiquidLiveStyle::deforming(const SLiquidStyle &state) noexcept {
    return !state.mesh.settled() || state.transition.active();
}

bool SLiquidLiveStyle::builtFor([[maybe_unused]] const SLiquidStyle &state,
    [[maybe_unused]] const Resources &resources) noexcept {
    return true;
}

bool SLiquidLiveStyle::available([[maybe_unused]] const SLiquidStyle &state) noexcept {
    return true;
}

SLiquidStyle SLiquidLiveStyle::initial(const Resources &resources, std::uint64_t seed) {
    return SLiquidStyle{.mesh = hyprimate::LiquidMesh{resources.prepared.movement, seed}};
}

double SLiquidLiveStyle::strength(const Resources &resources) {
    return resources.prepared.movement_strength;
}
bool SLiquidLiveStyle::usable(const Resources &resources) {
    const auto &params = resources.prepared.movement;
    return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) &&
           std::isfinite(params.momentum) && std::isfinite(params.variation);
}

SLiquidLiveStyle::Prepared SLiquidLiveStyle::prepare_settings(const Settings &settings) {
    const auto color = [](std::uint32_t argb) {
        const auto channel = [argb](unsigned int shift) {
            return static_cast<float>((argb >> shift) & 0xFFU) / 255.F;
        };
        return CHyprColor{channel(16), channel(8), channel(0), channel(24)};
    };
    return {.movement = {.viscosity = settings.movement.viscosity,
                .elasticity = settings.movement.elasticity,
                .momentum = settings.movement.momentum,
                .variation = settings.movement.variation},
        .open = {.elasticity = settings.open.elasticity, .momentum = settings.open.momentum},
        .close = {.elasticity = settings.close.elasticity, .momentum = settings.close.momentum},
        .movement_strength = settings.movement.strength,
        .open_strength = settings.open.strength,
        .close_strength = settings.close.strength,
        .emerge_drag = settings.open.emergeDrag,
        .close_splash = settings.close.splash,
        .open_color = color(settings.open.color),
        .close_color = color(settings.close.color)};
}
void SLiquidLiveStyle::prepare(const Settings &settings, Resources &resources, [[maybe_unused]] bool selected) {
    if (resources.update(settings, prepare_settings)) {
        resources.open_solid.set_color(resources.prepared.open_color);
        resources.close_solid.set_color(resources.prepared.close_color);
    }
}
bool SLiquidLiveStyle::movement_requested(const hyprimate::TrackDecision &decision) noexcept {
    return decision.translated;
}
