#pragma once

#include <cstdint>
#include <span>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // How a creature is drawn, independent of how the swarm moves: the sheet's walk cycle, which
    // way it faces, and what its silhouette measures. The simulation only supplies where an agent
    // is, how far it has walked and how locked it is.

    // The creatures a swarm can be drawn as; each has a sprite sheet.
    enum class SwarmCreature : std::uint8_t { Spider, Cat };

    // One square frame of a sprite sheet, measured from its alpha. Lengths are fractions of the
    // frame's side.
    struct SpriteFrame {
        Vec2 centre{0.5, 0.5};  // deepest point of the silhouette: the body, which the creature
                                // turns and grows about
        double core = 0.0;  // radius of the largest solid disc around `centre`
        double reach = 0.0;  // distance from `centre` to the farthest solid texel
    };

    // Measures a `side` x `side` frame of 8-bit alpha, row-major; texels of at least half alpha are
    // solid. An empty frame measures as zero core and reach around its middle.
    SpriteFrame measureSprite(std::span<const std::uint8_t> alpha, int side);

    // Frame of a `frames`-long walk cycle after walking `travel` px, one cycle per `stride` px.
    int walkFrame(double travel, double stride, int frames) noexcept;

    // Rotation to draw with, radians clockwise on screen: `turn` of the way from facing `from` to
    // facing `heading`, the short way round. A creature is drawn in its sheet facing `forward`; the
    // tile it carries is upright while it faces its rest direction.
    double spriteRotation(double heading, double from, double turn) noexcept;

    // Size in px of the frame's side for a piece at `lock`. At lock 0 the whole silhouette fits a
    // circle of radius `fit`; at lock 1 its solid core covers a circle of radius `cover`, so the
    // piece shows its whole tile. The size grows geometrically in between, which reads as an even
    // swelling. Zero for a frame with no silhouette.
    double spriteSize(const SpriteFrame &frame, double fit, double cover, double lock) noexcept;

}  // namespace hyprimate
