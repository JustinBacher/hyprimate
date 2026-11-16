// Exercise the generated configuration without requiring a running compositor.
#include <any>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
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
    // Unset globals must retain each transition's historical default.
    assert(near(c.open.paper.duration, 0.6));
    assert(near(c.close.paper.duration, 0.5));
    assert(near(c.open.cloth.duration, 1.0));
    assert(near(c.close.cloth.duration, 0.9));
    assert(near(c.open.bubble.duration, 0.62));
    assert(near(c.close.bubble.duration, 0.42));
    assert(near(c.movement.liquid.strength, 0.65));

    set("window:options:paper:duration", 1.7F);
    set("window:options:liquid:strength", 1.2F);
    set("window:options:cloth:flutter", 0.8F);
    set("window:options:bubble:duration", 1.4F);
    c = readConfig(values);
    assert(near(c.open.paper.duration, 1.7) && near(c.close.paper.duration, 1.7));
    assert(near(c.open.liquid.strength, 1.2) && near(c.close.liquid.strength, 1.2));
    assert(near(c.movement.liquid.strength, 1.2));
    assert(near(c.open.cloth.flutter, 0.8) && near(c.close.cloth.flutter, 0.8));
    assert(near(c.movement.cloth.flutter, 0.8));
    assert(near(c.open.bubble.duration, 1.4) && near(c.close.bubble.duration, 1.4));

    // Non-numeric styles and direction-specific options inherit too.
    set("window:options:bubble:shape", std::string("circle"));
    set("window:options:liquid:color", 0xff123456U);
    set("window:options:liquid:splash", false);
    set("window:options:swarm:lone_window_sides", std::string("top,left"));
    set("window:options:liquid:default_animation_edge", std::string("top"));
    c = readConfig(values);
    assert(c.movement.bubble.shape == Shape::Circle);
    assert(c.open.liquid.color == 0xff123456U && c.close.liquid.color == 0xff123456U);
    assert(c.open.liquid.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.close.liquid.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(!c.close.liquid.splash);
    assert(c.open.swarm.loneWindowSides.has(LoneWindowSides::Top));
    assert(!c.open.swarm.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(c.close.swarm.loneWindowSides.has(LoneWindowSides::Left));
    set("window:close:options:liquid:splash", true);
    set("window:open:options:swarm:lone_window_sides", std::string("all"));
    set("window:close:options:liquid:default_animation_edge", std::string("right"));
    c = readConfig(values);
    assert(c.close.liquid.splash);
    assert(c.open.liquid.defaultAnimationEdge == DefaultAnimationEdge::Top);
    assert(c.close.liquid.defaultAnimationEdge == DefaultAnimationEdge::Right);
    assert(c.open.swarm.loneWindowSides.has(LoneWindowSides::Bottom));
    assert(!c.close.swarm.loneWindowSides.has(LoneWindowSides::Bottom));

    // Explicit values equal to old defaults still take precedence over global settings.
    set("window:open:options:paper:duration", 0.6F);
    set("window:close:options:paper:duration", 0.5F);
    set("window:movement:options:liquid:strength", 0.65F);
    set("window:close:options:cloth:flutter", 0.2F);
    c = readConfig(values);
    assert(near(c.open.paper.duration, 0.6) && near(c.close.paper.duration, 0.5));
    assert(near(c.movement.liquid.strength, 0.65));
    assert(near(c.open.liquid.strength, 1.2));
    assert(near(c.close.cloth.flutter, 0.2) && near(c.open.cloth.flutter, 0.8));

    set("window:options:swarm:count", 123LL);
    set("window:options:swarm:speed", 2100.0F);
    set("window:open:options:swarm:count", 250LL);
    set("window:close:options:swarm:speed", 900.0F);
    set("window:options:swarm:sprite", std::string("cat"));
    set("window:open:options:swarm:sprite", std::string("spider"));
    set("window:options:swarm:agility", 0.8F);
    set("window:movement:options:swarm:agility", 0.5F);
    c = readConfig(values);
    assert(c.open.swarm.count == 250 && c.close.swarm.count == 123 && c.movement.swarm.count == 123);
    assert(near(c.open.swarm.speed, 2100) && near(c.close.swarm.speed, 900));
    assert(near(c.movement.swarm.speed, 2100));
    assert(c.open.swarm.sprite == Sprite::Spider && c.close.swarm.sprite == Sprite::Cat);
    assert(c.movement.swarm.sprite == Sprite::Cat);
    assert(near(c.open.swarm.agility, 0.8) && near(c.close.swarm.agility, 0.8));
    assert(near(c.movement.swarm.agility, 0.5));
}
