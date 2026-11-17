#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/bubble/BubbleMotion.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {

    // Rim corrugation mode numbers. The GPU material mirrors these values; change both together.
    inline constexpr std::array<int, 4> K_BUBBLE_RIM_MODES{3, 5, 9, 14};
    inline constexpr std::size_t K_BUBBLE_HOLES = 4;
    // Debris a renderer must be able to draw per surface; bubble_particles never writes more than
    // its span holds.
    inline constexpr std::size_t K_BUBBLE_MAX_PARTICLES = 384;
    // Screen-glass impacts one surface's rupture can produce.
    inline constexpr std::size_t K_BUBBLE_MAX_IMPACTS = 4;

    // One opening in a membrane: it nucleates in a black-film spot, then grows at a constant
    // (Taylor–Culick) speed. The film exists outside every nucleated hole.
    struct BubbleHole {
        Vec3 centre{};  // flat: (x, y, 0) logical px from the frame centre, y down;
                        // spherical: unit vector, bubble frame (x right, y down, z toward viewer)
        double start = 0;  // nucleation time on the surface's rupture clock, s
        double radius =
            0;  // boundary radius now: px (flat) or geodesic radians; 0 before nucleation
        double rim = 0;  // rim tube radius now, logical px
        double precursor = 0;  // 0..1 black-film thinning at the centre: rises before nucleation
        std::array<double, 4>
            amplitude{};  // corrugation relative to radius, per K_BUBBLE_RIM_MODES
        std::array<double, 4> phase{};  // radians
    };

    // A membrane has its own geometry, film and rupture state, independent of its imagery.
    struct BubbleSurface {
        Vec2 half_size{};
        double corner = 0;
        double depth = 0;
        Vec2 core{};  // signed heights of the interior pressure and radial recoil modes
        double content =
            1;  // opacity of the surface's imagery (window texture or refracted wallpaper)
        double film = 0;  // 0..1 weight of the surface-wide thin-film reflection layer
        double visibility = 0;
        double ripple = 0;
        double time = 0;
        double thickness = 0;  // mean film thickness, nm
        double flow =
            0;  // film-flow phase, s: advances with time, faster while the surface deforms
        bool spherical =
            false;  // holes and shading on a sphere of radius half_size.x (the outer bubble)
        bool closing = false;  // the surface is rupturing
        // Movement's deformation of a flat surface (the window); the opening and rest leave it
        // still.
        BubbleDeformation motion{};
        double transparency = .88;  // scene transmission when movement rounds the window

        // Rupture. Holes and debris are pure functions of these fields.
        double clock = 0;  // s on this surface's rupture clock
        double rate = 1;  // rupture-clock seconds per animation second
        double speed = 0;  // terminal rim speed: px/s (flat) or rad/s (spherical)
        double aureole = 0;  // 0..1 thickened, flapping band ahead of every rim
        double rupture_visibility = 0;  // film opacity retained after the parent is consumed
        std::uint64_t debris = 0;  // seed of this rupture's ligament and droplet stream
        std::array<BubbleHole, K_BUBBLE_HOLES> holes{};
        std::size_t hole_count = 0;  // holes[0, hole_count) exist, nucleated or still precursors
    };

    struct BubbleFrame {
        Vec2 size{};
        BubbleSurface window{};
        BubbleSurface shell{};
        double seed = 0;
        // Microbubbles riding the window's film, in frame coordinates.
        std::array<BubbleMicro, K_BUBBLE_MICRO_CAPACITY> micro{};
        std::size_t micro_count = 0;
    };

    // A liquid droplet, possibly still on its ligament. Evaporation shrinks its radius.
    struct BubbleParticle {
        Vec2 centre{};
        double radius = 0;
        Vec2 tail{};  // ligament anchor; equals centre once detached
        double tail_radius = 0;  // ligament radius at the anchor; 0 = none
        double depth = 0;  // -1 behind the bubble .. +1 toward the viewer
        double blur = 0;  // 0..1 defocus of a droplet approaching the screen glass
    };

    // A droplet that reaches the screen glass and stays there as a screen-anchored bead.
    struct BubbleImpact {
        Vec2 position{};  // logical px from the frame centre, y down
        double radius = 0;  // bead radius at impact, logical px
        double delay = 0;  // animation seconds from the surface's current clock until impact;
                           // negative: already landed
        Vec2 drift{};  // px/s creep on the glass
    };

    // Azimuth of a point around a hole. Flat: atan2(p.y - c.y, p.x - c.x) in surface px. Spherical
    // (unit point P, unit centre c): a = (0, 0, 1) when |c.z| < 0.9, otherwise (1, 0, 0); e1 =
    // normalize(cross(a, c)); e2 = cross(c, e1); atan2(dot(P, e2), dot(P, e1)). The shader uses the
    // same basis.
    double bubble_hole_azimuth(const BubbleHole &hole, Vec3 point, bool spherical);
    // Boundary radius at an azimuth, in the units of hole.radius:
    // radius * (1 + sum_m amplitude[m] * cos(K_BUBBLE_RIM_MODES[m] * phi + phase[m])).
    double bubble_hole_boundary(const BubbleHole &hole, double phi);
    // Debris of one surface's rupture visible now; returns the number written (at most out.size()).
    std::size_t bubble_particles(const BubbleSurface &surface, std::span<BubbleParticle> out);
    // Every screen-glass impact of one surface's rupture, past or future; returns the number
    // written.
    std::size_t bubble_impacts(const BubbleSurface &surface, std::span<BubbleImpact> out);

    struct BubbleDuration {
        double seconds;
    };

    // One window's soap film for its whole life: the open or close transition it is in, if any, and
    // the movement of its fluid, which keeps running across them.
    class Bubble {
      public:
        void begin(Transition direction, BubbleDuration duration, std::uint64_t seed, Vec2 size);
        // Advances the transition alone (a closing window no longer moves).
        void advance(double elapsed);
        // Advances the transition and the movement of a live window: size is its membrane
        // (capture box), body the window body inside it, logical px from the membrane's centre.
        void advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params);
        // Ends the transition; the movement is untouched.
        void reset();
        // Window translation since the previous advance, logical px; carried when the layout
        // animates it (see BubbleMotion::drive).
        void drive(Vec2 displacement, bool carried = false);
        // Seeds the movement's microbubbles and buffeting, per window.
        void seed_motion(std::uint64_t seed);
        // Drops the movement at once (movement switched off or the window detached).
        void stop_motion();
        [[nodiscard]] bool active() const;
        [[nodiscard]] bool opening() const;
        // An opening whose film has landed flat and is settling: movement may act on it.
        [[nodiscard]] bool landed() const;
        [[nodiscard]] bool closed() const;
        // The movement still disturbs the window's film.
        [[nodiscard]] bool stirred() const;
        [[nodiscard]] const BubbleMotion &motion() const;
        void clear_emissions();
        [[nodiscard]] BubbleFrame frame(Vec2 size) const;
        // Contains every drawn surface pixel and every particle (ligaments and perspective
        // included).
        [[nodiscard]] Box bounds(Vec2 size) const;

      private:
        void apply_motion(BubbleFrame &frame) const;

        BubbleFrame m_initial{};
        Vec2 m_size{};
        double m_duration = 0;
        double m_elapsed = 0;
        double m_seed = 0;
        double m_flow = 0;  // film-flow phase of the live window, s
        double m_transparency = .88;
        Vec2 m_rupture{};
        bool m_active = false;
        bool m_opening = false;
        bool m_closed = false;
        BubbleMotion m_motion{};
    };

}  // namespace hyprimate
