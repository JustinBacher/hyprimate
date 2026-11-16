#pragma once

#include "animations/bubble/Bubble.hpp"

namespace hyprimate::detail {
    struct IdentifiedParticle {
        BubbleParticle particle{};
        std::uint32_t id = 0;
    };
    std::size_t bubbleParticlesWithIds(
        const BubbleSurface &surface, std::span<IdentifiedParticle> out);
}  // namespace hyprimate::detail
