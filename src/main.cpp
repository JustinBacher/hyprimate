#include <hyprland/src/plugins/PluginAPI.hpp>

#include "PluginRuntime.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr char MISMATCH_MESSAGE[] =
    "[liquid-glass] Hyprland/header version mismatch; rebuild against the running compositor";

// Owned only while the plugin is loaded. Constructed after every compatibility check passes.
std::unique_ptr<CPluginRuntime> g_runtime;
} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash())) {
        HyprlandAPI::addNotification(handle, MISMATCH_MESSAGE, CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
        throw std::runtime_error(MISMATCH_MESSAGE);
    }

    PLUGIN_DESCRIPTION_INFO description{
        .name = "liquid-glass",
        .description = "Viscous window deformation during movement, opening, and closing",
        .author = "",
        .version = "0.1.0",
    };
    // Publish only after construction succeeds. The host unregisters config values
    // on failed initialization; member RAII releases all partially added listeners.
    auto runtime = std::make_unique<CPluginRuntime>(handle);
    g_runtime = std::move(runtime);
    return description;
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Destroying the runtime unregisters all listeners, then detaches every tracked window.
    g_runtime.reset();
}
