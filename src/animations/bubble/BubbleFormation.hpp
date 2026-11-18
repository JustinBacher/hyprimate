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
    inline constexpr std::size_t kBubbleFormationCapacity = BUBBLE_FOAM_CAPACITY + 1;

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
        [[nodiscard]] bool landed() const;

      private:
        BubbleFormationParams m_params{};
        BubbleFoam m_foam{};
        Vec2 m_size{}, m_position{}, m_velocity{};
        Vec2 m_inlet{}, m_wander[2]{}, m_eddy_centre[2]{};
        double m_eddy_spin[2]{};
        double m_gas = 0, m_reservoir = 0, m_progress = 0;
        double m_birth_hazard = 0, m_birth_threshold = 1;
        double m_feed_clock = 0, m_feed_target = 1.5, m_pump_rate = 0, m_inlet_rate = 0;
        double m_wall_pressure = 0, m_corner = 0, m_pace = 1, m_tau_start = .05;
        // Streams of rising air around the inlet; each surfaces its bubbles as one clump.
        struct Plume {
            Vec2 offset{};  // from the inlet, short-side units before window proportions
            double strength = 0, target = 0, clock = 0, spin = 0;
        };
        std::array<Plume, 5> m_plumes{};
        double m_core[2]{}, m_core_velocity[2]{}, m_wave[2]{}, m_wave_velocity[2]{};
        // The window bubble: absent until the fed film is taken from the foam at `m_emerged`.
        bool m_exists = false, m_expired = false;
        double m_emerged = 0;
        int m_fed = -1;
        BubbleFoam::Film m_origin{};  // the film it grew from, drawn while its membrane fades in
        std::uint64_t m_random = 0;
        int m_ticks = 0;
        bool m_landed = false;

        void step();
        void nucleate(double step, double progress, double width, double height);
        void feed(double step, double progress);
        void emerge(double progress);
        // Supplies air to the window bubble and describes its shape to the foam; returns the corner
        // radius surface tension still holds.
        double inflate(double step, double progress, double width, double height, BubbleFoamBody *body);
    };
}  // namespace hyprimate
