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

using HANDLE = void*;
template <typename T> using SP = std::shared_ptr<T>;
template <typename T, typename... Args> SP<T> makeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

namespace Config {
using BOOL = bool;
using INTEGER = long long;
using FLOAT = float;
using STRING = std::string;
namespace Values {
struct SFloatValueOptions { float min, max; };
struct SIntValueOptions { long long min, max; };
struct SStringValueOptions { std::function<std::expected<void, std::string>(const STRING&)> validator; };
struct Entry {
    bool specified = false;
    virtual ~Entry() = default;
};
inline std::unordered_map<std::string, Entry*> entries;
template <typename T> struct Value : Entry {
    T current;
    template <typename... Options> Value(const std::string& key, const char*, T initial, Options&&...)
        : current(initial) { entries[key] = this; }
    T value() const { return current; }
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
    void bind(const std::string& name) { key = name; }
    explicit CConfigValue(const std::string& name) : key(name) {}
    bool setByUser() const { return Config::Values::entries.at(key)->specified; }
};
namespace HyprlandAPI {
template <typename T> bool addConfigValueV2(HANDLE, const SP<T>&) { return true; }
}

#include "config_generated.hpp"

using namespace hyprimate::config;
template <typename T> void set(const char* suffix, T value) {
    auto* entry = Config::Values::entries.at(std::string("plugin:hyprimate:") + suffix);
    auto* typed = dynamic_cast<Config::Values::Value<T>*>(entry);
    assert(typed);
    typed->current = value;
    typed->specified = true;
}
bool near(double a, double b) { return std::abs(a - b) < 0.00001; }

int main() {
    SConfigValues values;
    registerConfigValues(nullptr, values);
    auto c = readConfig(values);
    assert(!c.workspace.enabled);
    assert(c.workspace.style == WorkspaceStyle::Rotation);
    assert(c.style == Style::Liquid);
    assert(near(c.workspace.rotation.duration, 0.6));
    set("workspace:enabled", true);
    set("workspace:style", std::string("paper"));
    set("window:style", std::string("rotation"));
    set("workspace:options:rotation:duration", 9.0F);
    c = readConfig(values);
    assert(c.workspace.enabled && c.workspace.style == WorkspaceStyle::Rotation);
    assert(c.style == Style::Liquid);
    assert(near(c.workspace.rotation.duration, 3.0));
    set("workspace:options:rotation:duration", -1.0F);
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    set("workspace:options:rotation:duration", std::numeric_limits<float>::quiet_NaN());
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    set("workspace:options:rotation:duration", std::numeric_limits<float>::infinity());
    assert(near(readConfig(values).workspace.rotation.duration, 0.1));
    // Unset globals must retain each transition's historical default.
    assert(near(c.paper.open.duration, 0.6));
    assert(near(c.paper.close.duration, 0.5));
    assert(near(c.cloth.open.duration, 1.0));
    assert(near(c.cloth.close.duration, 0.9));
    assert(near(c.bubble.open.duration, 1.0));
    assert(near(c.bubble.close.duration, 0.42));
    assert(near(c.liquid.movement.strength, 0.65));
    // Formation-only keys live in the existing opening table, never global/close/movement.
    for (const char* leaf : {"count", "birth_rate", "size_variation", "growth_variation",
             "interaction", "damping", "buoyancy", "linger"}) {
        const std::string suffix = std::string(":options:bubble:") + leaf;
        assert(Config::Values::entries.contains("plugin:hyprimate:window:open" + suffix));
        assert(!Config::Values::entries.contains("plugin:hyprimate:window" + suffix));
        assert(!Config::Values::entries.contains("plugin:hyprimate:window:close" + suffix));
        assert(!Config::Values::entries.contains("plugin:hyprimate:window:movement" + suffix));
    }
    set("window:open:options:bubble:count", 12LL);
    set("window:open:options:bubble:birth_rate", 1.7F);
    set("window:open:options:bubble:size_variation", .9F);
    set("window:open:options:bubble:growth_variation", .3F);
    set("window:open:options:bubble:interaction", 1.4F);
    set("window:open:options:bubble:damping", 1.8F);
    set("window:open:options:bubble:buoyancy", 0.0F);
    set("window:open:options:bubble:linger", .8F);
    c = readConfig(values);
    assert(c.bubble.open.count == 12 && near(c.bubble.open.birthRate, 1.7));
    assert(near(c.bubble.open.sizeVariation, .9) && near(c.bubble.open.growthVariation, .3));
    assert(near(c.bubble.open.interaction, 1.4) && near(c.bubble.open.damping, 1.8));
    assert(near(c.bubble.open.buoyancy, 0) && near(c.bubble.open.linger, .8));
    assert(c.bubble.close.count == 0 && near(c.bubble.close.duration, .42));
    set("window:open:options:bubble:count", 999LL);
    set("window:open:options:bubble:birth_rate", -1.0F);
    c = readConfig(values);
    assert(c.bubble.open.count == 160 && near(c.bubble.open.birthRate, .25));

    set("window:options:paper:duration", 1.7F);
    set("window:options:liquid:strength", 1.2F);
    set("window:options:cloth:flutter", 0.8F);
    set("window:options:bubble:duration", 1.4F);
    c = readConfig(values);
    assert(near(c.paper.open.duration, 1.7) && near(c.paper.close.duration, 1.7));
    assert(near(c.liquid.open.strength, 1.2) && near(c.liquid.close.strength, 1.2));
    assert(near(c.liquid.movement.strength, 1.2));
    assert(near(c.cloth.open.flutter, 0.8) && near(c.cloth.close.flutter, 0.8));
    assert(near(c.cloth.movement.flutter, 0.8));
    assert(near(c.bubble.open.duration, 1.4) && near(c.bubble.close.duration, 1.4));

    // Non-numeric styles and direction-specific options inherit too.
    set("window:options:bubble:shape", std::string("circle"));
    set("window:options:liquid:color", 0xff123456U);
    set("window:options:liquid:splash", false);
    set("window:options:swarm:lone_window_sides", std::string("top,left"));
    set("window:options:liquid:default_animation_edge", std::string("top"));
    c = readConfig(values);
    assert(c.bubble.movement.shape == Shape::Circle);
    assert(c.liquid.open.color == 0xff123456U && c.liquid.close.color == 0xff123456U);
    assert(c.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(!c.liquid.close.splash);
    assert(c.swarm.open.loneWindowSides.has(LoneWindowSides::Top));
    assert(!c.swarm.open.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(c.swarm.close.loneWindowSides.has(LoneWindowSides::Left));
    set("window:close:options:liquid:splash", true);
    set("window:open:options:swarm:lone_window_sides", std::string("all"));
    set("window:close:options:liquid:default_animation_edge", std::string("right"));
    c = readConfig(values);
    assert(c.liquid.close.splash);
    assert(c.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Right);
    assert(c.swarm.open.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(!c.swarm.close.loneWindowSides.has(LoneWindowSides::Bottom));

    // Liquid and cloth choose opening origins and closing exits independently.
    set("window:open:options:liquid:edge_bounds", std::string("work_area"));
    set("window:open:options:cloth:default_animation_edge", std::string("left"));
    set("window:open:options:cloth:edge_bounds", std::string("screen"));
    set("window:close:options:liquid:side", std::string("right"));
    set("window:close:options:liquid:edge_bounds", std::string("work_area"));
    set("window:close:options:cloth:side", std::string("origin"));
    set("window:close:options:cloth:default_animation_edge", std::string("top"));
    set("window:close:options:cloth:edge_bounds", std::string("screen"));
    c = readConfig(values);
    assert(c.liquid.open.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.liquid.open.edgeBounds == EdgeBounds::WorkArea);
    assert(c.cloth.open.defaultAnimationEdge == DefaultAnimationEdge::Left);
    assert(c.cloth.open.edgeBounds == EdgeBounds::Screen);
    assert(c.liquid.close.side == Side::Right);
    assert(c.liquid.close.defaultAnimationEdge == DefaultAnimationEdge::Right);
    assert(c.liquid.close.edgeBounds == EdgeBounds::WorkArea);
    assert(c.cloth.close.side == Side::Origin);
    assert(c.cloth.close.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.cloth.close.edgeBounds == EdgeBounds::Screen);

    // Explicit values equal to old defaults still take precedence over global settings.
    set("window:open:options:paper:duration", 0.6F);
    set("window:close:options:paper:duration", 0.5F);
    set("window:movement:options:liquid:strength", 0.65F);
    set("window:close:options:cloth:flutter", 0.2F);
    c = readConfig(values);
    assert(near(c.paper.open.duration, 0.6) && near(c.paper.close.duration, 0.5));
    assert(near(c.liquid.movement.strength, 0.65));
    assert(near(c.liquid.open.strength, 1.2));
    assert(near(c.cloth.close.flutter, 0.2) && near(c.cloth.open.flutter, 0.8));

    set("window:options:swarm:count", 123LL);
    set("window:options:swarm:speed", 2100.0F);
    set("window:open:options:swarm:count", 250LL);
    set("window:close:options:swarm:speed", 900.0F);
    set("window:options:swarm:sprite", std::string("/tmp/custom sprite.png"));
    set("window:open:options:swarm:sprite", std::string("/tmp/open.png"));
    set("window:options:swarm:agility", 0.8F);
    set("window:movement:options:swarm:agility", 0.5F);
    c = readConfig(values);
    assert(c.swarm.open.count == 250 && c.swarm.close.count == 123 && c.swarm.movement.count == 123);
    assert(near(c.swarm.open.speed, 2100) && near(c.swarm.close.speed, 900));
    assert(near(c.swarm.movement.speed, 2100));
    assert(c.swarm.open.sprite == "/tmp/open.png" && c.swarm.close.sprite == "/tmp/custom sprite.png");
    assert(c.swarm.movement.sprite == "/tmp/custom sprite.png");
    assert(near(c.swarm.open.agility, 0.8) && near(c.swarm.close.agility, 0.8));
    assert(near(c.swarm.movement.agility, 0.5));
}
