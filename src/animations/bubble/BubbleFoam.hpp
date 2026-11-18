#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // A small soap bubble as drawn. Opening companions, the movement's microbubbles and a wake's
    // free bubbles are all this one description and one GPU material.
    struct BubbleMicro {
        Vec2 centre{};  // logical px
        double radius = 0;  // logical px
        double growth = 0;  // 0 a cap swelling out of the liquid or parent film .. 1 a formed bubble
        Vec2 anchor{};  // where its neck meets the parent film
        double attach = 0;  // 0..1 strength of that neck; 0 once free
        double collapse = 0;  // 0..1 rupture of its film
        double thickness = 0;  // mean wall thickness, nm; independent of radius
        double seed = 0;  // 0..1
        double flow = 0;  // film-flow phase: its thickness pattern advects with it
        double visibility = 1;
        // Area-preserving quadrupole of the outline, relative to radius: radius(phi) =
        // radius * (1 + squash.x cos 2phi + squash.y sin 2phi). Negative along a pressed contact.
        Vec2 squash{};
    };

    inline constexpr std::size_t BUBBLE_FOAM_CAPACITY = 160;

    // Distance from the centre of a rounded box (half extents `half`, corner radius `corner`) to
    // its outline along unit direction n.
    double bubble_box_reach(Vec2 half, double corner, Vec2 n);

    struct BubbleFoamParams {
        double interaction = 1;  // contact and adhesion strength
        double damping = 1;  // how quickly the liquid calms relative motion
        double buoyancy = 1;  // upward drift
        double linger = .5;  // film lifetime before draining to rupture
    };

    // A large membrane the foam meets: a rounded box that may be growing. The foam pushes it,
    // presses its outline and may merge into it; the response is accumulated in the outputs.
    struct BubbleFoamBody {
        Vec2 centre{}, velocity{};
        Vec2 half{}, previous_half{};  // its shape now and one step earlier (growth pushes films)
        double corner = 0, previous_corner = 0;
        double mass = 0;
        double room = 0;  // projected area it can still take in this step
        // Outputs.
        double absorbed = 0;  // projected area merged into it
        double stir = 0;  // magnitude of contact disturbance, for its pressure mode
        Vec2 quadrupole{};  // contact disturbance of its n = 2 outline mode (cos, sin)
    };

    // The liquid the foam floats on: outflow from where air enters, slowly drifting eddies and a
    // common drift. Correlated motion comes from this field, not per-bubble randomness.
    struct BubbleFoamFlow {
        struct Eddy {
            Vec2 centre{};
            double circulation = 0;  // positive turns clockwise on screen (y down)
            double core = .15;
        };
        Vec2 source{};
        double outflow = 0;  // area per unit time leaving the source
        Vec2 drift{};
        Vec2 confine{};  // half extents of a soft region around the origin; zero: unconfined
        std::array<Eddy, 3> eddies{};
        [[nodiscard]] Vec2 velocity(Vec2 point) const;
    };

    // A raft of small soap bubbles on a liquid. Lengths and times are the owner's units: the
    // tuning assumes a length unit of the window's short side and a time unit of one transition,
    // so movement can reuse it with a reference time. Slots are never compacted: a disappearing
    // film cannot change another film's identity.
    class BubbleFoam {
      public:
        struct Film {
            Vec2 position{}, velocity{};
            double radius = 0;  // volume goes as radius cubed; coalescence conserves it
            double target = 0;  // radius it surfaces to
            double surfacing = 1;  // 0..1: a cap rising out of the liquid .. a free bubble
            double surface_rate = 0;  // per unit time
            double thickness = 0;  // nm at birth
            double seed = 0, phase = 0, flow = 0;
            double age = 0, life = 1;  // drained to rupture once age reaches life
            double burst = 0;  // 0 intact; rises to 1 as its film retracts
            double drainage = 1;  // time the film shared with a touching neighbour takes to drain
                                  // before it ruptures and they coalesce
            double adhesion = 1;
            Vec2 squash{}, squash_rate{};
            int into = -1;  // while coalescing: slot receiving its air, or INTO_BODY
            bool fed = false;  // air enters it: it neither drains nor ruptures
            bool live = false;
        };
        static constexpr int INTO_BODY = -2;

        void reset(std::uint64_t seed, const BubbleFoamParams &params, std::size_t budget);
        // A new film rising at a position; -1 when the budget is full.
        int spawn(Vec2 position, Vec2 velocity, double radius, double thickness, double surfacing,
            double life);
        void inflate(int slot, double volume);
        void set_fed(int slot, bool fed);
        // Advances by dt. The body, when given, is pushed and may absorb films.
        void step(double elapsed, const BubbleFoamFlow &flow, BubbleFoamBody *body);
        // Every intact film ruptures within `within`, each at its own time.
        void expire(double within);
        // Takes a film out of the foam (it became the body) without a visible event.
        Film take(int slot);
        // Rescales positions per axis (the owner's frame changed proportions).
        void scale(Vec2 axis);

        // The intact, unfed film whose outline is nearest the point, or -1.
        [[nodiscard]] int nearest(Vec2 point, double reach) const;
        [[nodiscard]] int largest() const;
        [[nodiscard]] const Film &film(int slot) const { return films[static_cast<std::size_t>(slot)]; }
        Film &film(int slot) { return films[static_cast<std::size_t>(slot)]; }
        [[nodiscard]] std::size_t live_count() const;
        [[nodiscard]] std::size_t capacity() const { return budget; }

        // Drawn films in logical px: position * axis * unit, radius * unit. Writes every slot up to
        // the last live one (dead slots invisible) and returns that count.
        [[nodiscard]] std::size_t write(std::span<BubbleMicro> out, double unit, Vec2 axis, double flow_phase) const;

      private:
        void interact(double elapsed);
        void meet(double elapsed, BubbleFoamBody &body);
        void coalesce(double elapsed, BubbleFoamBody *body);
        std::array<Film, BUBBLE_FOAM_CAPACITY> films{};
        BubbleFoamParams params{};
        std::size_t budget = 0;
        std::uint64_t random = 0;
    };
}  // namespace hyprimate
