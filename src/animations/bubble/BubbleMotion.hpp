#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/bubble/BubbleFoam.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {

    // Microbubbles one window's film can carry at once.
    inline constexpr std::size_t kBubbleMicroCapacity = 12;
    // Membrane swellings drawn at once: the feet of the attached bubbles that press most on it.
    inline constexpr std::size_t kBubbleBuds = 4;
    // Film strands a moving window can be drawing out of its trailing side at once.
    inline constexpr std::size_t kBubbleStrands = 4;
    // Strand points and released microbubbles one movement advance can hand to the screen.
    inline constexpr std::size_t kBubbleEmissions = 16;

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

    // A swelling of the membrane where a microbubble grows out of it or still hangs on its neck.
    // Positions are in the contracted membrane's coordinates (bubbleRoundPoint), logical px.
    struct BubbleBud {
        Vec2 foot{};
        double height = 0;  // outward displacement at the foot, px; 0: none
        double width = 0;  // Gaussian radius of the swelling, px; height stays below 0.55 width
    };

    // The movement's share of a flat membrane's shape, in logical px. The renderer carries every
    // point of the undeformed membrane by bubbleMotionPoint; the GPU material mirrors it.
    struct BubbleDeformation {
        // Displacement of the trailing film: it points at the side that lags. The leading face
        // flattens against the air by a fixed share of it.
        Vec2 drag{};
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
        std::array<BubbleBud, kBubbleBuds> buds{};

        bool still() const noexcept;
    };

    // The membrane's half size at the given roundness: its rectangle's, moving toward `extent`.
    Vec2 bubbleRoundHalf(Vec2 halfSize, const BubbleDeformation &deformation);
    // Where contraction carries a point of the rectangle (logical px from its centre, y down): its
    // position on the ellipse of half extents `extent` under the elliptical-grid square-to-disc
    // map, mixed in by `round`. The corners pull in first, the edges bow outward, and the content
    // goes with them.
    Vec2 bubbleRoundPoint(Vec2 halfSize, const BubbleDeformation &deformation, Vec2 point);
    // Displacement of a point of the (possibly rounded) membrane, whose half size is roundHalf. The
    // leading face flattens, the trailing film lags by drag and narrows across the motion, the
    // capillary modes ripple the outline while the centre stays put, and buds swell it locally.
    Vec2 bubbleMotionOffset(Vec2 roundHalf, const BubbleDeformation &deformation, Vec2 point);
    // Where movement carries a point of the undeformed membrane: rounded, displaced, then carried.
    Vec2 bubbleMotionPoint(Vec2 halfSize, const BubbleDeformation &deformation, Vec2 point);

    // A point of a film strand the moving window draws out of its trailing side. Frame
    // coordinates. Every advance re-sends the newest point of each strand still on the window, so
    // the strand stays joined to the moving outline; a provisional point is replaced by the next
    // point of its strand.
    struct BubbleTrailPoint {
        Vec2 position{};
        Vec2 velocity{};  // px/s on screen the film keeps when laid down
        double halfWidth = 0;  // logical px
        double thickness = 0;  // nm of liquid it holds when laid down
        std::uint32_t strand = 0;  // which strand; consecutive points of one strand join
        bool provisional = false;
        bool last = false;  // the strand pinched off the window here
    };

    // A microbubble that pinched off the window's film. Frame coordinates, screen velocity.
    struct BubbleRelease {
        BubbleMicro micro{};  // as last drawn attached, its neck still narrowing
        Vec2 velocity{};  // px/s
        Vec2 squashRate{};  // its outline still rings from the pinch
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
            double pinchRate = 0;  // per s
            double thickness = 0;
            double seed = 0;
            double growth = 0;  // 0..1 formation; falls back to 0 as it merges into the film
            double collapse = 0;
            Vec2 squash{}, squashRate{};  // outline quadrupole, relative to radius
            Stage stage = Stage::Bud;
            bool pops = false;  // resolves by popping rather than merging back
        };
        // A strand of film drawn out of the trailing side: a broad lamella or a thin thread.
        struct Strand {
            std::uint32_t id = 0;
            double angle = 0;  // outline parameter of its root; slides as the trailing side turns
            double halfWidth = 0;
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
        Outline outline(double angle) const noexcept;
        BubbleMicro draw(const Attached &bubble) const noexcept;
        BubbleBud bud(const Attached &bubble) const noexcept;
        void step(double dt, Vec2 acceleration, double speedRate);
        void nucleate(double dt, Vec2 acceleration);
        void migrate(double dt, Vec2 acceleration);
        void release(const Attached &bubble);
        void emitTrail(double dt, Vec2 acceleration);
        bool startStrand(double angle, double halfWidth, double thickness, double reach);
        // 0..1 how squarely the outline at a parameter faces back against the motion.
        double exposure(double angle) const noexcept;
        double trailingAngle() const noexcept;
        double reference() const noexcept;
        double dragLimit() const noexcept;
        double draw01() noexcept;
        double gauss() noexcept;
        void reshape() noexcept;

        BubbleMotionParams params_{};
        Vec2 size_{};
        Box body_{};
        Vec2 pending_{};
        Vec2 response_{};  // first stage of the velocity filter
        Vec2 velocity_{};  // filtered window velocity, px/s
        Vec2 acceleration_{};  // px/s², as of the last advance
        Vec2 behind_{};  // unit direction the film trails in; turns with the motion, not at once
        double speed_ = 0;
        double clock_ = 0;
        double substep_ = 0;  // unconsumed time below one substep
        Vec2 lag_{}, lagRate_{};  // inertial offset of the trailing fluid, px
        std::array<Vec2, 4> wave_{}, waveRate_{};  // capillary modes (cos, sin), px
        Vec2 pressure_{}, pressureRate_{};  // interior pressure and recoil modes, dimensionless
        double sheen_ = 0;
        double round_ = 0, roundRate_ = 0;  // contraction toward the moving shape
        double hold_ = 0;  // the contraction the movement asks for, held through brief pauses
        double idle_ = 0;  // s the window has been paused
        Vec2 carry_{}, carryRate_{};  // film position behind a layout-animated window, px
        Vec2 carryMark_{};  // carry_ as of the previous advance
        BubbleDeformation shape_{};  // the deformation the state shows, cached per advance
        std::array<double, 4> phase_{};  // buffeting phases
        std::uint64_t random_ = 0x9E3779B97F4A7C15ULL;
        double spawn_ = 0;  // accumulated nucleation hazard
        double threshold_ = 1;  // hazard at which the next microbubble forms
        std::array<Attached, kBubbleMicroCapacity> attached_{};
        std::size_t attachedCount_ = 0;
        std::array<Strand, kBubbleStrands> strands_{};
        std::uint32_t nextStrand_ = 1;  // never reused, so a drying strand is never extended
        double threadHazard_ = 0;  // accumulated hazard of a thread starting at a trailing corner
        std::array<BubbleTrailPoint, kBubbleEmissions> trail_{};
        std::size_t trailCount_ = 0;
        std::array<BubbleRelease, kBubbleEmissions> releases_{};
        std::size_t releaseCount_ = 0;
        bool stirred_ = false;
    };

}  // namespace hyprimate
