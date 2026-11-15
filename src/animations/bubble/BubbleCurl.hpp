#pragma once

#include "animations/bubble/Bubble.hpp"

#include <algorithm>
#include <cmath>

namespace liquid_glass {

struct BubbleCurlSection {
    double width = 0, turn = 0;
};

struct BubbleCurlSample {
    Vec3 position{}, normal{};
};

inline BubbleCurlSection bubbleCurlSection(const BubbleSurface &surface, const BubbleHole &hole, double physicalRadius,
                                           double phi) {
    return {
        std::max(0.0, std::min({48.0, .18 * std::min(surface.halfSize.x, surface.halfSize.y), .45 * physicalRadius})) *
            std::clamp(surface.aureole, 0.0, 1.0),
        2.7 + .65 * std::sin(3 * phi + hole.phase[0]) + .35 * std::sin(7 * phi + hole.phase[1])};
}

inline BubbleCurlSample bubbleCurlSample(Vec3 base, Vec3 outward, Vec3 normal, BubbleCurlSection section, double u) {
    const double theta = u * section.turn;
    const double a = section.turn != 0 ? section.width / section.turn : 0;
    const double along = -a * std::sin(theta), lift = a * (1 - std::cos(theta));
    const double c = std::cos(theta), s = std::sin(theta);
    return {{base.x + outward.x * along + normal.x * lift, base.y + outward.y * along + normal.y * lift,
             base.z + outward.z * along + normal.z * lift},
            {normal.x * c + outward.x * s, normal.y * c + outward.y * s, normal.z * c + outward.z * s}};
}

} // namespace liquid_glass
