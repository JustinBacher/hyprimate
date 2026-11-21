#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "BurnDynamics.hpp"
#include "WindowAnimationFacts.hpp"

// Independent wallpaper damage. No client or retained window references live here.
class CBurnGround {
  public:
    using MonitorId = hyprimate::animation::MonitorId;
    using ScreenFacts = hyprimate::animation::ScreenFacts;
    static constexpr std::size_t WIDTH = 96, HEIGHT = 64, MAX_AREAS = 8, MAX_PARTICLES = 128;
    struct Cell {
        double age = 0, lifetime = 0, healing = 0;
        float severity = 0;
        [[nodiscard]] float damage() const noexcept;
        [[nodiscard]] float heat() const noexcept;
        [[nodiscard]] float movement_damage() const noexcept;
        [[nodiscard]] float movement_heat() const noexcept;

      private:
        friend class CBurnGround;
        float kindle(std::uint16_t bit, std::size_t index, float stay, float recover) noexcept;
        std::uint16_t m_coverage = 0, m_regrown = 0;
        struct Movement {
            float age = 0, lifetime = 0, healing = 0, severity = 0, regrowth = 0;
        } m_movement;
    };
    struct Particle {
        enum class Kind : std::uint8_t { Paper, Smoke, Ember };
        std::uint64_t owner = 0;
        Kind kind = Kind::Paper;
        bool window_owned = false;
        hyprimate::Vec2 position, source;  // World logical; source stays at the point of ignition.
        double age = 0, lifetime = 1, size = 1;  // Size is a logical bounding radius.
        double angle = 0, fold = 1, char_depth = 0, shape = 0;
        hyprimate::Vec2 velocity, terminal;
        double flutter = 0, drag = 1, spin = 0;
        hyprimate::Edge edge = hyprimate::Edge::Left;
        double held_until = 0, edge_fraction = 0, edge_gap = 0;
    };
    struct Opening {
        hyprimate::BurnFront front;
        double width = .055, variation = .7, untouched = 0;
    };
    struct Area {
        MonitorId monitor = 0;
        hyprimate::animation::WorkspaceId workspace = 0;
        hyprimate::Box screen;
        std::array<Cell, WIDTH * HEIGHT> cells{};
        std::array<Particle, MAX_PARTICLES> particles{};
        std::size_t particle_count = 0;
        std::array<Opening, 32> openings{};
        std::size_t opening_count = 0;
        hyprimate::animation::Clock::time_point last{};
        bool clock_started = false;
        bool erase_pending = false;
        [[nodiscard]] hyprimate::Vec2 position(std::size_t x, std::size_t y) const noexcept;
        [[nodiscard]] bool active() const noexcept;
    };
    void stamp(const hyprimate::BurnFront &front,
        const hyprimate::BurnParams &params,
        const hyprimate::animation::Step &step,
        bool opening = true);
    void tick(const ScreenFacts &screen);
    void cancel(std::uint64_t owner) noexcept;
    void clear(std::optional<MonitorId> monitor = std::nullopt) noexcept;
    [[nodiscard]] bool active(MonitorId monitor) const noexcept;
    [[nodiscard]] std::span<const std::unique_ptr<Area>> areas() const noexcept {
        return m_areas;
    }

  private:
    struct Owner {
        std::uint64_t seed = 0;
        double progress = 0;
    };
    void stamp_area(const hyprimate::BurnFront &front,
        const hyprimate::BurnParams &params,
        const hyprimate::animation::Step &step,
        bool opening,
        double previous);
    void stamp_movement(Area &area,
        std::uint64_t seed,
        hyprimate::animation::WindowId window,
        const hyprimate::BurnParams &params,
        const hyprimate::animation::Motion &motion);
    void cancel_owner(std::uint64_t seed, hyprimate::animation::WindowId window) noexcept;
    std::array<ScreenFacts, MAX_AREAS> m_screens{};
    Area *area_for(const hyprimate::animation::Step &step);
    std::array<std::unique_ptr<Area>, MAX_AREAS> m_areas;
    std::array<Owner, 256> m_owners{};
    std::size_t m_next_owner = 0;
    std::uint64_t m_rng = 1;
};
