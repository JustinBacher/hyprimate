#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "window/paper/PaperSurface.hpp"

struct SPaperVertex {
    float x, y, z, u, v, nx, ny, nz;
};

// CPU-side mesh and its reusable topology and reconstruction scratch.
class CPaperRenderMesh {
  public:
    CPaperRenderMesh() = default;
    CPaperRenderMesh(const CPaperRenderMesh &) = delete;
    CPaperRenderMesh &operator=(const CPaperRenderMesh &) = delete;
    CPaperRenderMesh(CPaperRenderMesh &&) = delete;
    CPaperRenderMesh &operator=(CPaperRenderMesh &&) = delete;

    // Obtain a vertex view only after a successful build. Do not retain it across
    // any subsequent build attempt: even a failed build may invalidate the view.
    bool build(const hyprimate::PaperSurface &surface);
    [[nodiscard]] std::span<const SPaperVertex> vertices() const {
        return m_vertices;
    }

  private:
    struct Edge {
        std::uint32_t a, b;
        std::size_t first, second;
        std::size_t edge;
    };
    struct Neighbor {
        std::size_t corner = SIZE_MAX, edge = 0, other_edge = 0;
    };
    struct Bounds {
        hyprimate::Vec3 low{}, high{};
        std::size_t first = 0, count = 0, left = 0, right = 0;
    };
    void topology(const hyprimate::PaperSurface &surface);
    void reconstruct(const hyprimate::PaperSurface &surface);
    std::size_t build_bounds(
        const hyprimate::PaperSurface &surface, std::size_t first, std::size_t count);
    double clearance(const hyprimate::PaperSurface &surface,
        hyprimate::Vec3 point,
        std::uint32_t endpoint_a,
        std::uint32_t endpoint_b,
        double limit,
        std::size_t branch = 0) const;

    std::vector<SPaperVertex> m_vertices;
    std::vector<SPaperVertex> m_corners, m_midpoints;
    std::vector<Bounds> m_bounds;
    std::vector<std::size_t> m_order;
    std::vector<hyprimate::PaperTriangle> m_faces;
    std::vector<Edge> m_edges;
    std::vector<std::array<Neighbor, 2>> m_neighbors;
    std::vector<std::size_t> m_heads, m_next, m_fan;
    std::vector<double> m_weights;
    std::vector<unsigned char> m_visited;
    std::vector<hyprimate::Vec3> m_normals;
};
