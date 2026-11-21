#include "HyprAnimationCanvas.hpp"
#include "animations/burn/BurnRenderer.hpp"
#include "animations/burn/BurnStyle.hpp"

bool SBurnModule::warm(Resources &resources) {
    if (!resources.renderer) {
        resources.renderer = std::make_shared<CBurnRenderer>();
    }
    return resources.renderer->warm_up();
}

bool SBurnModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
    Resources &resources) {
    // Movement-only Burn still displays the normal retained window image.
    if (!state.simulation || !draw.canvas) return false;
    auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
                         ? resources.renderer
                         : resources.scratch_renderer;
    if (!renderer) renderer = std::make_shared<CBurnRenderer>();
    // Drawing never advances dynamics or emits Ground/debris, so workspace
    // capture and inert passes cannot duplicate live emissions.
    return renderer->draw(*state.simulation, draw);
}
