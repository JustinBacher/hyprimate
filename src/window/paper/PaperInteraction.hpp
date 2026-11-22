#pragma once

#include <array>

#include "PaperSurface.hpp"
#include "window/WindowAnimationFacts.hpp"

namespace hyprimate {

    enum class PaperMoveStyle : std::uint8_t { Flutter, Glide };

    struct PaperInteractionParams {
        double stiffness = 0.75, inertia = 0.45, settle_damping = 0.8;
        PaperMoveStyle style = PaperMoveStyle::Flutter;
        struct Flutter {
            double air_resistance = 0.65, intensity = 0.5, frequency = 1.2;
            double lift = 0.3, torsion = 0.4;
        } flutter;
        struct Glide {
            double friction = 0.6, corner_lift = 0.25, edge_curl = 0.3;
            double shadow_separation = 0.2;
        } glide;
        struct Resize {
            double fold_intensity = 0.6, curl_intensity = 0.45, crease_density = 0.5;
        } resize;
    };

    // Fixed shared UV grid. Bending rotates unit material tangents; metric
    // projection is needed only where independently driven folds cross.
    class PaperInteraction {
      public:
        bool advance(PaperSurface &, const PaperInteractionParams &, const animation::Step &);
        // Samples the same piecewise-linear triangles uploaded by the renderer.
        // Precondition: surface is an interaction_mesh produced by this class.
        [[nodiscard]] static Vec3 sample(const PaperSurface &, Vec2 uv);

      private:
        static constexpr unsigned CELLS = 24;
        static constexpr unsigned SIDE = CELLS + 1;
        static constexpr unsigned COUNT = SIDE * SIDE;
        struct Response {
            double drive = 0, value = 0;
            void advance(double target, double rate, double seconds);
        };
        struct Movement {
            std::array<double, CELLS> horizontal{}, vertical{};
            double lift = 0, torsion = 0, corner = 0, corner_side = 0;
            double shadow_separation = 1;
            bool vertical_corner = false;
        };
        std::array<Response, 4> m_grow{}, m_shrink{};
        std::array<Response, 2> m_velocity{}, m_acceleration{};
        Vec2 m_previous_velocity{};
        double m_phase = 0;
        bool m_initialized = false;

        void mesh(PaperSurface &);
        [[nodiscard]] Movement flutter(const PaperInteractionParams &) const;
        [[nodiscard]] Movement glide(const PaperInteractionParams &) const;
        void deform(PaperSurface &, const PaperInteractionParams &, Vec2 material);
    };
}  // namespace hyprimate
