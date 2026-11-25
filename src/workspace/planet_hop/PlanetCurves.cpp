#include <cmath>
#include <string_view>
#include <utility>

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <lua.hpp>

#include "workspace/planet_hop/PlanetCurves.hpp"
#include "workspace/planet_hop/PlanetSpring.hpp"

namespace hyprimate::workspace::planet_hop {
    PlanetCurves *PlanetCurves::s_owner = nullptr;

    bool PlanetCurves::install(void *handle, std::function<void()> changed) {
        if (!HyprlandAPI::addLuaFunction(handle, "planet_hop", "curve", &define)) {
            return false;
        }
        s_owner = this;
        m_changed = std::move(changed);
        m_pre_reload =
            Event::bus()->m_events.config.preReload.listen([this] { m_springs.clear(); });
        return true;
    }

    PlanetCurves::~PlanetCurves() {
        if (s_owner == this) {
            s_owner = nullptr;
        }
    }

    int PlanetCurves::define(lua_State *state) {
        if (s_owner == nullptr) {
            return luaL_error(state, "planet_hop is not loaded");
        }
        std::size_t name_length = 0;
        const char *name = luaL_checklstring(state, 1, &name_length);
        const std::string_view NAME(name, name_length);
        luaL_argcheck(state, !NAME.empty(), 1, "empty curve name");
        luaL_argcheck(state, NAME.find('\0') == std::string_view::npos, 1, "invalid curve name");
        luaL_checktype(state, 2, LUA_TTABLE);
        lua_getfield(state, 2, "type");
        std::size_t type_length = 0;
        const char *type = luaL_checklstring(state, -1, &type_length);
        luaL_argcheck(
            state, std::string_view(type, type_length) == "spring", 2, "expected type = 'spring'");
        lua_pop(state, 1);
        const auto NUMBER = [state](const char *field, float fallback, bool allow_zero) {
            lua_getfield(state, 2, field);
            const auto VALUE = static_cast<float>(luaL_optnumber(state, -1, fallback));
            lua_pop(state, 1);
            if (!std::isfinite(VALUE) || (allow_zero ? VALUE < 0 : VALUE <= 0)) {
                luaL_error(state, "invalid spring parameter: %s", field);
            }
            return VALUE;
        };
        Hyprutils::Animation::SSpringCurve spring;
        spring.mass = NUMBER("mass", spring.mass, false);
        spring.stiffness = NUMBER("stiffness", spring.stiffness, false);
        spring.damping = NUMBER("damping", spring.damping, true);
        s_owner->m_springs.insert_or_assign(std::string(NAME), spring);
        s_owner->m_changed();
        return 0;
    }

    PlanetCurves::Selection PlanetCurves::resolve(const std::string &name, double seconds) const {
        constexpr std::string_view PREFIX = "spring:";
        const bool EXPLICIT = name.starts_with(PREFIX);
        const std::string SUFFIX = EXPLICIT ? name.substr(PREFIX.size()) : std::string{};
        const auto &key = EXPLICIT ? SUFFIX : name;
        if (!EXPLICIT) {
            if (const auto LOCAL = m_springs.find(name); LOCAL != m_springs.end()) {
                auto curve = spring_curve(LOCAL->second, seconds);
                if (curve) {
                    return {Selection::Kind::Spring, std::move(curve)};
                }
                auto &manager = Animation::mgr();
                return manager && manager->bezierExists(name)
                           ? Selection{Selection::Kind::Bezier, {}}
                           : Selection{};
            }
        }
        auto &manager = Animation::mgr();
        if (!manager) {
            return {};
        }
        if (!EXPLICIT && manager->bezierExists(name)) {
            return {Selection::Kind::Bezier, {}};
        }
        if (!manager->springExists(key)) {
            return {};
        }
        auto curve = spring_curve(*manager->getSpring(key), seconds);
        return curve ? Selection{Selection::Kind::Spring, std::move(curve)} : Selection{};
    }
}  // namespace hyprimate::workspace::planet_hop
