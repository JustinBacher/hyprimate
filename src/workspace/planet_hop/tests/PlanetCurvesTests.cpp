#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <lua.hpp>

#include "Check.hpp"
#include "workspace/planet_hop/PlanetCurves.hpp"

// Hyprland headers instantiate these globals before main; no compositor is present.
Log::CLogger::CLogger() = default;
CHyprColor::CHyprColor(float red, float green, float blue, float alpha)
    : r(red), g(green), b(blue), a(alpha) {
}

namespace Animation {
    CHyprAnimationManager::CHyprAnimationManager() = default;
    void CHyprAnimationManager::scheduleTick() {
    }
    void CHyprAnimationManager::onTicked() {
    }
    UP<CHyprAnimationManager> &mgr() {
        static UP<CHyprAnimationManager> manager = makeUnique<CHyprAnimationManager>();
        return manager;
    }
}  // namespace Animation

namespace {
    std::vector<std::string> bezier_queries;
}
using Manager = Hyprutils::Animation::CAnimationManager;
extern "C" bool real_bezier(Manager *, const std::string &) asm(
    "__real__ZN9Hyprutils9Animation17CAnimationManager12bezierExistsERKNSt7__cxx1112basic_"
    "stringIcSt11char_traitsIcESaIcEEE");
extern "C" bool wrapped_bezier(Manager *, const std::string &) asm(
    "__wrap__ZN9Hyprutils9Animation17CAnimationManager12bezierExistsERKNSt7__cxx1112basic_"
    "stringIcSt11char_traitsIcESaIcEEE");
bool wrapped_bezier(Manager *manager, const std::string &name) {
    bezier_queries.push_back(name);
    return real_bezier(manager, name);
}

namespace {
    using hyprimate::workspace::planet_hop::PlanetCurves;
    using Spring = Hyprutils::Animation::SSpringCurve;

    // Exercise the registered Lua callback without installing a compositor plugin.
    template <class Tag, typename Tag::type Member>
    struct Access {
        friend typename Tag::type get(Tag) {
            return Member;
        }
    };
    struct Define {
        using type = int (*)(lua_State *);
        friend type get(Define);
    };
    struct Owner {
        using type = PlanetCurves **;
        friend type get(Owner);
    };
    struct Springs {
        using type = std::unordered_map<std::string, Spring> PlanetCurves::*;
        friend type get(Springs);
    };
    struct Changed {
        using type = std::function<void()> PlanetCurves::*;
        friend type get(Changed);
    };
    template struct Access<Define, &PlanetCurves::define>;
    template struct Access<Owner, &PlanetCurves::s_owner>;
    template struct Access<Springs, &PlanetCurves::m_springs>;
    template struct Access<Changed, &PlanetCurves::m_changed>;

    int define(
        lua_State *state, const std::string &name, const std::string &type, float stiffness) {
        lua_pushcfunction(state, get(Define{}));
        lua_pushlstring(state, name.data(), name.size());
        lua_newtable(state);
        lua_pushlstring(state, type.data(), type.size());
        lua_setfield(state, -2, "type");
        lua_pushnumber(state, stiffness);
        lua_setfield(state, -2, "stiffness");
        const int result = lua_pcall(state, 2, 0, 0);
        if (result != LUA_OK) {
            lua_pop(state, 1);
        }
        return result;
    }
}  // namespace

int main() {
    lua_State *state = luaL_newstate();
    CHECK(state != nullptr);
    if (state == nullptr) {
        return 1;
    }
    PlanetCurves curves;
    *get(Owner{}) = &curves;
    int changes = 0;
    curves.*get(Changed{}) = [&changes] { ++changes; };
    CHECK(define(state, "orbit", "spring", 100) == LUA_OK);
    const auto &springs = curves.*get(Springs{});
    CHECK(springs.contains("orbit"));
    using Kind = PlanetCurves::Selection::Kind;
    auto &manager = Animation::mgr();
    manager->addBezierWithName("spring:missing", {0, 0}, {1, 1});
    manager->addBezierWithName("orbit", {0, 0}, {1, 1});
    manager->addBezierWithName("native", {0, 0}, {1, 1});
    manager->addSpringWithName("native", Spring{});
    manager->addSpringWithName("global", Spring{});
    bezier_queries.clear();
    CHECK(curves.resolve("spring:missing", 1).kind == Kind::Invalid);
    CHECK(bezier_queries.empty());
    CHECK(curves.resolve("orbit", 1).kind == Kind::Spring);
    CHECK(bezier_queries.empty());
    CHECK(curves.resolve("native", 1).kind == Kind::Bezier);
    CHECK(bezier_queries.size() == 1 && bezier_queries.back() == "native");
    bezier_queries.clear();
    CHECK(curves.resolve("spring:global", 1).kind == Kind::Spring);
    CHECK(bezier_queries.empty());
    CHECK(curves.resolve("global", 1).kind == Kind::Spring);
    CHECK(bezier_queries.size() == 1 && bezier_queries.back() == "global");
    CHECK(define(state, std::string("orbit\0alternate", 15), "spring", 400) != LUA_OK);
    CHECK(springs.contains("orbit") && springs.at("orbit").stiffness == 100);
    CHECK(changes == 1);
    CHECK(define(state, "other", std::string("spring\0other", 12), 200) != LUA_OK);
    CHECK(!springs.contains("other"));
    CHECK(changes == 1);
    lua_close(state);
    return g_failures == 0 ? 0 : 1;
}
