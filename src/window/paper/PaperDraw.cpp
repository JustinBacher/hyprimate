#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/paper/PaperRenderer.hpp"
#include "window/paper/PaperStyle.hpp"

bool SPaperModule::warm(Resources &resources) {
    if (!resources.renderer) {
        resources.renderer = std::make_shared<CPaperRenderer>();
    }
    return resources.renderer->warm_up();
}

bool SPaperModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
    Resources &resources) {
    const auto &surface = state.surface();
    if (surface.faces.empty()) {
        return true;
    }  // no surface until the first compact pose
    auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
                         ? resources.renderer
                         : resources.scratch_renderer;
    if (!renderer) {
        renderer = std::make_shared<CPaperRenderer>();
    }
    return renderer->draw(surface, draw);
}
