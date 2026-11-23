#pragma once

#include <array>
#include <span>

#include "shared/Core.hpp"

namespace hyprimate::construction::studies {

    // The only published geometry is live client faces and the crew. Source boxes are body-local
    // logical pixels; posed faces and joints are in world logical pixels (y down).
    class PieceRelocation {
        // At most four lower bays (footing + upper) and two spanning crowns.
        static constexpr unsigned MAX_COLUMNS = 4;
        static constexpr std::size_t MAX_PIECES = 2 * MAX_COLUMNS + 2;

      public:
        static constexpr std::size_t MAX_TRIPS = 4;
        static constexpr std::size_t MAX_FACES = 2 * MAX_PIECES;
        static constexpr std::size_t MAX_WORKERS = MAX_TRIPS * 2;

        enum class Placement { Source, Carried, Seated };
        struct Face {
            Box source;
            Vec3 origin, u, v;
            std::size_t piece;
            Placement placement;
        };
        struct Limb {
            Vec3 root, joint, tip;
        };
        struct Worker {
            std::size_t piece;
            Vec3 pelvis, neck, head;
            std::array<Limb, 2> arms, legs;
            std::array<bool, 2> planted;
            double height, facing, visibility;
            bool gripping;
        };

        // Establish the origin of a new observation. A motion sample uses the latest unoffset body;
        // callers reset on a scope change. Incompatible sizes or a stall release to native.
        void reset(Box body, std::uint64_t seed) noexcept;
        void step(Box body, double elapsed, bool held) noexcept;

        [[nodiscard]] bool active() const noexcept {
            return m_count != 0;
        }
        [[nodiscard]] Box body() const noexcept {
            return m_body;
        }
        [[nodiscard]] std::span<const Face> faces() const noexcept {
            return {m_faces.data(), m_face_count};
        }
        [[nodiscard]] std::span<const Worker> workers() const noexcept {
            return {m_workers.data(), m_worker_count};
        }

      private:
        enum class Job { Waiting, Approach, Latch, Carry, Fit, Fasten, Depart, Done };
        struct Rect {
            double left, top, right, bottom;
        };
        struct Basis {
            Vec3 origin, u, v;
        };
        struct Boot {
            Vec3 sole, start, goal;
            double age = 0, duration = 0;
        };
        struct Carrier {
            double socket_x = 0, other_x = 0, facing = 1;
            Vec3 stance;
            std::array<Boot, 2> boots;
        };
        struct Pack {
            std::array<Rect, 2> rects;
            std::array<Carrier, 2> carriers;
            Rect hull;
            Vec2 position, velocity;
            double tilt = 0, roll = 0, age = 0, area = 0;
            unsigned supports = 0;
            std::size_t rect_count = 0, crew = 0;
            Job job = Job::Waiting;
        };

        void cut(Box source) noexcept;
        void add(Rect a, Rect b = {}) noexcept;
        void advance(double dt, bool held) noexcept;
        void reserve() noexcept;
        void update_carriers(Pack &pack, double dt) noexcept;
        void publish() noexcept;
        void clear() noexcept;
        [[nodiscard]] static double grip_amount(const Pack &pack) noexcept;
        [[nodiscard]] Basis basis(const Pack &pack) const noexcept;
        [[nodiscard]] Vec3 station(
            const Pack &pack, const Carrier &carrier, const Basis &pose) const noexcept;

        std::array<Pack, MAX_PIECES> m_packs;
        std::array<Face, MAX_FACES> m_faces;
        std::array<Worker, MAX_WORKERS> m_workers;
        Box m_body;
        Vec2 m_origin, m_landing, m_landing_velocity;
        std::uint64_t m_seed = 0;
        std::size_t m_count = 0, m_seated = 0, m_closure = 0, m_face_count = 0, m_worker_count = 0;
        unsigned m_installed = 0;
        double m_height = 0, m_until_dispatch = 0, m_haul_distance = 0;
    };

}  // namespace hyprimate::construction::studies
