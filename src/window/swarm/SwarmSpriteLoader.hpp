#pragma once

#include <string_view>

#include <cairo/cairo.h>

namespace hyprimate {
    // The caller owns the returned Cairo surface, including an error surface.
    // Only missing built-ins fall back, and only when allow_embedded is true.
    [[nodiscard]] cairo_surface_t *load_swarm_sprite(
        std::string_view name, std::string_view directory, bool allow_embedded);
}
