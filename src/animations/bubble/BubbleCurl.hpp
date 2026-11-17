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
        constexpr double pi = 3.14159265358979323846;
        const double r = std::min(surface.halfSize.x, surface.halfSize.y);
        if (r <= 0) {
            return {};
        }
        double width = std::max(0.0, std::min({64.0, .24 * r, .45 * physicalRadius})) *
                       std::clamp(surface.aureole, 0.0, 1.0);
        if (surface.spherical) {
            width = std::min(width, std::max(0.0, (pi * r) - physicalRadius));
        }
        const double q = std::clamp(((physicalRadius / r) - .04) / (.85 - .04), 0.0, 1.0);
        return {.width = width,
            .turn = std::max(.8,
                1 + 3.8 * q * q * (3 - 2 * q) + .65 * std::sin((3 * phi) + hole.phase[0]) +
                    .35 * std::sin((7 * phi) + hole.phase[1]))};
    }

    inline BubbleCurlSample bubbleCurlSample(
        Vec3 base, Vec3 outward, Vec3 normal, BubbleCurlSection section, double u) {
        const double THETA = u * section.turn;
        const double a = section.turn != 0 ? section.width / section.turn : 0;
        const double ALONG = -a * std::sin(THETA);
        const double LIFT = a * (1 - std::cos(THETA));
        const double c = std::cos(THETA);
        const double s = std::sin(THETA);
        return {.position = {.x = base.x + (outward.x * ALONG) + (normal.x * LIFT),
                    .y = base.y + (outward.y * ALONG) + (normal.y * LIFT),
                    .z = base.z + (outward.z * ALONG) + (normal.z * LIFT)},
            .normal = {.x = (normal.x * c) + (outward.x * s),
                .y = (normal.y * c) + (outward.y * s),
                .z = (normal.z * c) + (outward.z * s)}};
    }

    inline BubbleCurlSample bubblePeelSample(
        const BubbleSurface &surface, const BubbleHole &hole, double phi, double u) {
        const double r = std::min(surface.halfSize.x, surface.halfSize.y);
        const double BOUNDARY = bubbleHoleBoundary(hole, phi);
        const auto SECTION =
            bubbleCurlSection(surface, hole, BOUNDARY * (surface.spherical ? r : 1), phi);
        Vec3 direction{.x = std::cos(phi), .y = std::sin(phi), .z = 0};
        Vec3 normal{.x = 0, .y = 0, .z = 1};
        Vec3 base{};
        if (surface.spherical && r > 0) {
            const Vec3 c = hole.centre;
            const Vec3 a =
                std::abs(c.z) < .9 ? Vec3{.x = 0, .y = 0, .z = 1} : Vec3{.x = 1, .y = 0, .z = 0};
            Vec3 e1{.x = (a.y * c.z) - (a.z * c.y),
                .y = (a.z * c.x) - (a.x * c.z),
                .z = (a.x * c.y) - (a.y * c.x)};
            const double LEN = std::sqrt((e1.x * e1.x) + (e1.y * e1.y) + (e1.z * e1.z));
            if (LEN > 0) {
                e1 = {.x = e1.x / LEN, .y = e1.y / LEN, .z = e1.z / LEN};
            }
            const Vec3 e2{.x = (c.y * e1.z) - (c.z * e1.y),
                .y = (c.z * e1.x) - (c.x * e1.z),
                .z = (c.x * e1.y) - (c.y * e1.x)};
            direction = {.x = (e1.x * std::cos(phi)) + (e2.x * std::sin(phi)),
                .y = (e1.y * std::cos(phi)) + (e2.y * std::sin(phi)),
                .z = (e1.z * std::cos(phi)) + (e2.z * std::sin(phi))};
            const double ROOT = BOUNDARY + (SECTION.width / r);
            normal = {.x = (c.x * std::cos(ROOT)) + (direction.x * std::sin(ROOT)),
                .y = (c.y * std::cos(ROOT)) + (direction.y * std::sin(ROOT)),
                .z = (c.z * std::cos(ROOT)) + (direction.z * std::sin(ROOT))};
            direction = {.x = (-c.x * std::sin(ROOT)) + (direction.x * std::cos(ROOT)),
                .y = (-c.y * std::sin(ROOT)) + (direction.y * std::cos(ROOT)),
                .z = (-c.z * std::sin(ROOT)) + (direction.z * std::cos(ROOT))};
            base = {.x = r * normal.x, .y = r * normal.y, .z = r * normal.z};
        } else {
            base = {.x = hole.centre.x + (direction.x * (BOUNDARY + SECTION.width)),
                .y = hole.centre.y + (direction.y * (BOUNDARY + SECTION.width)),
                .z = 0};
        }
        return bubbleCurlSample(base, direction, normal, SECTION, u);
    }

}  // namespace hyprimate
