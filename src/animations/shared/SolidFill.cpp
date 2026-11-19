#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <drm_fourcc.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "animations/shared/SolidFill.hpp"

namespace {
    constexpr int SOLID_SIZE = 4;

    uint8_t to_byte(float unit) noexcept {
        return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.F, 1.F) * 255.F));
    }

    bool same_color(const CHyprColor &first, const CHyprColor &second) noexcept {
        return first.r == second.r && first.g == second.g && first.b == second.b &&
               first.a == second.a;
    }
}  // namespace

SP<Render::ITexture> CSolidFill::texture() {
    const uint8_t ALPHA = to_byte(static_cast<float>(m_color.a));
    if (ALPHA == 0 || !g_pHyprRenderer) {
        return nullptr;
    }
    if (m_texture && same_color(m_built, m_color)) {
        return m_texture;
    }

    // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
    const auto RED = static_cast<uint8_t>(to_byte(static_cast<float>(m_color.r)) * ALPHA / 255U);
    const auto GREEN = static_cast<uint8_t>(to_byte(static_cast<float>(m_color.g)) * ALPHA / 255U);
    const auto BLUE = static_cast<uint8_t>(to_byte(static_cast<float>(m_color.b)) * ALPHA / 255U);
    std::array<uint8_t, static_cast<std::size_t>(SOLID_SIZE) * SOLID_SIZE * 4> pixels{};
    const auto BYTES = std::span(pixels);
    for (auto pixel = BYTES.begin(); pixel != BYTES.end();) {
        *pixel = BLUE;
        ++pixel;
        *pixel = GREEN;
        ++pixel;
        *pixel = RED;
        ++pixel;
        *pixel = ALPHA;
        ++pixel;
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
