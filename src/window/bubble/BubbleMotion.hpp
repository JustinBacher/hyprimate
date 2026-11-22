#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "shared/Core.hpp"
#include "window/bubble/BubbleFoam.hpp"

namespace hyprimate {

    // Microbubbles one window's film can carry at once.
    inline constexpr std::size_t K_BUBBLE_MICRO_CAPACITY = 12;
    // Membrane swellings drawn at once.
    inline constexpr std::size_t K_BUBBLE_BUDS = 4;
    // Film strands a moving window can draw at once.
    inline constexpr std::size_t K_BUBBLE_STRANDS = 4;
    // Strand points and released microbubbles one movement advance can hand to the screen.
    inline constexpr std::size_t K_BUBBLE_EMISSIONS = 16;

    // What surface tension pulls a moving window into: a soft pillow (rounded corners, gently
    // bulging sides, its proportions kept), an ellipse of the same proportions, or a disc.
    enum class BubbleShape : std::uint8_t { Pillow, Ellipse, Circle };

    struct BubbleMotionParams {
        double strength = 0;  // scales every deformation; 0 leaves movement undeformed
        double microbubbles = 0;  // 0..1 how readily microbubbles form on the film
        double trail = 0;  // 0..1 how much film the moving window leaves behind
        double roundness = 0;  // 0..1 how far a moving window contracts toward its shape
        BubbleShape shape = BubbleShape::Pillow;
        double shape_speed = 1200;  // px/s at which the contraction is full; slower is less
        double transparency = .88;  // window transparency in bubble form, not at rest
    };

    // A swelling of the membrane where a microbubble grows out of it or still hangs on its neck.
    // Positions are in the contracted membrane's coordinates (bubbleRoundPoint), logical px.
    struct BubbleBud {
        Vec2 foot{};
        double height = 0;  // outward displacement at the foot, px; 0: none
        double width = 0;  // Gaussian radius of the swelling, px; height stays below 0.55 width
    };

    // The movement's share of a flat membrane's shape, in logical px. The renderer carries every
    // point of the undeformed membrane by bubble_motion_point; the GPU material mirrors it.
    struct BubbleDeformation {
        // The leading face flattens against the air by a fixed share of the trailing lag.
        Vec2 drag{};
        std::array<double, 4> wave_cos{}, wave_sin{};  // outward capillary modes n = 2..5
        double sheen = 0;  // 0..1 interference colour on the stressed film near the outline
        // How far surface tension has pulled the membrane toward its moving shape: 0 its
        // rectangle, 1 the elliptical-grid image of the rectangle on the ellipse of half extents
        // `extent`; slightly negative while it overshoots back.
        double round = 0;
        Vec2 extent{};
        // Where the film is drawn relative to the window: a layout move animates the window faster
        // than a bubble travels, so the film follows it from where it was.
        Vec2 carry{};
        std::array<BubbleBud, K_BUBBLE_BUDS> buds{};

        [[nodiscard]] bool still() const noexcept;
    };

    // The membrane's half size at the given roundness: its rectangle's, moving toward `extent`.
    Vec2 bubble_round_half(Vec2 half_size, const BubbleDeformation &deformation);
    // Where contraction carries a point of the rectangle (logical px from its centre, y down): its
    // position on the ellipse of half extents `extent` under the elliptical-grid square-to-disc
    // map, mixed in by `round`. The corners pull in first, the edges bow outward, and the content
    // goes with them.
    Vec2 bubble_round_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Displacement of a point of the (possibly rounded) membrane, whose half size is half_size.
    // The leading face flattens, the trailing film lags and narrows across the motion, the
    // capillary modes ripple the outline, and buds swell it locally.
    Vec2 bubble_motion_offset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);
    // Where movement carries a point of the undeformed membrane: rounded, displaced, then carried.
    Vec2 bubble_motion_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point);

    // A point of a film strand the moving window draws out of its trailing side. Frame
    // coordinates. Every advance re-sends the newest point of each strand still on the window, so
    // the strand stays joined to the moving outline; a provisional point is replaced by the next
    // point of its strand.
    struct BubbleTrailPoint {
        Vec2 position{};
        Vec2 velocity{};  // px/s on screen the film keeps when laid down
        double half_width = 0;  // logical px
        double thickness = 0;  // nm of liquid it holds when laid down
        std::uint32_t strand = 0;  // which strand; consecutive points of one strand join
        bool provisional = false;
        bool last = false;  // the strand pinched off the window here
    };

    // A microbubble that pinched off the window's film. Frame coordinates, screen velocity.
    struct BubbleRelease {
        BubbleMicro micro{};  // as last drawn attached, its neck still narrowing
        Vec2 velocity{};  // px/s
        Vec2 squash_rate{};  // its outline still rings from the pinch
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
        [[nodiscard]] bool stirred() const noexcept;
        [[nodiscard]] BubbleDeformation deformation() const noexcept;
        // Interior pressure and radial recoil, added to the surface's core.
        [[nodiscard]] Vec2 core() const noexcept;
        // Speed of the window, px/s, as the fluid feels it.
        [[nodiscard]] double speed() const noexcept;
        // The attached microbubbles; returns the number written.
        [[nodiscard]] std::size_t microbubbles(std::span<BubbleMicro> out) const;

        // Film and microbubbles handed to the screen since the previous take, oldest first.
        [[nodiscard]] std::span<const BubbleTrailPoint> trail() const noexcept;
        [[nodiscard]] std::span<const BubbleRelease> releases() const noexcept;
        void clear_emissions() noexcept;

      private:
        // A microbubble's life on the film: it swells out of it, rides it, and either pinches off,
        // sinks back into it or pops.
        enum class Stage : std::uint8_t { Bud, Ride, Pinch, Merge, Pop };
        struct Attached {
            double angle = 0;  // superellipse parameter of its foot on the body outline
            double slip = 0;  // rad/s along the outline
            double radius = 0;  // formed radius, px
            double grow = 0;  // s to form
            double age = 0;
            double patience = 0;  // s of calm it rides out before it resolves; spent faster slow
            double hold = 0;  // how unstable its attachment must get before it lets go
            double pinch = 0;  // 0..1 narrowing of its neck before it lets go
            double pinch_rate = 0;  // per s
            double thickness = 0;
            double seed = 0;
            double growth = 0;  // 0..1 formation; falls back to 0 as it merges into the film
            double collapse = 0;
            Vec2 squash{}, squash_rate{};  // outline quadrupole, relative to radius
            Stage stage = Stage::Bud;
            bool pops = false;  // resolves by popping rather than merging back
        };
        // A strand of film drawn out of the trailing side: a broad lamella or a thin thread.
        struct Strand {
            std::uint32_t id = 0;
            double angle = 0;  // outline parameter of its root; slides as the trailing side turns
            double half_width = 0;
            double thickness = 0;
            double reach = 0;  // px it can be drawn out before it pinches off
            double length = 0;  // px drawn out so far
            double travel = 0;  // px since its last fixed point
            double slack = 0;  // s the window has been too slow to draw it on
            bool live = false;
            bool thread = false;  // a thin thread from a corner or a released bubble's neck
        };

        struct Outline {
            Vec2 point{}, normal{}, tangent{};
            double stretch = 0;  // px per radian of the parameter
            double curvature = 0;  // 1/px
        };
        [[nodiscard]] Outline outline(double angle) const noexcept;
        [[nodiscard]] BubbleMicro draw(const Attached &attached_bubble) const noexcept;
        [[nodiscard]] BubbleBud bud(const Attached &attached_bubble) const noexcept;
        void step(double step_seconds, Vec2 acceleration, double speed_rate);
        void nucleate(double elapsed, Vec2 acceleration);
        void migrate(double elapsed, Vec2 acceleration);
        void release(const Attached &attached_bubble);
        void emit_trail(double elapsed, Vec2 acceleration);
        bool start_strand(double angle, double half_width, double thickness, double reach);
        // 0..1 how squarely the outline at a parameter faces back against the motion.
        [[nodiscard]] double exposure(double angle) const noexcept;
        [[nodiscard]] double trailing_angle() const noexcept;
        [[nodiscard]] double reference() const noexcept;
        [[nodiscard]] double drag_limit() const noexcept;
        double draw01() noexcept;
        double gauss() noexcept;
        void reshape() noexcept;

        BubbleMotionParams m_params{};
        Vec2 m_size{};
        Box m_body{};
        Vec2 m_pending{};
        Vec2 m_response{};
        Vec2 m_velocity{};
        Vec2 m_behind{};
        double m_speed = 0;
        double m_clock = 0;
        double m_substep = 0;
        Vec2 m_lag{}, m_lag_rate{};
        std::array<Vec2, 4> m_wave{}, m_wave_rate{};
        Vec2 m_pressure{}, m_pressure_rate{};
        double m_sheen = 0;
        double m_round = 0, m_round_rate = 0;
        double m_hold = 0;
        double m_idle = 0;
        Vec2 m_carry{}, m_carry_rate{};
        Vec2 m_carry_mark{};
        BubbleDeformation m_shape{};
        std::array<double, 4> m_phase{};
        std::uint64_t m_random = 0x9E3779B97F4A7C15ULL;
        double m_spawn = 0;
        double m_threshold = 1;
        std::array<Attached, K_BUBBLE_MICRO_CAPACITY> m_attached{};
        std::size_t m_attached_count = 0;
        std::array<Strand, K_BUBBLE_STRANDS> m_strands{};
        std::uint32_t m_next_strand = 1;
        double m_thread_hazard = 0;
        std::array<BubbleTrailPoint, K_BUBBLE_EMISSIONS> m_trail{};
        std::size_t m_trail_count = 0;
        std::array<BubbleRelease, K_BUBBLE_EMISSIONS> m_releases{};
        std::size_t m_release_count = 0;
        bool m_stirred = false;
    };

}  // namespace hyprimate
