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
        const double theta = u * section.turn;
        const double a = section.turn != 0 ? section.width / section.turn : 0;
        const double along = -a * std::sin(theta);
        const double lift = a * (1 - std::cos(theta));
        const double c = std::cos(theta);
        const double s = std::sin(theta);
        return {.position = {.x = base.x + (outward.x * along) + (normal.x * lift),
                    .y = base.y + (outward.y * along) + (normal.y * lift),
                    .z = base.z + (outward.z * along) + (normal.z * lift)},
            .normal = {(normal.x * c) + (outward.x * s),
                (normal.y * c) + (outward.y * s),
                (normal.z * c) + (outward.z * s)}};
    }

    inline BubbleCurlSample bubblePeelSample(
        const BubbleSurface &surface, const BubbleHole &hole, double phi, double u) {
        const double r = std::min(surface.halfSize.x, surface.halfSize.y);
        const double boundary = bubbleHoleBoundary(hole, phi);
        const auto section =
            bubbleCurlSection(surface, hole, boundary * (surface.spherical ? r : 1), phi);
        Vec3 direction{std::cos(phi), std::sin(phi), 0};
        Vec3 normal{0, 0, 1};
        Vec3 base{};
        if (surface.spherical && r > 0) {
            const Vec3 c = hole.centre;
            const Vec3 a =
                std::abs(c.z) < .9 ? Vec3{.x = 0, .y = 0, .z = 1} : Vec3{.x = 1, .y = 0, .z = 0};
            Vec3 e1{
                (a.y * c.z) - (a.z * c.y), (a.z * c.x) - (a.x * c.z), (a.x * c.y) - (a.y * c.x)};
            const double len = std::sqrt((e1.x * e1.x) + (e1.y * e1.y) + (e1.z * e1.z));
            if (len > 0) {
                e1 = {.x = e1.x / len, .y = e1.y / len, .z = e1.z / len};
            }
            const Vec3 e2{(c.y * e1.z) - (c.z * e1.y),
                (c.z * e1.x) - (c.x * e1.z),
                (c.x * e1.y) - (c.y * e1.x)};
            direction = {(e1.x * std::cos(phi)) + (e2.x * std::sin(phi)),
                (e1.y * std::cos(phi)) + (e2.y * std::sin(phi)),
                (e1.z * std::cos(phi)) + (e2.z * std::sin(phi))};
            const double root = boundary + (section.width / r);
            normal = {(c.x * std::cos(root)) + (direction.x * std::sin(root)),
                (c.y * std::cos(root)) + (direction.y * std::sin(root)),
                (c.z * std::cos(root)) + (direction.z * std::sin(root))};
            direction = {(-c.x * std::sin(root)) + (direction.x * std::cos(root)),
                (-c.y * std::sin(root)) + (direction.y * std::cos(root)),
                (-c.z * std::sin(root)) + (direction.z * std::cos(root))};
            base = {.x = r * normal.x, .y = r * normal.y, .z = r * normal.z};
        } else {
            base = {.x = hole.centre.x + (direction.x * (boundary + section.width)),
                .y = hole.centre.y + (direction.y * (boundary + section.width)),
                .z = 0};
        }
        return bubbleCurlSample(base, direction, normal, section, u);
    }

}  // namespace hyprimate
