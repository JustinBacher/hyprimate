#pragma once

#include <array>
#include <cstddef>

#include "CubeMaterial.hpp"

namespace hyprimate::workspace::cube::detail {
    constexpr std::size_t STOPS = 20;
    constexpr std::size_t MAX_VERTICES = 3 + (STOPS * (6 + 3));  // sky, sides, one cap fan
    using Rgb = std::array<GLfloat, 3>;
    using Uv = std::array<GLfloat, 2>;
    using Shades = std::array<GLfloat, 4>;
    constexpr Shades FULL_LIGHT{1, 1, 1, 1};
    constexpr std::array<Uv, 4> FACE_UVS{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    constexpr Rgb WHITE{1, 1, 1};

    enum class Chart : GLint { Paint = -1, Source = 0, Destination = 1, Sky = 2 };

    struct Clip {
        GLfloat x;
        GLfloat y;
        GLfloat w;
    };

    using Quad = std::array<Clip, 4>;

    struct Vertex {
        Clip clip;
        Uv uv;
        Rgb tint;
        Chart chart;
    };

    class Mesh {
      public:
        // Push initializes each vertex before it can be part of the uploaded prefix.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
        explicit Mesh(bool backward) : m_backward(backward) {
        }

        void sky() {
            for (const auto &point : std::array<Clip, 3>{{{-1, -1, 1}, {3, -1, 1}, {-1, 3, 1}}}) {
                push({point, {point.x, point.y}, {}, Chart::Sky});
            }
        }

        void triangle(const std::array<Clip, 3> &points, const Rgb &color) {
            for (const auto &point : points) {
                push({point, {}, color, Chart::Paint});
            }
        }

        void quad(
            const Quad &points, Chart chart, const Rgb &color, const Shades &shades = FULL_LIGHT) {
            for (const auto INDEX : {0U, 1U, 2U, 0U, 2U, 3U}) {
                // Reflect travel, not lettering. Keep the original quad diagonal,
                // including on the floor/ceiling where it affects the gradient.
                const auto &uv = FACE_UVS[INDEX];
                const Uv COORD{m_backward ? 1.0F - uv[0] : uv[0], uv[1]};
                const GLfloat LIGHT = shades[INDEX];
                push({points[INDEX],
                    COORD,
                    {color[0] * LIGHT, color[1] * LIGHT, color[2] * LIGHT},
                    chart});
            }
        }

        [[nodiscard]] const Vertex *data() const {
            return m_vertices.data();
        }
        [[nodiscard]] GLsizeiptr bytes() const {
            return static_cast<GLsizeiptr>(m_size * sizeof(Vertex));
        }
        [[nodiscard]] GLsizei count() const {
            return static_cast<GLsizei>(m_size);
        }

      private:
        void push(const Vertex &vertex) {
            m_vertices[m_size++] = vertex;
        }

        // Only the populated prefix is uploaded; no per-frame heap or buffer allocation.
        std::array<Vertex, MAX_VERTICES> m_vertices;
        std::size_t m_size = 0;
        bool m_backward;
    };

    struct Pose {
        double progress;
        double half_height;
        double tilt;
        double zoom;
        Direction direction;
    };

    void build_mesh(Mesh &mesh, const Pose &pose, Variant variant);
}  // namespace hyprimate::workspace::cube::detail
