#include <cstring>
#include <span>
#include <string_view>

#include <cairo/cairo.h>

#include "window/swarm/SwarmEmbeddedSprites.hpp"
#include "window/swarm/SwarmSprite.hpp"
#include "window/swarm/SwarmSpriteLoader.hpp"

namespace {
    cairo_status_t read_sprite(void *closure, unsigned char *data, unsigned int length) {
        auto &remaining = *static_cast<std::span<const unsigned char> *>(closure);
        if (length > remaining.size()) return CAIRO_STATUS_READ_ERROR;
        std::memcpy(data, remaining.data(), length);
        remaining = remaining.subspan(length);
        return CAIRO_STATUS_SUCCESS;
    }
}

namespace hyprimate {
    cairo_surface_t *load_swarm_sprite(std::string_view name, std::string_view directory, bool allow_embedded) {
        const auto PATH = sprite_path(SpriteName{name}, SpriteDirectory{directory});
        auto *surface = cairo_image_surface_create_from_png(PATH.c_str());
        if (cairo_surface_status(surface) != CAIRO_STATUS_FILE_NOT_FOUND) return surface;
        auto encoded = embedded_swarm_sprite(name, allow_embedded);
        if (encoded.empty()) return surface;
        cairo_surface_destroy(surface);
        return cairo_image_surface_create_from_png_stream(read_sprite, &encoded);
    }
}
