#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "animations/bubble/BubbleCurl.hpp"

namespace hyprimate {

    constexpr std::size_t kBubbleMaxSheets = 7;  // Six pairwise necks and the final island.

    // A finite sector of the original rolled film, not an independently spawned sprite.
    struct BubbleSheet {
        std::uint32_t id = 0;
        std::size_t hole = 0;
        double birth = 0;  // Surface clock at detachment.
        double age = 0, life = 0;  // Seconds.
        double phi = 0, halfAngle = 0;
        std::array<double, kBubbleHoles> radii{}, onsets{};  // Hole geometry frozen at birth.
        Vec3 pivot{}, axis{}, translation{};
        double rotation = 0, scale = 1;
    };

    // Includes expired sheets (scale == 0) until the transition ends: their material must never
    // reattach.
    std::size_t bubbleSheets(const BubbleSurface &, std::span<BubbleSheet>);
    BubbleCurlSample bubbleSheetSample(
        const BubbleSurface &, const BubbleSheet &, double phi, double u);
    // Material coordinates: logical pixels on flat film, a unit vector on spherical film.
    bool bubbleSheetContains(const BubbleSurface &, const BubbleSheet &, Vec3 material);

}  // namespace hyprimate
