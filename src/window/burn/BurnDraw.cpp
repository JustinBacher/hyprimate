#include <memory>
#include <type_traits>

#include "window/burn/BurnRenderer.hpp"
#include "window/burn/BurnStyle.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"

static_assert(std::is_same_v<decltype(SBurnResources::renderer), std::unique_ptr<CBurnRenderer>>);
static_assert(
    std::is_same_v<decltype(SBurnResources::scratch_renderer), std::unique_ptr<CBurnRenderer>>);

SBurnResources::SBurnResources() = default;
SBurnResources::SBurnResources(SSharedStyleResources &) : SBurnResources() {
}
SBurnResources::~SBurnResources() = default;

bool SBurnModule::warm(Resources &resources) {
    if (!resources.renderer) {
        resources.renderer = std::make_unique<CBurnRenderer>();
    }
    return resources.renderer->warm_up() && resources.ground_draw.warm_up();
}

bool SBurnModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
    Resources &resources) {
    // Movement-only Burn still displays the normal retained window image.
    if (!state.simulation || !draw.canvas) return false;
    auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
                         ? resources.renderer
                         : resources.scratch_renderer;
    if (!renderer) renderer = std::make_unique<CBurnRenderer>();
    // Drawing never advances dynamics or emits Ground/debris, so workspace
    // capture and inert passes cannot duplicate live emissions.
    return renderer->draw(*state.simulation, draw);
}
