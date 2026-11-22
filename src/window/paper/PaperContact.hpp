#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "window/paper/PaperSurface.hpp"

namespace hyprimate::paper {
    class Topology;

    // Self-contact for a linearly predicted substep. prev is never modified. Scratch
    // indices live only within solve(); remeshing between calls is always safe.
    class ContactSolver {
      public:
        // Optional elastic projection is interleaved between changed contact
        // passes. It may modify x only: no remeshing, prev or mass changes.
        // Fresh swept discovery always follows it; final projection is contact.
        void solve(Topology &mesh,
            double thickness,
            double friction,
            void (*elasticity)(void *) = nullptr,
            void *context = nullptr);
        void reset();

      private:
        struct Bounds {
            Vec3 lo{}, hi{};
            std::uint32_t face = 0;
        };
        // Six fixed diagonal separating axes supplement the XYZ sweep boxes.
        struct DiagonalBounds {
            std::array<double, 6> lo{}, hi{};
        };
        struct Constraint {
            std::array<std::uint32_t, 4> node{};
            std::array<double, 4> weight{};
            Vec3 normal{}, tangent{};
            double impulse = 0;
            // Relative material position at TOI; pre-impact free slip is excluded.
            Vec3 previous{};
            double inverse_mass = 0;
        };
        std::vector<Bounds> bounds_;
        std::vector<Bounds> node_bounds_;
        std::vector<DiagonalBounds> node_diagonal_, face_diagonal_, edge_diagonal_;
        // Direct VF and unique-edge sweeps emit each physical feature once.
        std::vector<std::array<std::uint32_t, 5>> features_;
        std::vector<std::array<std::uint32_t, 2>> edges_;
        std::vector<std::uint32_t> active_;
        std::vector<Constraint> contacts_;
        std::vector<unsigned char> protected_;
        // Per-solve impact zones; retain capacity, never reuse node identities.
        std::vector<Vec3> impact_goal_, impact_transport_;
        std::vector<std::uint32_t> impact_parent_;
        std::vector<double> impact_fraction_, impact_mass_;
        std::vector<unsigned char> impact_fixed_;

        void broadphase(const Topology &mesh, double thickness);
        void narrowphase(const Topology &mesh, double thickness);
        void resolve_impacts(Topology &mesh, double thickness);
    };
}  // namespace hyprimate::paper
