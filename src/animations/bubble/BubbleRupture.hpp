#pragma once

#include "animations/bubble/Bubble.hpp"

namespace liquid_glass::detail {
struct IdentifiedParticle {
    BubbleParticle particle{};
    std::uint32_t id = 0;
};
std::size_t bubbleParticlesWithIds(const BubbleSurface &surface, std::span<IdentifiedParticle> out);
} // namespace liquid_glass::detail
