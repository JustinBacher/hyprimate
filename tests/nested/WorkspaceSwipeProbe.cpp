#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <string>

#include <lua.hpp>

#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/UnifiedWorkspaceSwipeGesture.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
    using Clock = std::chrono::steady_clock;

    uint64_t monotonic_milliseconds() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    }

    bool gesture_in_progress() {
        return g_pUnifiedWorkspaceSwipe && g_pUnifiedWorkspaceSwipe->isGestureInProgress();
    }

    void diagnostic(const char *when, const char *phase, double dx, double dy, bool inProgress) {
        std::fprintf(stderr,
            "workspace-swipe-probe %s ms=%llu phase=%s dx=%.17g dy=%.17g in_progress=%s\n",
            when, static_cast<unsigned long long>(monotonic_milliseconds()), phase, dx, dy,
            inProgress ? "true" : "false");
        std::fflush(stderr);
    }
}  // namespace

int swipe_event(lua_State *L) {
    if (lua_gettop(L) != 3 || lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "event expects phase, dx, dy");
    }

    const char *phase = lua_tostring(L, 1);
    if (std::strcmp(phase, "begin") != 0 && std::strcmp(phase, "update") != 0 &&
        std::strcmp(phase, "end") != 0) {
        return luaL_error(L, "phase must be begin, update, or end");
    }

    double dx = 0.0;
    double dy = 0.0;
    if (std::strcmp(phase, "update") == 0) {
        if (lua_type(L, 2) != LUA_TNUMBER || lua_type(L, 3) != LUA_TNUMBER) {
            return luaL_error(L, "update requires numeric dx and dy");
        }
        dx = lua_tonumber(L, 2);
        dy = lua_tonumber(L, 3);
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            return luaL_error(L, "update requires finite dx and dy");
        }
    }

    if (!g_pInputManager) {
        return luaL_error(L, "input manager is unavailable");
    }

    const auto timeMs = static_cast<uint32_t>(monotonic_milliseconds());
    const bool before = gesture_in_progress();
    diagnostic("entry", phase, dx, dy, before);
    if (std::strcmp(phase, "begin") == 0) {
        g_pInputManager->onSwipeBegin(IPointer::SSwipeBeginEvent{.timeMs = timeMs, .fingers = 3});
    } else if (std::strcmp(phase, "update") == 0) {
        g_pInputManager->onSwipeUpdate(IPointer::SSwipeUpdateEvent{
            .timeMs = timeMs, .fingers = 3, .delta = {dx, dy}});
    } else {
        g_pInputManager->onSwipeEnd(IPointer::SSwipeEndEvent{.timeMs = timeMs, .cancelled = false});
    }
    const bool result = gesture_in_progress();
    diagnostic("return", phase, dx, dy, result);
    lua_pushboolean(L, result);
    return 1;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash())) {
        throw std::runtime_error("Hyprland/header version mismatch; rebuild against the running compositor");
    }
    if (!HyprlandAPI::addLuaFunction(handle, "workspace_swipe_test", "event", &swipe_event)) {
        throw std::runtime_error("Failed to register workspace swipe probe Lua function");
    }
    return {"workspace-swipe-probe", "workspace swipe runtime diagnostics", "Hyprimate test", "1"};
}

APICALL EXPORT void PLUGIN_EXIT() {}
