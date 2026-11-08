#include "SolidFill.hpp"

#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <drm_fourcc.h>

namespace {
    constexpr int SOLID_SIZE = 4;

    uint8_t toByte(float unit) noexcept {
        return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.F, 1.F) * 255.F));
    }

    bool sameColor(const CHyprColor& a, const CHyprColor& b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
}

SP<Render::ITexture> CSolidFill::texture() {
    const uint8_t alpha = toByte(m_color.a);
    if (alpha == 0 || !g_pHyprRenderer)
        return nullptr;
    if (m_texture && sameColor(m_built, m_color))
        return m_texture;

    // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
    const uint8_t                          red   = static_cast<uint8_t>(toByte(m_color.r) * alpha / 255U);
    const uint8_t                          green = static_cast<uint8_t>(toByte(m_color.g) * alpha / 255U);
    const uint8_t                          blue  = static_cast<uint8_t>(toByte(m_color.b) * alpha / 255U);
    std::array<uint8_t, SOLID_SIZE * SOLID_SIZE * 4> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = blue;
        pixels[i + 1] = green;
        pixels[i + 2] = red;
        pixels[i + 3] = alpha;
    }

    m_texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888, pixels.data(), SOLID_SIZE * 4, Vector2D{SOLID_SIZE, SOLID_SIZE}, false,
                                               alpha == 255);
    m_built   = m_color;
    return m_texture;
}
