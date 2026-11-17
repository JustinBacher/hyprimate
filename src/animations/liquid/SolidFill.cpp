#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <drm_fourcc.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "animations/liquid/SolidFill.hpp"

namespace {
    constexpr int SOLID_SIZE = 4;

    uint8_t toByte(float unit) noexcept {
        return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.F, 1.F) * 255.F));
    }

    bool sameColor(const CHyprColor &a, const CHyprColor &b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
}  // namespace

SP<Render::ITexture> CSolidFill::texture() {
    const uint8_t ALPHA = toByte(m_color.a);
    if (ALPHA == 0 || !g_pHyprRenderer) {
        return nullptr;
    }
    if (m_texture && sameColor(m_built, m_color)) {
        return m_texture;
    }

    // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
    const auto RED = static_cast<uint8_t>(toByte(m_color.r) * ALPHA / 255U);
    const auto GREEN = static_cast<uint8_t>(toByte(m_color.g) * ALPHA / 255U);
    const auto BLUE = static_cast<uint8_t>(toByte(m_color.b) * ALPHA / 255U);
    std::array<uint8_t, SOLID_SIZE * SOLID_SIZE * 4> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = BLUE;
        pixels[i + 1] = GREEN;
        pixels[i + 2] = RED;
        pixels[i + 3] = ALPHA;
    }

    m_texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        pixels.data(),
        SOLID_SIZE * 4,
        Vector2D{SOLID_SIZE, SOLID_SIZE},
        false,
        ALPHA == 255);
    m_built = m_color;
    return m_texture;
}
