#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <utility>
#include <vector>

#include "animations/paper/PaperContact.hpp"
#include "animations/paper/PaperSurface.hpp"
#include "animations/paper/PaperTopology.hpp"

namespace hyprimate {

    // Worker-owned, graphics-independent thin sheet. Material coordinates and signed plastic
    // hinge memory survive direction changes; event entropy affects applied forces only.
    class PaperCrumple {
      public:
        // Fresh windows use reset/new state. Active reversals retain the material
        // metric even if capture size changes. Cancellation is checked per preparation
        // tick; a cancelled caller must suppress publication of this inactive state.
        void begin(Transition,
            const PaperParams &,
            std::uint64_t seed,
            Vec2 size,
            Vec2 cursor_uv = {0.5, 0.5},
            std::stop_token stop = {});
        bool advance(double seconds);
        bool active() const noexcept;
        bool closed() const noexcept;
        void frame(PaperSurface &) const;
        void reset();

      private:
        enum class Phase : std::uint8_t { Flat, Opening, Closing, Closed };
        struct Grip {
            Vec2 uv;
            double start, angle, lift, strength;
        };
        struct Jaw {
            Vec3 normal;
            double start, scale;
        };
        struct OpeningLoad {
            Vec3 target;
            // Per-iteration share of this region's remaining unfolding removed this tick.
            double rim, gain;
        };

        void choose_forces(std::uint64_t seed, Vec2 cursor);
        bool pack(std::stop_token stop);
        void step(bool closing, bool adapt = true);
        void project_strain();
        void project_enclosure();
        void project_bends(double support_gap);
        double previous_hinge_angle(std::size_t index);
        void flatten();
        bool flat_projection_safe(bool previous = false) const;
        double flat_error_pixels(bool previous = false) const;
        bool flat_and_quiet() const;
        Vec3 rest(Vec2 uv) const noexcept;
        Vec3 force(const paper::Node &) const noexcept;
        double opacity() const noexcept;

        paper::Topology m_topology;
        paper::ContactSolver m_contacts;
        PaperParams m_params{};
        Vec2 m_size{1, 1}, m_material{1, 1}, m_origin{0.5, 0.5};
        Vec2 m_output_size{1, 1};
        std::array<Grip, 6> m_grips{};
        std::array<Jaw, 16> m_jaws{};
        Phase m_phase = Phase::Flat;
        double m_fraction = 0, m_previous_fraction = 0, m_accumulator = 0;
        double m_step = 1.0 / 240.0;
        // Opening platen starts at the packet's height plus flap headroom.
        double m_press_height = 0;
        std::uint64_t m_tick = 0;
        // Capacity-retained per-tick loads; no grip/transcendental work in solver sweeps.
        std::vector<OpeningLoad> m_opening_loads;
        // Closing hand compliance is driven by material strain.
        std::vector<double> m_grip_compliance;
        std::vector<double> m_previous_angles;
        mutable std::vector<std::pair<std::uint64_t, float>> m_crease_scratch;
    };

}  // namespace hyprimate
