// Exercise the generated configuration without requiring a running compositor.
#include <any>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
using HANDLE = void*;
template <typename T> using SP = std::shared_ptr<T>;
// NOLINTNEXTLINE(readability-identifier-naming): mirrors Hyprland's makeShared, which the generated header calls.
template <typename T, typename... Args> SP<T> makeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

// NOLINTNEXTLINE(readability-identifier-naming): mirrors Hyprland's Config namespace used by the generated header.
namespace Config {
using BOOL = bool;
using INTEGER = long long;
using FLOAT = float;
using STRING = std::string;
// NOLINTNEXTLINE(readability-identifier-naming): mirrors Hyprland's Config::Values namespace.
namespace Values {
struct SFloatValueOptions { float min, max; };
struct SIntValueOptions { long long min, max; };
struct SStringValueOptions { std::function<std::expected<void, std::string>(const STRING&)> validator; };
struct Entry {
    bool specified = false;
    Entry() = default;
    virtual ~Entry() = default;
    Entry(const Entry &) = default;
    Entry &operator=(const Entry &) = default;
    Entry(Entry &&) = default;
    Entry &operator=(Entry &&) = default;
};
std::unordered_map<std::string, Entry*>& entries() {
    static std::unordered_map<std::string, Entry*> registry;
    return registry;
}
template <typename T> struct Value : Entry {
    T current;
    template <typename... Options> Value(const std::string& key, const char* /*description*/, T initial, [[maybe_unused]] Options&&... options)
        : current(std::move(initial)) { entries()[key] = this; }
    [[nodiscard]] T value() const { return current; }
};
using CBoolValue = Value<bool>;
using CIntValue = Value<long long>;
using CFloatValue = Value<float>;
using CStringValue = Value<std::string>;
using CColorValue = Value<unsigned int>;
}
}
template <typename T> class CConfigValue {
    std::string key;
  public:
    CConfigValue() = default;
    void bind(std::string name) { key = std::move(name); }
    explicit CConfigValue(std::string name) : key(std::move(name)) {}
    // NOLINTNEXTLINE(readability-identifier-naming): mirrors Hyprland's CConfigValue::setByUser.
    [[nodiscard]] bool setByUser() const { return Config::Values::entries().at(key)->specified; }
};
// NOLINTNEXTLINE(readability-identifier-naming): mirrors Hyprland's HyprlandAPI namespace.
namespace HyprlandAPI {
// NOLINTNEXTLINE(readability-identifier-naming): mirrors HyprlandAPI::addConfigValueV2.
template <typename T> bool addConfigValueV2(HANDLE /*handle*/, const SP<T>& /*value*/) { return true; }
}
}

#include "config_generated.hpp"

using namespace hyprimate::config;
namespace {
template <typename T> void set(const char* suffix, T value) {
    auto* entry = Config::Values::entries().at(std::string("plugin:hyprimate:") + suffix);
    auto* typed = dynamic_cast<Config::Values::Value<T>*>(entry);
    assert(typed);
    typed->current = std::move(value);
    typed->specified = true;
}
[[maybe_unused]] bool near(double first, double second) { return std::abs(first - second) < 0.00001; }
}

int main() try {
    SConfigValues values;
    registerConfigValues(nullptr, values);
    auto config = readConfig(values);
    assert(!config.workspace.enabled);
    assert(config.workspace.style == WorkspaceStyle::Rotation);
    assert(config.style == Style::Liquid);
    assert(near(config.workspace.rotation.duration, 0.6));
    assert(config.workspace.planetHop.windowMode == PlanetHopWindowMode::Surface);
    assert(config.workspace.planetHop.overlayMode == PlanetHopOverlayMode::Fade);
    assert(config.workspace.planetHop.planetLayout == PlanetHopPlanetLayout::Focused);
    assert(config.workspace.planetHop.starsEnabled && config.workspace.planetHop.atmosphereEnabled);
    assert(config.workspace.planetHop.lightingEnabled && config.workspace.planetHop.cloudsEnabled);
    assert(config.workspace.planetHop.depthEnabled && config.workspace.planetHop.rotationEnabled);
    assert(near(config.workspace.planetHop.duration, 800));
    assert(near(config.workspace.planetHop.solarVerticalSpread, .65));
    assert(near(config.workspace.planetHop.solarBackgroundScale, .75));
    assert(near(config.workspace.planetHop.liveBoundary, 0.22));
    assert(near(config.workspace.planetHop.elevation, 0.2));
    assert(config.workspace.planetHop.curve == "linear");
    assert(config.workspace.planetHop.depthSource.empty());
    set("workspace:style", std::string("planet_hop"));
    set("workspace:options:planet_hop:window_mode", std::string("floating"));
    set("workspace:options:planet_hop:overlay_mode", std::string("hud"));
    set("workspace:options:planet_hop:planet_layout", std::string("solar_system"));
    set("workspace:options:planet_hop:duration", 1250.0F);
    set("workspace:options:planet_hop:clouds_enabled", false);
    set("workspace:options:planet_hop:depth_strength", 9.0F);
    set("workspace:options:planet_hop:solar_vertical_spread", 12.0F);
    set("workspace:options:planet_hop:solar_background_scale", 0.0F);
    set("workspace:options:planet_hop:sampling_quality", -1LL);
    set("workspace:options:planet_hop:live_boundary", 1.0F);
    set("workspace:options:planet_hop:elevation", -1.0F);
    config = readConfig(values);
    assert(config.workspace.style == WorkspaceStyle::PlanetHop);
    assert(config.workspace.planetHop.windowMode == PlanetHopWindowMode::Floating);
    assert(config.workspace.planetHop.overlayMode == PlanetHopOverlayMode::Hud);
    assert(config.workspace.planetHop.planetLayout == PlanetHopPlanetLayout::SolarSystem);
    assert(near(config.workspace.planetHop.duration, 1250));
    assert(!config.workspace.planetHop.cloudsEnabled && config.workspace.planetHop.starsEnabled);
    assert(near(config.workspace.planetHop.depthStrength, 0.5));
    assert(near(config.workspace.planetHop.solarVerticalSpread, 2.0));
    assert(near(config.workspace.planetHop.solarBackgroundScale, .25));
    assert(config.workspace.planetHop.samplingQuality == 0);
    assert(near(config.workspace.planetHop.liveBoundary, 0.35));
    assert(near(config.workspace.planetHop.elevation, 0));
    set("workspace:options:planet_hop:window_mode", std::string("invalid"));
    set("workspace:options:planet_hop:depth_strength", std::numeric_limits<float>::quiet_NaN());
    config = readConfig(values);
    assert(config.workspace.planetHop.windowMode == PlanetHopWindowMode::Surface);
    assert(near(config.workspace.planetHop.depthStrength, 0));
    set("workspace:enabled", true);
    set("workspace:style", std::string("paper"));
    set("window:style", std::string("rotation"));
    set("workspace:options:rotation:duration", 9.0F);
    config = readConfig(values);
    assert(config.workspace.enabled && config.workspace.style == WorkspaceStyle::Rotation);
    assert(config.style == Style::Liquid);
    assert(near(config.workspace.rotation.duration, 3.0));
    set("workspace:options:rotation:duration", -1.0F);
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    set("workspace:options:rotation:duration", std::numeric_limits<float>::quiet_NaN());
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    set("workspace:options:rotation:duration", std::numeric_limits<float>::infinity());
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    // Unset globals must retain each transition's historical default.
    if (!near(config.construction.open.duration, 0.62)) return 1;
    if (config.construction.movement.variant != Variant::Native) return 1;
    set("window:options:construction:duration", 2.25F);
    if (!near(readConfig(values).construction.open.duration, 2.25)) return 1;
    set("window:open:options:construction:duration", 1.75F);
    if (!near(readConfig(values).construction.open.duration, 1.75)) return 1;
    set("window:movement:options:construction:variant", std::string("crew_carry"));
    if (readConfig(values).construction.movement.variant != Variant::CrewCarry) return 1;
    set("window:movement:options:construction:variant", std::string("unsupported"));
    if (readConfig(values).construction.movement.variant != Variant::Native) return 1;
    assert(near(config.paper.open.duration, 0.6));
    assert(near(config.paper.close.duration, 0.5));
    assert(near(config.paper.open.paperStiffness, .72) &&
           near(config.paper.close.paperStiffness, .72));
    assert(near(config.paper.open.settling, .45) && near(config.paper.close.settling, .45));
    assert(near(config.paper.shared.stiffness, .75) && near(config.paper.shared.inertia, .45));
    assert(near(config.paper.shared.settleDamping, .8));
    assert(config.paper.move.style == PaperMoveStyle::Flutter);
    assert(near(config.paper.move.flutter.airResistance, .65));
    assert(near(config.paper.move.flutter.intensity, .5) &&
           near(config.paper.move.flutter.frequency, 1.2));
    assert(near(config.paper.move.flutter.lift, .3) && near(config.paper.move.flutter.torsion, .4));
    assert(near(config.paper.move.glide.friction, .6) &&
           near(config.paper.move.glide.cornerLift, .25));
    assert(near(config.paper.move.glide.edgeCurl, .3) &&
           near(config.paper.move.glide.shadowSeparation, .2));
    assert(near(config.paper.resize.foldIntensity, .6) &&
           near(config.paper.resize.curlIntensity, .45));
    assert(near(config.paper.resize.creaseDensity, .5));
    assert(near(config.cloth.open.duration, 1.0));
    assert(near(config.cloth.close.duration, 0.9));
    assert(near(config.bubble.open.duration, 1.0));
    assert(near(config.bubble.close.duration, 0.42));
    assert(near(config.liquid.movement.strength, 0.65));
    // Formation-only keys live in the existing opening table, never global/close/movement.
    for (const char* leaf : {"count", "birth_rate", "size_variation", "growth_variation",
             "interaction", "damping", "buoyancy", "linger"}) {
        [[maybe_unused]] const std::string SUFFIX = std::string(":options:bubble:") + leaf;
        assert(Config::Values::entries().contains("plugin:hyprimate:window:open" + SUFFIX));
        assert(!Config::Values::entries().contains("plugin:hyprimate:window" + SUFFIX));
        assert(!Config::Values::entries().contains("plugin:hyprimate:window:close" + SUFFIX));
        assert(!Config::Values::entries().contains("plugin:hyprimate:window:movement" + SUFFIX));
    }
    set("window:open:options:bubble:count", 12LL);
    set("window:open:options:bubble:birth_rate", 1.7F);
    set("window:open:options:bubble:size_variation", .9F);
    set("window:open:options:bubble:growth_variation", .3F);
    set("window:open:options:bubble:interaction", 1.4F);
    set("window:open:options:bubble:damping", 1.8F);
    set("window:open:options:bubble:buoyancy", 0.0F);
    set("window:open:options:bubble:linger", .8F);
    config = readConfig(values);
    assert(config.bubble.open.count == 12 && near(config.bubble.open.birthRate, 1.7));
    assert(near(config.bubble.open.sizeVariation, .9) && near(config.bubble.open.growthVariation, .3));
    assert(near(config.bubble.open.interaction, 1.4) && near(config.bubble.open.damping, 1.8));
    assert(near(config.bubble.open.buoyancy, 0) && near(config.bubble.open.linger, .8));
    assert(config.bubble.close.count == 0 && near(config.bubble.close.duration, .42));
    set("window:open:options:bubble:count", 999LL);
    set("window:open:options:bubble:birth_rate", -1.0F);
    config = readConfig(values);
    assert(config.bubble.open.count == 160 && near(config.bubble.open.birthRate, .25));

    set("window:options:paper:duration", 1.7F);
    set("window:options:liquid:strength", 1.2F);
    set("window:options:cloth:flutter", 0.8F);
    set("window:options:bubble:duration", 1.4F);
    config = readConfig(values);
    assert(near(config.paper.open.duration, 1.7) && near(config.paper.close.duration, 1.7));
    assert(near(config.liquid.open.strength, 1.2) && near(config.liquid.close.strength, 1.2));
    assert(near(config.liquid.movement.strength, 1.2));
    assert(near(config.cloth.open.flutter, 0.8) && near(config.cloth.close.flutter, 0.8));
    assert(near(config.cloth.movement.flutter, 0.8));
    assert(near(config.bubble.open.duration, 1.4) && near(config.bubble.close.duration, 1.4));

    set("window:options:paper:crumple_origin", std::string("cursor"));
    set("window:close:options:paper:crumple_origin", std::string("left"));
    set("window:options:paper:force_direction", std::string("rotational"));
    set("window:open:options:paper:simulation_quality", std::string("high"));
    set("window:open:options:paper:unfold_tension", 0.81F);
    config = readConfig(values);
    assert(config.paper.open.crumpleOrigin == CrumpleOrigin::Cursor);
    assert(config.paper.close.crumpleOrigin == CrumpleOrigin::Left);
    assert(config.paper.close.forceDirection == ForceDirection::Rotational);
    assert(config.paper.open.simulationQuality == SimulationQuality::High);
    assert(config.paper.close.simulationQuality == SimulationQuality::Adaptive);
    assert(near(config.paper.open.unfoldTension, 0.81));

    // Non-numeric styles and direction-specific options inherit too.
    set("window:options:bubble:shape", std::string("circle"));
    set("window:options:liquid:color", 0xff123456U);
    set("window:options:liquid:splash", false);
    set("window:options:swarm:lone_window_sides", std::string("top,left"));
    set("window:options:liquid:default_animation_edge", std::string("top"));
    config = readConfig(values);
    assert(config.bubble.movement.shape == Shape::Circle);
    assert(config.liquid.open.color == 0xff123456U && config.liquid.close.color == 0xff123456U);
    assert(config.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(config.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(!config.liquid.close.splash);
    assert(config.swarm.open.loneWindowSides.has(LoneWindowSides::Top));
    assert(!config.swarm.open.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(config.swarm.close.loneWindowSides.has(LoneWindowSides::Left));
    set("window:close:options:liquid:splash", true);
    set("window:open:options:swarm:lone_window_sides", std::string("all"));
    set("window:close:options:liquid:default_animation_edge", std::string("right"));
    config = readConfig(values);
    assert(config.liquid.close.splash);
    assert(config.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(config.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Right);
    assert(config.swarm.open.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(!config.swarm.close.loneWindowSides.has(LoneWindowSides::Bottom));

    // Liquid and cloth choose opening origins and closing exits independently.
    set("window:open:options:liquid:edge_bounds", std::string("work_area"));
    set("window:open:options:cloth:default_animation_edge", std::string("left"));
    set("window:open:options:cloth:edge_bounds", std::string("screen"));
    set("window:close:options:liquid:side", std::string("right"));
    set("window:close:options:liquid:edge_bounds", std::string("work_area"));
    set("window:close:options:cloth:side", std::string("origin"));
    set("window:close:options:cloth:default_animation_edge", std::string("top"));
    set("window:close:options:cloth:edge_bounds", std::string("screen"));
    config = readConfig(values);
    assert(config.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(config.liquid.open.edgeBounds == EdgeBounds::WorkArea);
    assert(config.cloth.open.defaultAnimationEdge == DefaultAnimationEdge::Left);
    assert(config.cloth.open.edgeBounds == EdgeBounds::Screen);
    assert(config.liquid.close.side == Side::Right);
    assert(config.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Right);
    assert(config.liquid.close.edgeBounds == EdgeBounds::WorkArea);
    assert(config.cloth.close.side == Side::Origin);
    assert(config.cloth.close.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(config.cloth.close.edgeBounds == EdgeBounds::Screen);

    // Explicit values equal to old defaults still take precedence over global settings.
    set("window:open:options:paper:duration", 0.6F);
    set("window:close:options:paper:duration", 0.5F);
    set("window:movement:options:liquid:strength", 0.65F);
    set("window:close:options:cloth:flutter", 0.2F);
    config = readConfig(values);
    assert(near(config.paper.open.duration, 0.6) && near(config.paper.close.duration, 0.5));
    assert(near(config.liquid.movement.strength, 0.65));
    assert(near(config.liquid.open.strength, 1.2));
    assert(near(config.cloth.close.flutter, 0.2) && near(config.cloth.open.flutter, 0.8));

    set("window:options:swarm:count", 123LL);
    set("window:options:swarm:speed", 2100.0F);
    set("window:open:options:swarm:count", 250LL);
    set("window:close:options:swarm:speed", 900.0F);
    set("window:options:swarm:sprite", std::string("/tmp/custom sprite.png"));
    set("window:open:options:swarm:sprite", std::string("/tmp/open.png"));
    set("window:options:swarm:agility", 0.8F);
    set("window:movement:options:swarm:agility", 0.5F);
    config = readConfig(values);
    assert(config.swarm.open.count == 250 && config.swarm.close.count == 123 && config.swarm.movement.count == 123);
    assert(near(config.swarm.open.speed, 2100) && near(config.swarm.close.speed, 900));
    assert(near(config.swarm.movement.speed, 2100));
    assert(config.swarm.open.sprite == "/tmp/open.png" && config.swarm.close.sprite == "/tmp/custom sprite.png");
    assert(config.swarm.movement.sprite == "/tmp/custom sprite.png");
    assert(near(config.swarm.open.agility, 0.8) && near(config.swarm.close.agility, 0.8));
    assert(near(config.swarm.movement.agility, 0.5));

    set("window:options:paper:stiffness", .91F);
    set("window:options:paper:settle_damping", .87F);
    set("window:options:paper:inertia", .63F);
    config = readConfig(values);
    assert(near(config.paper.open.paperStiffness, .91) &&
           near(config.paper.close.paperStiffness, .91));
    assert(near(config.paper.open.settling, .87) && near(config.paper.close.settling, .87));
    assert(near(config.paper.shared.inertia, .63));
    set("window:options:paper:paper_stiffness", .72F);
    set("window:options:paper:settling", .45F);
    config = readConfig(values);
    assert(near(config.paper.open.paperStiffness, .72) &&
           near(config.paper.close.paperStiffness, .72));
    assert(near(config.paper.open.settling, .45) && near(config.paper.close.settling, .45));
    set("window:open:options:paper:paper_stiffness", .33F);
    set("window:close:options:paper:settling", .22F);
    config = readConfig(values);
    assert(near(config.paper.open.paperStiffness, .33) &&
           near(config.paper.close.paperStiffness, .72));
    assert(near(config.paper.open.settling, .45) && near(config.paper.close.settling, .22));
    assert(
        near(config.paper.shared.stiffness, .91) && near(config.paper.shared.settleDamping, .87));
    // Explicit phase defaults still outrank the new material without a legacy shared value.
    Config::Values::entries()
        .at("plugin:hyprimate:window:options:paper:paper_stiffness")
        ->specified = false;
    Config::Values::entries().at("plugin:hyprimate:window:options:paper:settling")->specified =
        false;
    set("window:open:options:paper:paper_stiffness", .72F);
    set("window:close:options:paper:settling", .45F);
    config = readConfig(values);
    assert(near(config.paper.open.paperStiffness, .72) &&
           near(config.paper.close.paperStiffness, .91));
    assert(near(config.paper.open.settling, .87) && near(config.paper.close.settling, .45));

    set("window:options:paper:move:style", std::string("glide"));
    set("window:options:paper:move:flutter:air_resistance", .13F);
    set("window:options:paper:move:glide:edge_curl", .74F);
    set("window:options:paper:resize:fold_intensity", .29F);
    config = readConfig(values);
    assert(config.paper.move.style == PaperMoveStyle::Glide);
    assert(near(config.paper.move.flutter.airResistance, .13));
    assert(near(config.paper.move.glide.edgeCurl, .74));
    assert(near(config.paper.resize.foldIntensity, .29));
    set("window:options:paper:move:style", std::string("invalid"));
    assert(readConfig(values).paper.move.style == PaperMoveStyle::Flutter);
    set("window:options:paper:stiffness", -1.0F);
    set("window:options:paper:inertia", 2.0F);
    set("window:options:paper:settle_damping", std::numeric_limits<float>::infinity());
    set("window:options:paper:move:flutter:frequency", std::numeric_limits<float>::quiet_NaN());
    set("window:options:paper:move:glide:corner_lift", 2.0F);
    set("window:options:paper:resize:crease_density", -1.0F);
    config = readConfig(values);
    assert(near(config.paper.shared.stiffness, 0) && near(config.paper.shared.inertia, 1));
    assert(near(config.paper.shared.settleDamping, 0));
    assert(near(config.paper.close.paperStiffness, 0) && near(config.paper.open.settling, 0));
    assert(near(config.paper.move.flutter.frequency, .1));
    assert(
        near(config.paper.move.glide.cornerLift, 1) && near(config.paper.resize.creaseDensity, 0));
}
catch (const std::exception &) {
    return 1;
}
