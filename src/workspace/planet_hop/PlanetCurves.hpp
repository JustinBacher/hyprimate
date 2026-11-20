#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprutils/animation/AnimationManager.hpp>

#include "workspace/WorkspaceTransition.hpp"

struct lua_State;

namespace hyprimate::workspace::planet_hop {
    class PlanetCurves {
      public:
        bool install(void *handle, std::function<void()> changed);
        ~PlanetCurves();
        PlanetCurves() = default;
        PlanetCurves(const PlanetCurves &) = delete;
        PlanetCurves &operator=(const PlanetCurves &) = delete;
        PlanetCurves(PlanetCurves &&) = delete;
        PlanetCurves &operator=(PlanetCurves &&) = delete;

        [[nodiscard]] Transition::Curve resolve(const std::string &name, double seconds) const;

      private:
        static int define(lua_State *state);
        // addLuaFunction takes a plain C function, without an instance context.
        static PlanetCurves *s_owner;
        std::unordered_map<std::string, Hyprutils::Animation::SSpringCurve> m_springs;
        std::function<void()> m_changed;
        CHyprSignalListener m_pre_reload;
    };
}  // namespace hyprimate::workspace::planet_hop
