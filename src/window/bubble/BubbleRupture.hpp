#pragma once

#include "window/bubble/Bubble.hpp"

namespace hyprimate::detail {
    struct IdentifiedParticle {
        BubbleParticle particle{};
        std::uint32_t id = 0;
    };
    std::size_t bubble_particles_with_ids(
        const BubbleSurface &surface, std::span<IdentifiedParticle> out);
}  // namespace hyprimate::detail
