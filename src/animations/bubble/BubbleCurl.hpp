#pragma once

#include <algorithm>
#include <cmath>

#include "animations/bubble/Bubble.hpp"

namespace hyprimate {

    struct BubbleCurlSection {
        double width = 0, turn = 0;
    };

    struct BubbleCurlSample {
        Vec3 position{}, normal{};
    };

    inline BubbleCurlSection bubbleCurlSection(
        const BubbleSurface &surface, const BubbleHole &hole, double physicalRadius, double phi) {
        constexpr double PI_VALUE = 3.14159265358979323846;
        const double RADIUS = std::min(surface.halfSize.x, surface.halfSize.y);
        if (RADIUS <= 0) {
            return {};
        }
        double width = std::max(0.0, std::min({64.0, .24 * RADIUS, .45 * physicalRadius})) *
                       std::clamp(surface.aureole, 0.0, 1.0);
        if (surface.spherical) {
            width = std::min(width, std::max(0.0, (PI_VALUE * RADIUS) - physicalRadius));
        }
        const double FRACTION =
            std::clamp(((physicalRadius / RADIUS) - .04) / (.85 - .04), 0.0, 1.0);
        return {.width = width,
            .turn = std::max(.8,
                1 + 3.8 * FRACTION * FRACTION * (3 - 2 * FRACTION) +
                    .65 * std::sin((3 * phi) + hole.phase[0]) +
                    .35 * std::sin((7 * phi) + hole.phase[1]))};
    }

    inline BubbleCurlSample bubbleCurlSample(
        Vec3 base, Vec3 outward, Vec3 normal, BubbleCurlSection section, double u) {
        const double THETA = u * section.turn;
        const double ARC_RADIUS = section.turn != 0 ? section.width / section.turn : 0;
        const double ALONG = -ARC_RADIUS * std::sin(THETA);
        const double LIFT = ARC_RADIUS * (1 - std::cos(THETA));
        const double COSINE = std::cos(THETA);
        const double SINE = std::sin(THETA);
        return {.position = {.x = base.x + (outward.x * ALONG) + (normal.x * LIFT),
                    .y = base.y + (outward.y * ALONG) + (normal.y * LIFT),
                    .z = base.z + (outward.z * ALONG) + (normal.z * LIFT)},
            .normal = {.x = (normal.x * COSINE) + (outward.x * SINE),
                .y = (normal.y * COSINE) + (outward.y * SINE),
                .z = (normal.z * COSINE) + (outward.z * SINE)}};
    }

    inline BubbleCurlSample bubblePeelSample(
        const BubbleSurface &surface, const BubbleHole &hole, double phi, double u) {
        const double RADIUS = std::min(surface.halfSize.x, surface.halfSize.y);
        const double BOUNDARY = bubbleHoleBoundary(hole, phi);
        const auto SECTION =
            bubbleCurlSection(surface, hole, BOUNDARY * (surface.spherical ? RADIUS : 1), phi);
        Vec3 direction{.x = std::cos(phi), .y = std::sin(phi), .z = 0};
        Vec3 normal{.x = 0, .y = 0, .z = 1};
        Vec3 base{};
        if (surface.spherical && RADIUS > 0) {
            const Vec3 CENTRE = hole.centre;
            const Vec3 AXIS = std::abs(CENTRE.z) < .9 ? Vec3{.x = 0, .y = 0, .z = 1}
                                                      : Vec3{.x = 1, .y = 0, .z = 0};
            Vec3 basisFirst{.x = (AXIS.y * CENTRE.z) - (AXIS.z * CENTRE.y),
                .y = (AXIS.z * CENTRE.x) - (AXIS.x * CENTRE.z),
                .z = (AXIS.x * CENTRE.y) - (AXIS.y * CENTRE.x)};
            const double LEN =
                std::sqrt((basisFirst.x * basisFirst.x) + (basisFirst.y * basisFirst.y) +
                          (basisFirst.z * basisFirst.z));
            if (LEN > 0) {
                basisFirst = {
                    .x = basisFirst.x / LEN, .y = basisFirst.y / LEN, .z = basisFirst.z / LEN};
            }
            const Vec3 BASIS_SECOND{.x = (CENTRE.y * basisFirst.z) - (CENTRE.z * basisFirst.y),
                .y = (CENTRE.z * basisFirst.x) - (CENTRE.x * basisFirst.z),
                .z = (CENTRE.x * basisFirst.y) - (CENTRE.y * basisFirst.x)};
            direction = {.x = (basisFirst.x * std::cos(phi)) + (BASIS_SECOND.x * std::sin(phi)),
                .y = (basisFirst.y * std::cos(phi)) + (BASIS_SECOND.y * std::sin(phi)),
                .z = (basisFirst.z * std::cos(phi)) + (BASIS_SECOND.z * std::sin(phi))};
            const double ROOT = BOUNDARY + (SECTION.width / RADIUS);
            normal = {.x = (CENTRE.x * std::cos(ROOT)) + (direction.x * std::sin(ROOT)),
                .y = (CENTRE.y * std::cos(ROOT)) + (direction.y * std::sin(ROOT)),
                .z = (CENTRE.z * std::cos(ROOT)) + (direction.z * std::sin(ROOT))};
            direction = {.x = (-CENTRE.x * std::sin(ROOT)) + (direction.x * std::cos(ROOT)),
                .y = (-CENTRE.y * std::sin(ROOT)) + (direction.y * std::cos(ROOT)),
                .z = (-CENTRE.z * std::sin(ROOT)) + (direction.z * std::cos(ROOT))};
            base = {.x = RADIUS * normal.x, .y = RADIUS * normal.y, .z = RADIUS * normal.z};
        } else {
            base = {.x = hole.centre.x + (direction.x * (BOUNDARY + SECTION.width)),
                .y = hole.centre.y + (direction.y * (BOUNDARY + SECTION.width)),
                .z = 0};
        }
        return bubbleCurlSample(base, direction, normal, SECTION, u);
    }

}  // namespace hyprimate
