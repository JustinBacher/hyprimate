#pragma once

#include <array>
#include <string_view>

#include <hyprland/src/render/Texture.hpp>

class CWLSurfaceResource;

namespace hyprimate::shader {
    struct OriginalWallpaper {
        SP<Render::ITexture> texture;
        // texture UV = bias + (top-first normalized output UV * scale).
        std::array<float, 4> uv{};
        // External root identity, independent of its current buffer. Empty for stock.
        WP<CWLSurfaceResource> owner;

        explicit operator bool() const noexcept;
    };

    // Resolve again at each synchronous draw; do not retain the texture across draws.
    // An empty namespace selects only the compositor's stock wallpaper.
    OriginalWallpaper original_wallpaper(
        const PHLMONITOR &monitor, std::string_view source_namespace);
}  // namespace hyprimate::shader
