#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "shared/Core.hpp"

namespace hyprimate {
    enum class BurnDirection : std::uint8_t { Natural, Fall, Rise, Wind };
    struct BurnParams {
        double duration = 1.5;
        double variation = .7, ember_density = .65, fragment_density = .55;
        double smoke_density = .22;  // Movement combustion only.
        double wind_direction = 0, wind_strength = 100, gravity_strength = 300;
        double curl = .8, front_width = .055;
        double scorch_lifetime = 1.4, healing_duration = 1.8;
        BurnDirection direction = BurnDirection::Natural;
        bool consume = false;
    };
    struct BurnFront {
        Box capture;
        // Visible threshold, increasing on open and decreasing on close.
        double progress = 1;
        bool closing = false;
        std::uint64_t seed = 0;
    };
    struct BurnFragment {
        Vec2 position, size;
        Box uv;
        double depth = 0;
        Vec3 rotation;
        double heat = 1, opacity = 1;
        unsigned layer = 0;
        Vec2 velocity;
        Vec3 spin;
        double age = 0, lifetime = 1;
    };
    struct BurnParticle {
        Vec2 position;
        double size = 1, heat = 1, opacity = 1;
        bool ember = true;
        Vec2 velocity;
        double age = 0, lifetime = 1;
    };
    constexpr float burn_phase(std::uint64_t seed) noexcept {
        // Consecutive compositor owners should not repeat the preceding outline.
        return static_cast<float>((seed * 2654435761ULL) & 4095U) * .013F;
    }
    // All masks and emission sites use the same material arrival time.
    double burn_arrival(Vec2 uv, std::uint64_t seed, double variation = .7) noexcept;
    class BurnSimulation {
      public:
        static constexpr std::size_t MAX_FRAGMENTS = 192, MAX_PARTICLES = 768;
        void open(Box capture, const BurnParams &params, std::uint64_t seed) noexcept;
        void close(Box capture, const BurnParams &params, std::uint64_t seed) noexcept;
        void advance(double elapsed, Box capture) noexcept;
        void reset() noexcept;
        [[nodiscard]] const BurnFront &front() const noexcept {
            return m_front;
        }
        [[nodiscard]] const BurnParams &params() const noexcept {
            return m_params;
        }
        [[nodiscard]] std::span<const BurnFragment> fragments() const noexcept {
            return {m_fragments.data(), m_fragment_count};
        }
        [[nodiscard]] std::span<const BurnParticle> particles() const noexcept {
            return {m_particles.data(), m_particle_count};
        }
        [[nodiscard]] Box bounds() const noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool initialized() const noexcept {
            return m_initialized;
        }

      private:
        void integrate(double dt) noexcept;
        void emit(double previous) noexcept;
        void fragment(Vec2 uv, Vec2 size, unsigned layer = 0) noexcept;
        void particle(Vec2 position, bool ember) noexcept;
        [[nodiscard]] Vec2 acceleration(bool ember) const noexcept;
        BurnFront m_front;
        BurnParams m_params;
        std::array<BurnFragment, MAX_FRAGMENTS> m_fragments{};
        std::array<BurnParticle, MAX_PARTICLES> m_particles{};
        std::size_t m_fragment_count = 0, m_particle_count = 0;
        std::uint64_t m_rng = 0;
        double m_emission_credit = 0, m_fragment_credit = 0;
        bool m_initialized = false;
    };
}  // namespace hyprimate
