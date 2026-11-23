// Linked only into a disposable negative-control relink. Nothing here is compiled into the plugin.
#include <cstdio>
#include <optional>

#include <hyprland/src/render/Renderer.hpp>

using namespace Render;

// Same ABI as the host's nonvirtual member (implicit `this` follows the return storage).
extern "C" SP<IFramebuffer> deny_construction_work_buffer(IHyprRenderer *, CRenderContext &,
    std::optional<Vector2D>) asm(CONSTRUCTION_WRAPPED_WORK_BUFFER);
extern "C" SP<IFramebuffer> deny_construction_work_buffer(IHyprRenderer *, CRenderContext &, std::optional<Vector2D>) {
    static bool reported = false;
    if (!reported) {
        std::fputs("[construction-fixture] work buffer denied\n", stderr);
        reported = true;
    }
    return nullptr;
}
