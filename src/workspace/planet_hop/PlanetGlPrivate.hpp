#pragma once

#include <algorithm>
#include <cmath>

#include "workspace/planet_hop/PlanetGlRenderer.hpp"

namespace hyprimate::workspace::detail {
    inline float capture_mix(const PlanetLayerTextures &layer) {
        return layer.previous_image != 0 && std::isfinite(layer.live_mix)
                   ? std::clamp(layer.live_mix, 0.0F, 1.0F)
                   : 1.0F;
    }

    void cache_program_uniforms(GLuint planet,
        std::array<GLint, 48> &planet_uniforms,
        GLuint quad,
        std::array<GLint, 48> &quad_uniforms,
        GLuint sky,
        std::array<GLint, 48> &sky_uniforms);
}  // namespace hyprimate::workspace::detail
