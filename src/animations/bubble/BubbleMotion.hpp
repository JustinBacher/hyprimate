#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // Microbubbles one window's film can carry at once.
    inline constexpr std::size_t kBubbleMicroCapacity = 10;
    // Trail points and released microbubbles one movement step can hand to the screen.
    inline constexpr std::size_t kBubbleEmissions = 8;

    // What surface tension pulls a moving window into: a soft pillow (rounded corners, gently
    // bulging sides, its proportions kept), an ellipse of the same proportions, or a disc.
    enum class BubbleShape : std::uint8_t { Pillow, Ellipse, Circle };

    struct BubbleMotionParams {
        double strength = 0;  // scales every deformation; 0 leaves movement undeformed
        double microbubbles = 0;  // 0..1 how readily microbubbles form on the film
        double trail = 0;  // 0..1 how much film the moving window leaves behind
        double roundness = 0;  // 0..1 how far a moving window contracts toward its shape
        BubbleShape shape = BubbleShape::Pillow;
        double shapeSpeed = 1200;  // px/s at which the contraction is full; slower is less
        double transparency = .88;  // window transparency in bubble form, not at rest
    };

    // The movement's share of a flat membrane's shape, in logical px. The renderer carries every
    // point of the undeformed membrane by bubbleMotionPoint; the GPU material mirrors it.
    struct BubbleDeformation {
        Vec2 drag{};  // displacement of the trailing film: it points at the side that lags
        std::array<double, 4> waveCos{}, waveSin{};  // outward capillary modes n = 2..5
        double sheen = 0;  // 0..1 interference colour on the stressed film near the outline
        // How far surface tension has pulled the membrane toward its moving shape: 0 its
        // rectangle, 1 the elliptical-grid image of the rectangle on the ellipse of half extents
        // `extent`; slightly negative while it overshoots back.
        double round = 0;
        Vec2 extent{};
        // Where the film is drawn relative to the window: a layout move animates the window faster
        // than a bubble travels, so the film follows it from where it was.
        Vec2 carry{};

        bool still() const noexcept;
    };

    // The membrane's half size at the given roundness: its rectangle's, moving toward `extent`.
    Vec2 bubbleRoundHalf(Vec2 half_size, const BubbleDeformation &deformation);
    // Where contraction carries a point of the rectangle (logical px from its centre, y down): its
    // position on the ellipse of half extents `extent` under the elliptical-grid square-to-disc
    // map, mixed in by `round`. The corners pull in first, the edges bow outward, and the content
    // goes with them.
    Vec2 bubbleRoundPoint(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Displacement of a point of the (possibly rounded) membrane, whose half size is roundHalf. The
    // leading edge stays with the window; the trailing film lags by drag and narrows across the
    // motion, and the capillary modes ripple the outline while the centre stays put.
    Vec2 bubbleMotionOffset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Where movement carries a point of the undeformed membrane: rounded, displaced, then carried.
    Vec2 bubbleMotionPoint(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);

    // A soap microbubble as drawn: attached ones in frame coordinates, free ones in the coordinates
    // of the wake that carries them.
    struct BubbleMicro {
        Vec2 centre{};  // logical px
        double radius = 0;  // logical px
        double growth = 0;  // 0 a swelling of the parent film .. 1 a formed bubble
        Vec2 anchor{};  // where its neck meets the parent film
        double attach = 0;  // 0..1 strength of that neck; 0 once free
        double collapse = 0;  // 0..1 rupture of its film
        double thickness = 0;  // film thickness, nm
        double seed = 0;  // 0..1
    };

    // A point of film the moving window leaves behind, from its trailing outline. Frame
    // coordinates.
    struct BubbleTrailPoint {
        Vec2 position{};
        Vec2 direction{};  // unit direction of travel
        double halfWidth = 0;  // logical px
        double density = 0;  // 0..1 how much film it holds
        bool start = false;  // first point of a stroke: earlier points belong to another trail
    };

    // A microbubble that pinched off the window's film. Frame coordinates, screen velocity.
    struct BubbleRelease {
        BubbleMicro micro{};
        Vec2 velocity{};  // px/s
        double life = 0;  // s before its film collapses
    };

    // The movement of one window's bubble: an inertial fluid body behind the leading edge, the
    // capillary waves on its outline, the interior pressure mode it shares with the opening's
    // fluid core, the film colour stress brings out, and the microbubbles living on the film. All
    // of it is one state advanced in fixed substeps; nothing restarts between movements.
    class BubbleMotion {
      public:
        // Window translation since the previous advance, logical px. A carried translation is one
        // the layout animates: the film keeps its place and follows the window at its own pace.
        void drive(Vec2 displacement, bool carried = false) noexcept;
        // size: the membrane (capture box); body: the window body inside it, logical px from the
        // membrane's centre.
        void advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params);
        void reset() noexcept;
        void seed(std::uint64_t seed) noexcept;

        // False once every disturbance has died out: the window is drawn exactly as at rest.
        bool stirred() const noexcept;
        BubbleDeformation deformation() const noexcept;
        // Interior pressure and radial recoil, added to the surface's core.
        Vec2 core() const noexcept;
        // Speed of the window, px/s, as the fluid feels it.
        double speed() const noexcept;
        // The attached microbubbles; returns the number written.
        std::size_t microbubbles(std::span<BubbleMicro> out) const;

        // Film and microbubbles handed to the screen since the previous take, oldest first.
        std::span<const BubbleTrailPoint> trail() const noexcept;
        std::span<const BubbleRelease> releases() const noexcept;
        void clearEmissions() noexcept;

      private:
        enum class Fate : std::uint8_t { Detach, Merge, Collapse };
        struct Attached {
            double angle = 0;  // superellipse parameter of its foot on the body outline
            double slip = 0;  // rad/s along the outline
            double radius = 0;
            double grow = 0;  // s to form
            double age = 0;
            double life = 0;  // s attached before its fate
            double thickness = 0;
            double seed = 0;
            double growth = 0;
            double collapse = 0;
            bool merging = false;
            Fate fate = Fate::Collapse;
        };

        struct Outline {
            Vec2 point{}, normal{}, tangent{};
            double stretch = 0;  // px per radian of the parameter
        };
        Outline outline(double angle) const noexcept;
        BubbleMicro draw(const Attached &attached_bubble) const noexcept;
        void step(double step_seconds, Vec2 acceleration, double speed_rate);
        void nucleate(double elapsed, Vec2 acceleration);
        void migrate(double elapsed, Vec2 acceleration);
        void emit_trail(double elapsed, Vec2 acceleration);
        double trailing_angle() const noexcept;
        double reference() const noexcept;
        double drag_limit() const noexcept;
        double draw01() noexcept;
        void reshape() noexcept;

        BubbleMotionParams m_params{};
        Vec2 m_size{};
        Box m_body{};
        Vec2 m_pending{};
        Vec2 m_response{};  // first stage of the velocity filter
        Vec2 m_velocity{};  // filtered window velocity, px/s
        double m_speed = 0;
        double m_clock = 0;
        double m_substep = 0;  // unconsumed time below one substep
        Vec2 m_lag{}, m_lag_rate{};  // inertial offset of the trailing fluid, px
        std::array<Vec2, 4> m_wave{}, m_wave_rate{};  // capillary modes (cos, sin), px
        Vec2 m_pressure{}, m_pressure_rate{};  // interior pressure and recoil modes, dimensionless
        double m_sheen = 0;
        double m_round = 0, m_round_rate = 0;  // contraction toward the moving shape
        double m_hold = 0;  // the contraction the movement asks for, held through brief pauses
        double m_idle = 0;  // s the window has been paused
        Vec2 m_carry{}, m_carry_rate{};  // film position behind a layout-animated window, px
        Vec2 m_carry_mark{};  // m_carry as of the previous advance
        BubbleDeformation m_shape{};  // the deformation the state shows, cached per advance
        std::array<double, 4> m_phase{};  // buffeting phases
        std::uint64_t m_random = 0x9E3779B97F4A7C15ULL;
        double m_spawn = 0;  // accumulated nucleation hazard
        double m_threshold = 1;  // hazard at which the next microbubble forms
        std::array<Attached, kBubbleMicroCapacity> m_attached{};
        std::size_t m_attached_count = 0;
        double m_travel = 0;  // px since the last trail point
        bool m_trailing = false;
        std::array<BubbleTrailPoint, kBubbleEmissions> m_trail{};
        std::size_t m_trail_count = 0;
        std::array<BubbleRelease, kBubbleEmissions> m_releases{};
        std::size_t m_release_count = 0;
        bool m_stirred = false;
    };

}  // namespace hyprimate
