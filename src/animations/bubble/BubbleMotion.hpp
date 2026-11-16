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

    struct BubbleMotionParams {
        double strength = 0;  // scales every deformation; 0 leaves movement undeformed
        double microbubbles = 0;  // 0..1 how readily microbubbles form on the film
        double trail = 0;  // 0..1 how much film the moving window leaves behind
    };

    // The movement's share of a flat membrane's shape, in logical px. The renderer displaces the
    // undeformed membrane by bubbleMotionOffset; the GPU material mirrors it.
    struct BubbleDeformation {
        Vec2 drag{};  // displacement of the trailing film: it points at the side that lags
        std::array<double, 4> waveCos{}, waveSin{};  // outward capillary modes n = 2..5
        double sheen = 0;  // 0..1 interference colour on the stressed film near the outline

        bool still() const noexcept;
    };

    // Where a point of the undeformed membrane (logical px from its centre, y down) is carried.
    // The leading edge stays with the window; the trailing film lags by drag and narrows across the
    // motion, and the capillary modes ripple the outline while the centre stays put.
    Vec2 bubbleMotionOffset(Vec2 halfSize, const BubbleDeformation &deformation, Vec2 point);

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
        // Window translation since the previous advance, logical px.
        void drive(Vec2 displacement) noexcept;
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
        BubbleMicro draw(const Attached &bubble) const noexcept;
        void step(double dt, Vec2 acceleration, double speedRate);
        void nucleate(double dt, Vec2 acceleration);
        void migrate(double dt, Vec2 acceleration);
        void emitTrail(double dt, Vec2 acceleration);
        double trailingAngle() const noexcept;
        double reference() const noexcept;
        double dragLimit() const noexcept;
        double draw01() noexcept;
        void reshape() noexcept;

        BubbleMotionParams params_{};
        Vec2 size_{};
        Box body_{};
        Vec2 pending_{};
        Vec2 response_{};  // first stage of the velocity filter
        Vec2 velocity_{};  // filtered window velocity, px/s
        double speed_ = 0;
        double clock_ = 0;
        double substep_ = 0;  // unconsumed time below one substep
        Vec2 lag_{}, lagRate_{};  // inertial offset of the trailing fluid, px
        std::array<Vec2, 4> wave_{}, waveRate_{};  // capillary modes (cos, sin), px
        Vec2 pressure_{}, pressureRate_{};  // interior pressure and recoil modes, dimensionless
        double sheen_ = 0;
        BubbleDeformation shape_{};  // the deformation the state shows, cached per advance
        std::array<double, 4> phase_{};  // buffeting phases
        std::uint64_t random_ = 0x9E3779B97F4A7C15ULL;
        double spawn_ = 0;  // accumulated nucleation hazard
        double threshold_ = 1;  // hazard at which the next microbubble forms
        std::array<Attached, kBubbleMicroCapacity> attached_{};
        std::size_t attachedCount_ = 0;
        double travel_ = 0;  // px since the last trail point
        bool trailing_ = false;
        std::array<BubbleTrailPoint, kBubbleEmissions> trail_{};
        std::size_t trailCount_ = 0;
        std::array<BubbleRelease, kBubbleEmissions> releases_{};
        std::size_t releaseCount_ = 0;
        bool stirred_ = false;
    };

}  // namespace hyprimate
