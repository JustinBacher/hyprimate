#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "window/bubble/BubbleCurl.hpp"

namespace hyprimate {

    constexpr std::size_t K_BUBBLE_MAX_SHEETS = 7;  // Six pairwise necks and the final island.

    // A finite sector of the original rolled film, not an independently spawned sprite.
    struct BubbleSheet {
        std::uint32_t id = 0;
        std::size_t hole = 0;
        double birth = 0;  // Surface clock at detachment.
        double age = 0, life = 0;  // Seconds.
        double phi = 0, half_angle = 0;
        std::array<double, K_BUBBLE_HOLES> radii{}, onsets{};  // Hole geometry frozen at birth.
        Vec3 pivot{}, axis{}, translation{};
        double rotation = 0, scale = 1;
    };

    // Includes expired sheets (scale == 0) until the transition ends: their material must never
    // reattach.
    std::size_t bubble_sheets(const BubbleSurface &surface, std::span<BubbleSheet> out);
    BubbleCurlSample bubble_sheet_sample(
        const BubbleSurface &surface, const BubbleSheet &sheet, double phi, double fraction);
    // Material coordinates: logical pixels on flat film, a unit vector on spherical film.
    bool bubble_sheet_contains(
        const BubbleSurface &surface, const BubbleSheet &sheet, Vec3 material);

}  // namespace hyprimate
