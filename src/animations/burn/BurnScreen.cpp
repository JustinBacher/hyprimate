#include "BurnStyle.hpp"
#include "HyprAnimationCanvas.hpp"

void SBurnModule::Screen::tick(
    Resources &resources, const hyprimate::animation::ScreenFacts &facts) {
    resources.ground.tick(facts);
}
bool SBurnModule::Screen::needs_frame(
    const Resources &resources, hyprimate::animation::MonitorId monitor) {
    return resources.ground.active(monitor);
}
void SBurnModule::Screen::under_window(
    Resources &, const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &) {
}
void SBurnModule::Screen::before_windows(
    Resources &resources, const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw) {
    if (draw.real_pass && draw.canvas != nullptr) {
        resources.ground_draw.queue(*draw.canvas, resources.ground, draw.screen);
    }
}
void SBurnModule::Screen::after_windows(
    Resources &, const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &) {
}
void SBurnModule::Screen::clear(
    Resources &resources, std::optional<hyprimate::animation::MonitorId> monitor) {
    resources.ground.clear(monitor);
}
