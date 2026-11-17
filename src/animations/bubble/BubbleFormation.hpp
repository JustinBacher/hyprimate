#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animations/bubble/BubbleFoam.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    struct BubbleFrame;
    // Drawn companions: every foam slot, plus the film the window bubble grew from while its
    // membrane takes over.
    inline constexpr std::size_t kBubbleFormationCapacity = kBubbleFoamCapacity + 1;

    struct BubbleFormationParams {
        int count = 140;
        double birthRate = 1;
        double sizeVariation = .75;
        double growthVariation = .6;
        double interaction = 1;
        double damping = 1;
        double buoyancy = 1;
        double linger = .5;
    };

    // The opening: air blown into soapy liquid under the window's place. Small bubbles surface,
    // drift on the liquid's flow, cluster, coalesce and burst; once the air is directed into the
    // bubble sitting over the inlet, it grows by inflation and by swallowing its neighbours until
    // it is the window bubble, which inflates into the window's box and settles. Positions and
    // projected areas use the current short side as their unit; time is transition progress.
    class BubbleFormation {
      public:
        void begin(Vec2 size, std::uint64_t seed, const BubbleFormationParams &params = {});
        void advance(double progress, Vec2 size);
        void writeFrame(BubbleFrame &frame) const;
        bool landed() const;

      private:
        BubbleFormationParams params_{};
        BubbleFoam foam_{};
        Vec2 size_{}, position_{}, velocity_{};
        Vec2 inlet_{}, wander_[2]{}, eddyCentre_[2]{};
        double eddySpin_[2]{};
        double gas_ = 0, reservoir_ = 0, progress_ = 0;
        double birthHazard_ = 0, birthThreshold_ = 1;
        double feedClock_ = 0, feedTarget_ = 1.5, pumpRate_ = 0, inletRate_ = 0;
        double wallPressure_ = 0, corner_ = 0, pace_ = 1, tauStart_ = .05;
        // Streams of rising air around the inlet; each surfaces its bubbles as one clump.
        struct Plume {
            Vec2 offset{};  // from the inlet, short-side units before window proportions
            double strength = 0, target = 0, clock = 0, spin = 0;
        };
        std::array<Plume, 5> plumes_{};
        double core_[2]{}, coreVelocity_[2]{}, wave_[2]{}, waveVelocity_[2]{};
        // The window bubble: absent until the fed film is taken from the foam at `emerged_`.
        bool exists_ = false, expired_ = false;
        double emerged_ = 0;
        int fed_ = -1;
        BubbleFoam::Film origin_{};  // the film it grew from, drawn while its membrane fades in
        std::uint64_t random_ = 0;
        int ticks_ = 0;
        bool landed_ = false;

        void step();
        void nucleate(double dt, double t, double width, double height);
        void feed(double dt, double t);
        void emerge(double t);
        // Supplies air to the window bubble and describes its shape to the foam; returns the corner
        // radius surface tension still holds.
        double inflate(double dt, double t, double width, double height, BubbleFoamBody *body);
    };
}  // namespace hyprimate
