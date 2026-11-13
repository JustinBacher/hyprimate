#pragma once

#include "Core.hpp"

#include <vector>

namespace liquid_glass {

// A deterministic, single-threaded cloth: a particle grid held by a small patch around one grab point, simulated in
// the frame of the window it belongs to. Lengths are in units of the sheet's geometric mean side, so a sheet of
// pixel size W x H spans size = {W, H} / sqrt(W * H) and keeps its proportions however far it swings. Coordinates
// are x right, y up from the top edge, z towards the viewer.
class ClothSolver {
  public:
    struct Particle {
        Vec3 position{}, velocity{}, rest{};
        bool pinned = false; // held: step() never moves it, so the caller may place it
    };
    struct Forces {
        Vec3   acceleration{}; // uniform: gravity minus the frame's own acceleration
        Vec3   wind{};         // velocity of the still air as seen from the sheet's frame
        Vec3   airOffset{};    // where the frame sits in that air; gusts are fixed in the air, not on the sheet
        double drag = 0;       // per second, along the surface; the face catches FACE_DRAG times more
        double gust = 0;       // turbulent share of the wind speed
        double phase = 0;      // slow evolution of the gust field
        double damping = 0;    // per second, relative to the frame
        double hold = 0;       // per second: the share of its distance from the rest shape the sheet is eased back
    };
    ClothSolver(int cells, Vec2 size, Vec2 grabUV);
    Particle       &particle(int x, int y) { return m_particles[y * (m_cells + 1) + x]; }
    const Particle &particle(int x, int y) const { return m_particles[y * (m_cells + 1) + x]; }
    // The sheet changed size: the pose is rescaled with it.
    void resize(Vec2 size);
    void step(double dt, const Forces &forces);

  private:
    struct Edge {
        int a, b, kind;
    };
    int                   m_cells;
    Vec2                  m_size;
    std::vector<Particle> m_particles;
    std::vector<Edge>     m_edges, m_weave; // every constraint; the weave alone, ordered outwards from the grab
    std::vector<Vec3>     m_previous, m_normals, m_air;
    std::vector<int>      m_tether; // per particle, the held particle nearest to it at rest
};
} // namespace liquid_glass
