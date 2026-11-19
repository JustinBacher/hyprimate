#include "animations/swarm/SwarmStyle.hpp"

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "HyprAnimationCanvas.hpp"

#include "animations/swarm/SwarmConfig.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

SSwarmModule::Prepared SSwarmModule::prepare_settings(const Settings &settings) {
    return {
        .open = swarm_params(settings.open),
        .close = swarm_params(settings.close),
        .movement = swarm_params(settings.movement),
        .open_sides = swarm_sides(settings.open.loneWindowSides),
        .close_sides = swarm_sides(settings.close.loneWindowSides),
    };
}

SSwarmModule::State SSwarmModule::start([[maybe_unused]] const Resources &resources, std::uint64_t seed) {
    State state;
    state.seed = seed;
    return state;
}

bool SSwarmModule::open(State &state, const Place &place, Resources &resources, Effects &effects) {
    if (!resources.last) {
        return false;
    }
    const EdgeSet SIDES = place.near.alone ? resources.prepared.open_sides :
        resolve_edge_sides(place.goal_body, place.screen, place.near.tiled_goals);
    const auto &creature = resources.last.value().open.sprite;
    if (SIDES.empty() || !is_usable(place.rest_capture) || !is_usable(place.screen) ||
        resources.sprites->sheet(creature) == nullptr) {
        return false;
    }
    state.creature = creature;
    state.swarm.reset();
    state.swarm.begin(Transition::Open, resources.prepared.open, place.seed,
        {.capture = place.rest_capture, .body = place.goal_body, .screen = place.screen}, SIDES);
    if (!state.swarm.active()) {
        return false;
    }
    effects.snap_to_goal = true;
    return true;
}

bool SSwarmModule::moves(const Motion &motion, const Resources &resources) {
    return motion.translated && resources.last && resources.sprites->sheet(resources.last.value().movement.sprite) != nullptr;
}

Frame SSwarmModule::advance(State &state, const Step &step, Resources &resources, [[maybe_unused]] Effects &effects) {
    if ((state.swarm.opening() && !step.open_enabled) ||
        (state.swarm.moving() && !step.movement_enabled)) {
        state.swarm.reset();
    }
    if (!step.dying && step.motion) {
        const auto &motion = *step.motion;
        if (step.movement_enabled && motion.translated && !state.swarm.active() && resources.last &&
            resources.sprites->sheet(resources.last.value().movement.sprite) != nullptr) {
            state.creature = resources.last.value().movement.sprite;
            state.swarm.reset();
            state.swarm.begin_movement(resources.prepared.movement, state.seed,
                {.capture = motion.from_capture, .body = motion.from_body, .screen = step.screen});
        }
        if (state.swarm.opening() || (step.movement_enabled && state.swarm.moving())) {
            state.swarm.update_movement(
                {.capture = motion.capture, .body = motion.body, .screen = step.screen});
        }
    }
    if (!step.dying && state.swarm.moving()) {
        state.swarm.set_movement_params(resources.prepared.movement);
    }
    state.swarm.advance(step.elapsed, step.movement_enabled && step.held);
    Box bounds = state.swarm.bounds();
    if (bounds.w > 0.0 && bounds.h > 0.0) {
        bounds.x -= 2.0;
        bounds.y -= 2.0;
        bounds.w += 4.0;
        bounds.h += 4.0;
    }
    return {.running = state.swarm.active(), .bounds = bounds,
        .tail = !step.dying && state.swarm.active() &&
            (step.workspace_moving || !step.workspace_visible)};
}

CloseStart SSwarmModule::close(State &state, const Place &place, Resources &resources) {
    const EdgeSet SIDES = place.near.alone ? resources.prepared.close_sides :
        resolve_edge_sides(place.body, place.screen, place.near.tiled_goals);
    if (SIDES.empty() || !is_usable(place.capture) || !is_usable(place.screen) ||
        (!state.swarm.active() && !resources.last)) {
        return CloseStart::Declined;
    }
    const auto &creature = state.swarm.active() ? state.creature : resources.last.value().close.sprite;
    if (resources.sprites->sheet(creature) == nullptr) {
        return CloseStart::Declined;
    }
    if (!state.swarm.active()) {
        state.creature = creature;
    }
    state.swarm.begin(Transition::Close, resources.prepared.close, place.seed,
        {.capture = place.capture, .body = place.body, .screen = place.screen}, SIDES);
    return state.swarm.active() ? CloseStart::Owned : CloseStart::Declined;
}

void SSwarmModule::end(State &state, [[maybe_unused]] Resources &resources, [[maybe_unused]] End why) noexcept {
    state.swarm.reset();
}

bool SSwarmModule::draw_canvas(State &state, CHyprAnimationCanvas &canvas,
    const SP<Render::IFramebuffer> &image, Box source, Box output, Box canvas_box,
    Vec2 image_origin, Vec2 image_size, Resources &resources) {
    const auto *sheet = resources.sprites->sheet(state.creature);
    const auto ATLAS = sheet != nullptr ? resources.sprites->texture(state.creature) : SP<Render::ITexture>{};
    if (sheet == nullptr || !ATLAS || !image || !image->getTexture()) {
        return false;
    }
    const CBox SOURCE{source.x, source.y, source.w, source.h};
    const CBox OUTPUT{output.x, output.y, output.w, output.h};
    const CBox CANVAS{canvas_box.x, canvas_box.y, canvas_box.w, canvas_box.h};
    auto &meshes = canvas.pass() == PassKind::Live ? state.meshes : state.scratch_meshes;
    fill_swarm_meshes(state.swarm, *sheet, SOURCE, OUTPUT, image_origin, image_size, meshes);
    auto &ctx = canvas.context();
    const auto LAYER = render_swarm_layer(ctx, image->getTexture(), ATLAS,
        swarm_layer_size(SOURCE), meshes, resources.sprites->surface());
    if (!LAYER) {
        return false;
    }
    const auto OUT = g_pHyprRenderer->getWorkBuffer(ctx, CANVAS.size());
    if (!OUT) {
        return false;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto DRAW_STATE = ctx.saveDrawState();
    auto &render_data = ctx.m_data;
    g_pHyprRenderer->bindFB(ctx, OUT);
    render_data.damage = CRegion{0.0, 0.0, CANVAS.w, CANVAS.h};
    render_data.fbSize = CANVAS.size();
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    if (!meshes.pieces.empty()) {
        Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
        texture_data.damage = &render_data.damage;
        texture_data.allowCustomUV = true;
        texture_data.allowDim = false;
        const CBox LOCAL{output.x - canvas_box.x, output.y - canvas_box.y, output.w, output.h};
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, LAYER->getTexture(), LOCAL, meshes.pieces, texture_data);
    }
    canvas.layer() = OUT;
    return true;
}
