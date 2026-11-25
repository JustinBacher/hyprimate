#include "window/hyprland/HyprAnimationCanvas.hpp"
#include "window/paper/PaperRenderer.hpp"
#include "window/paper/PaperStyle.hpp"

SPaperResources::SPaperResources() = default;

SPaperResources::SPaperResources(SSharedStyleResources &) : SPaperResources() {
}

// Each renderer releases its GL objects in its own recorded context.
SPaperResources::~SPaperResources() = default;

bool SPaperModule::warm(Resources &resources) {
    if (!resources.m_live_renderer) {
        resources.m_live_renderer = std::make_unique<CPaperRenderer>();
    }
    return resources.m_live_renderer->warm_up();
}

bool SPaperModule::draw(State &state,
    const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
    Resources &resources) {
    const auto &surface = state.surface();
    if (surface.faces.empty()) {
        return true;
    }  // no surface until the first compact pose
    auto &renderer = draw.canvas->pass() == hyprimate::animation::PassKind::Live
                         ? resources.m_live_renderer
                         : resources.m_scratch_renderer;
    if (!renderer) {
        renderer = std::make_unique<CPaperRenderer>();
    }
    return renderer->draw(surface, draw);
}
