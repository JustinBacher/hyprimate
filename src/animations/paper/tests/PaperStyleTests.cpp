#include <cmath>

#include "Check.hpp"
#include "animations/paper/PaperStyle.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

static_assert(StyleCore<SPaperModule>);
static_assert(MeshDrawn<SPaperModule, MeshVertex>);

int main() {
    SPaperResources resources;
    resources.prepared.open = {.duration = 0.6, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    resources.prepared.close = {.duration = 0.5, .crumpliness = 0.3, .shading = 0.0, .variation = 1.0};
    auto state = SPaperModule::start(resources, 0);
    Place place{};
    place.capture = {.x = 100, .y = 200, .w = 1000, .h = 700};
    place.seed = 7;
    Effects effects;
    CHECK(SPaperModule::open(state, place, resources, effects));
    Motion motion{};
    motion.translated = true;
    CHECK(!SPaperModule::moves(motion, resources));
    Step step{};
    step.elapsed = 0.1;
    step.capture = place.capture;
    step.open_enabled = true;
    const auto frame = SPaperModule::advance(state, step, resources, effects);
    CHECK(frame.running);
    const auto before = *state.frame;
    const double opacity = state.crumple.opacity();
    CHECK(SPaperModule::close(state, place, resources) == CloseStart::Owned);
    CHECK(std::abs(state.crumple.opacity() - opacity) < 1e-12);
    for (std::size_t i = 0; i < before.position.size(); ++i) {
        CHECK(std::abs(before.position[i].x - state.frame->position[i].x) < 1e-12);
        CHECK(std::abs(before.position[i].y - state.frame->position[i].y) < 1e-12);
    }
    CHECK(state.crumple.shaded());
    resources.prepared.close.shading = 0;
    step.dying = true;
    step.open_enabled = false;
    CHECK(SPaperModule::advance(state, step, resources, effects).running);
    CHECK(state.crumple.shaded());
    MeshBuffers<MeshVertex> buffers;
    const MeshTarget<MeshVertex> target{.source = {.x = 0, .y = 0, .w = 1000, .h = 700},
        .output = {.x = 0, .y = 0, .w = 1000, .h = 700}, .image_origin = {},
        .image_size = {.x = 1000, .y = 700}, .scale = 1, .buffers = buffers};
    const auto mesh = SPaperModule::mesh(state, target, resources);
    CHECK(mesh.window != nullptr);
    CHECK(mesh.shade != nullptr);
    CHECK(mesh.window->size() == SHEET_VERTEX_COUNT);
    SPaperModule::end(state, resources, End::Interrupted);
    CHECK(!state.crumple.active());

    auto fresh = SPaperModule::start(resources, 0);
    CHECK(SPaperModule::close(fresh, place, resources) == CloseStart::Owned);
    CHECK(!fresh.crumple.shaded());
    step.elapsed = STALL_SECONDS + 0.01;
    CHECK(!SPaperModule::advance(fresh, step, resources, effects).running);
    SPaperModule::end(fresh, resources, End::Completed);
    return 0;
}
