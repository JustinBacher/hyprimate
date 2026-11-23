#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "WindowAnimationFacts.hpp"

namespace hyprimate::construction::studies {

    enum class IntactTransportKind { CrewCarry, Crane };

    // Positions are world-logical (+y down); source is the latest unposed body, never an image.
    struct IntactTransportFace {
        Box source;
        Vec2 origin, u, v;
        double lift = 0;

        [[nodiscard]] Vec2 point(Vec2 body_local) const noexcept;
    };

    struct CarryLegPose {
        Vec3 hip, knee, ankle, sole;
        double boot_pitch = 0;
        bool planted = false;
    };

    struct CarryArmPose {
        Vec3 shoulder, elbow, wrist, fingertips, palm;
        std::optional<Vec2> grip;  // Present only when the actual palm meets the face.
    };

    struct CarryBearerPose {
        std::array<CarryLegPose, 2> legs;  // Near, far.
        std::array<CarryArmPose, 2> arms;
        Vec3 hips, neck, head;
        Vec2 station;
        double height = 0, opacity = 0;
        int facing = 1;
        unsigned vest = 0;
        bool behind_face = false;
    };

    struct CraneWirePose {
        Vec2 sheave, midpoint, end;
        double width = 0;
        bool attached = false;
        bool taut = false;
    };

    struct CarryCranePose {
        Vec2 track, heel, collar, nose, sheave, ram_base, ram_tip;
        Box bounds, track_shadow, chassis, cab, ballast;
        std::array<Vec2, 4> wheels, tread_marks;
        std::array<CraneWirePose, 2> wires;  // Left and right corner; no spreader or third wire.
        std::size_t wire_count = 0;
        double lever = 0, extension = 0;
        int facing = 1;  // Boom faces into the apron; the crawler backs out.
        bool departing = false;
    };

    class IntactTransport {
      public:
        explicit IntactTransport(IntactTransportKind kind, std::uint64_t seed) noexcept;

        // An owner constrains a mapped crane to one drawable apron and an exposed exit.
        // Returns false only when that mapped route cannot be used; no owner preserves the study.
        bool step(const animation::Step &step, std::span<const Box> outputs = {},
            std::optional<Box> owner = {}) noexcept;
        [[nodiscard]] bool active() const noexcept {
            return m_active;
        }
        [[nodiscard]] const IntactTransportFace &face() const noexcept {
            return m_face;
        }
        [[nodiscard]] std::span<const CarryBearerPose> bearers() const noexcept {
            return {m_bearers.data(), m_bearer_count};
        }
        [[nodiscard]] const std::optional<CarryCranePose> &crane() const noexcept {
            return m_crane_pose;
        }
        [[nodiscard]] Box load_shadow() const noexcept {
            return m_shadow;
        }

      private:
        enum class Route { Waiting, Approach, Grip, Leave, Gone };
        struct Foot {
            Vec3 sole, origin;
            double elapsed = 0, duration = 0, pitch = 0, strike_x = 0;
        };
        struct Bearer {
            std::array<Vec2, 2>
                sockets;  // Fractions of the body, kept through a concurrent resize.
            Vec2 stance_offset, staging, root, start, end, velocity;
            std::array<Foot, 2> feet;
            double height = 0, delay = 0, stride = 0, trigger = 0;
            double age = 0, duration = 0, alpha = 0, reach = 0, brace = 0, shift = 0;
            double initial_alpha = 0, initial_reach = 0;
            int facing = 1, swing = -1, next_foot = 0, contacts = 0;
            unsigned vest = 0;
            Route route = Route::Waiting;
            bool side = false, side_pair = false, rear = false;

            [[nodiscard]] Vec2 station(Box body) const noexcept;
            [[nodiscard]] Vec3 neutral(int i) const noexcept;
            void enter(Vec2 target, bool fresh) noexcept;
            void walk(double dt, Vec2 velocity) noexcept;
            void advance(
                double dt, Box body, bool working, double age, double settle, double exit) noexcept;
            void publish(
                CarryBearerPose &pose, const IntactTransportFace &face, double pressure) noexcept;
        };
        struct Crane {
            Vec2 base, base_v, aim, aim_v, pin;
            std::array<Vec2, 2> anchor;
            double extend = 0, payout = 0, slack = 1, lift = 0;
            double home_bottom = 0;
            int side = 1;
            bool attached = false;
        };

        void reset() noexcept;
        void pose_face(double lift, double radians) noexcept;
        void place_crew(Box apron) noexcept;
        void advance_crew(
            double dt, Vec2 velocity, bool working, bool regrab, bool resizing) noexcept;
        bool advance_crane(double dt,
            Box apron,
            std::span<const Box> outputs,
            bool mapped,
            Vec2 velocity,
            bool working,
            bool fresh,
            bool pickup_started,
            bool resizing) noexcept;

        static constexpr std::size_t MAX_BEARERS = 6;
        const IntactTransportKind m_kind;
        const std::uint64_t m_seed;
        IntactTransportFace m_face;
        Box m_shadow;
        std::array<Bearer, MAX_BEARERS> m_crew;
        std::array<CarryBearerPose, MAX_BEARERS> m_bearers;
        std::size_t m_crew_count = 0, m_bearer_count = 0;
        unsigned m_crew_roster = 0;
        Crane m_crane;
        std::optional<CarryCranePose> m_crane_pose;
        Vec2 m_velocity;
        double m_work_age = 0, m_release_age = 0, m_coast = 0, m_pickup_remaining = 0;
        double m_pressure = 0, m_release_pressure = 0, m_settle = 0, m_exit = 0;
        double m_angle = 0, m_angular_velocity = 0;
        animation::WorkspaceId m_workspace = 0;
        bool m_active = false, m_working = false, m_was_held = false, m_observed = false;
        bool m_drag_translated = false;
    };

}  // namespace hyprimate::construction::studies
