#pragma once

#include "animations/shared/Core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace liquid_glass {

// Rim corrugation mode numbers. The GPU material mirrors these values; change both together.
inline constexpr std::array<int, 4> kBubbleRimModes{3, 5, 9, 14};
inline constexpr std::size_t        kBubbleHoles = 4;
// Debris a renderer must be able to draw per surface; bubbleParticles never writes more than its span holds.
inline constexpr std::size_t kBubbleMaxParticles = 384;
// Screen-glass impacts one surface's rupture can produce.
inline constexpr std::size_t kBubbleMaxImpacts = 4;

// One opening in a membrane: it nucleates in a black-film spot, then grows at a constant (Taylor–Culick) speed. The
// film exists outside every nucleated hole.
struct BubbleHole {
    Vec3                  centre{};      // flat: (x, y, 0) logical px from the frame centre, y down;
                                         // spherical: unit vector, bubble frame (x right, y down, z toward viewer)
    double                start = 0;     // nucleation time on the surface's rupture clock, s
    double                radius = 0;    // boundary radius now: px (flat) or geodesic radians; 0 before nucleation
    double                rim = 0;       // rim tube radius now, logical px
    double                precursor = 0; // 0..1 black-film thinning at the centre: rises before nucleation
    std::array<double, 4> amplitude{};   // corrugation relative to radius, per kBubbleRimModes
    std::array<double, 4> phase{};       // radians
};

// A membrane has its own geometry, film and rupture state, independent of its imagery.
struct BubbleSurface {
    Vec2   halfSize{};
    double corner = 0;
    double depth = 0;
    Vec2   core{};          // signed heights of the interior pressure and radial recoil modes
    double content = 1;     // opacity of the surface's imagery (window texture or refracted wallpaper)
    double film = 0;        // 0..1 weight of the surface-wide thin-film reflection layer
    double visibility = 0;
    double ripple = 0;
    double time = 0;
    double thickness = 0;   // mean film thickness, nm
    double flow = 0;        // film-flow phase, s: advances with time, faster while the surface deforms
    bool   spherical = false; // holes and shading on a sphere of radius halfSize.x (the outer bubble)
    bool   closing = false;   // the surface is rupturing

    // Rupture. Holes and debris are pure functions of these fields.
    double                                clock = 0;  // s on this surface's rupture clock
    double                                rate = 1;   // rupture-clock seconds per animation second
    double                                speed = 0;  // terminal rim speed: px/s (flat) or rad/s (spherical)
    double                                aureole = 0; // 0..1 thickened, flapping band ahead of every rim
    std::uint64_t                         debris = 0;  // seed of this rupture's ligament and droplet stream
    std::array<BubbleHole, kBubbleHoles>  holes{};
    std::size_t                           holeCount = 0; // holes[0, holeCount) exist, nucleated or still precursors
};

struct BubbleFrame {
    Vec2          size{};
    BubbleSurface window{};
    BubbleSurface shell{};
    double        seed = 0;
};

// A drawn piece of rupture debris in logical px from the frame centre, y down: a liquid droplet, possibly still on
// its ligament, or a small film flake. Evaporation shrinks radius; there is no opacity fade.
struct BubbleParticle {
    Vec2   centre{};
    double radius = 0;
    Vec2   tail{};         // ligament anchor; equals centre once detached
    double tailRadius = 0; // ligament radius at the anchor; 0 = none
    double depth = 0;      // -1 behind the bubble .. +1 toward the viewer
    double thickness = 0;  // > 0: film flake of this thickness (nm); 0: liquid droplet
    double spin = 0;       // flake orientation, radians
    double blur = 0;       // 0..1 defocus of a droplet approaching the screen glass
};

// A droplet that reaches the screen glass and stays there as a screen-anchored bead.
struct BubbleImpact {
    Vec2   position{}; // logical px from the frame centre, y down
    double radius = 0; // bead radius at impact, logical px
    double delay = 0;  // animation seconds from the surface's current clock until impact; negative: already landed
    Vec2   drift{};    // px/s creep on the glass
};

// Azimuth of a point around a hole. Flat: atan2(p.y - c.y, p.x - c.x) in surface px. Spherical (unit point P, unit
// centre c): a = (0, 0, 1) when |c.z| < 0.9, otherwise (1, 0, 0); e1 = normalize(cross(a, c)); e2 = cross(c, e1);
// atan2(dot(P, e2), dot(P, e1)). The shader uses the same basis.
double bubbleHoleAzimuth(const BubbleHole &hole, Vec3 point, bool spherical);
// Boundary radius at an azimuth, in the units of hole.radius:
// radius * (1 + sum_m amplitude[m] * cos(kBubbleRimModes[m] * phi + phase[m])).
double bubbleHoleBoundary(const BubbleHole &hole, double phi);
// Debris of one surface's rupture visible now; returns the number written (at most out.size()).
std::size_t bubbleParticles(const BubbleSurface &surface, std::span<BubbleParticle> out);
// Every screen-glass impact of one surface's rupture, past or future; returns the number written.
std::size_t bubbleImpacts(const BubbleSurface &surface, std::span<BubbleImpact> out);

class Bubble {
  public:
    void        begin(Transition direction, double duration, std::uint64_t seed, Vec2 size);
    void        advance(double elapsed);
    void        reset();
    bool        active() const;
    bool        opening() const;
    bool        closed() const;
    BubbleFrame frame(Vec2 size) const;
    // Contains every drawn surface pixel and every particle (ligaments and perspective included).
    Box bounds(Vec2 size) const;

  private:
    BubbleFrame initial_{};
    Vec2        size_{};
    double      duration_ = 0;
    double      elapsed_ = 0;
    double      seed_ = 0;
    Vec2        rupture_{};
    bool        active_ = false;
    bool        opening_ = false;
    bool        closed_ = false;
};

} // namespace liquid_glass
