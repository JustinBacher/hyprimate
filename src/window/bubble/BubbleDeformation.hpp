#pragma once

#include <array>
#include <cstddef>

#include "shared/Core.hpp"

namespace hyprimate {
    // Membrane swellings drawn at once.
    inline constexpr std::size_t K_BUBBLE_BUDS = 4;

    // A swelling of the membrane where a microbubble grows out of it or still hangs on its neck.
    // Positions are in the contracted membrane's coordinates (bubbleRoundPoint), logical px.
    struct BubbleBud {
        Vec2 foot{};
        double height = 0;  // outward displacement at the foot, px; 0: none
        double width = 0;  // Gaussian radius of the swelling, px; height stays below 0.55 width
    };

    // The movement's share of a flat membrane's shape, in logical px. The renderer carries every
    // point of the undeformed membrane by bubble_motion_point; the GPU material mirrors it.
    struct BubbleDeformation {
        // The leading face flattens against the air by a fixed share of the trailing lag.
        Vec2 drag{};
        std::array<double, 4> wave_cos{}, wave_sin{};  // outward capillary modes n = 2..5
        double sheen = 0;  // 0..1 interference colour on the stressed film near the outline
        // How far surface tension has pulled the membrane toward its moving shape: 0 its
        // rectangle, 1 the elliptical-grid image of the rectangle on the ellipse of half extents
        // `extent`; slightly negative while it overshoots back.
        double round = 0;
        Vec2 extent{};
        // Where the film is drawn relative to the window: a layout move animates the window faster
        // than a bubble travels, so the film follows it from where it was.
        Vec2 carry{};
        std::array<BubbleBud, K_BUBBLE_BUDS> buds{};

        [[nodiscard]] bool still() const noexcept;
    };

    // The membrane's half size at the given roundness: its rectangle's, moving toward `extent`.
    Vec2 bubble_round_half(Vec2 half_size, const BubbleDeformation &deformation);
    // Where contraction carries a point of the rectangle (logical px from its centre, y down): its
    // position on the ellipse of half extents `extent` under the elliptical-grid square-to-disc
    // map, mixed in by `round`. The corners pull in first, the edges bow outward, and the content
    // goes with them.
    Vec2 bubble_round_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Displacement of a point of the (possibly rounded) membrane, whose half size is half_size.
    // The leading face flattens, the trailing film lags and narrows across the motion, the
    // capillary modes ripple the outline, and buds swell it locally.
    Vec2 bubble_motion_offset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Where movement carries a point of the undeformed membrane: rounded, displaced, then carried.
    Vec2 bubble_motion_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
}  // namespace hyprimate
