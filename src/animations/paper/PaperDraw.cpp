#include "HyprAnimationCanvas.hpp"
#include "animations/paper/PaperRenderer.hpp"
#include "animations/paper/PaperStyle.hpp"

void SPaperModule::warm(Resources &resources) {
    if (!resources.renderer) {
        resources.renderer = std::make_shared<CPaperRenderer>();
        resources.renderer->warm_up();
    }
}

bool SPaperModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
    Resources &resources) {
    if (state.display.faces.empty()) {
        return true;
    }  // no surface until the first compact pose
    auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
                         ? resources.renderer
                         : resources.scratch_renderer;
    if (!renderer) {
        renderer = std::make_shared<CPaperRenderer>();
    }
    return renderer->draw(state.display, draw);
}
