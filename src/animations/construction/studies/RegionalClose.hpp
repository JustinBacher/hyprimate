#pragma once

#include <array>
#include <optional>
#include <span>

#include "animations/shared/Core.hpp"

namespace hyprimate::construction::studies {

    // A connected deformation of the visible client image, independent of opening layout and
    // capture.
    class RegionalClose {
        static constexpr std::size_t COLUMNS = 28, ROWS = 8;
        static constexpr std::size_t MAX_REGIONS = 5;

      public:
        static constexpr std::size_t MAX_VISIBLE_FACES = 24;

      private:
        static constexpr std::size_t MAX_COLUMNS = COLUMNS + 2 * MAX_VISIBLE_FACES;
        static constexpr std::size_t MAX_ROWS = ROWS + 2 * MAX_VISIBLE_FACES;

      public:
        struct VisibleFace {
            Box source;  // The part that was actually visible, not an uncropped/opening target box.
            Vec3 origin, u,
                v;  // Current posed source rectangle, including any in-flight transform.

            static constexpr VisibleFace flat(Box box) noexcept {
                return {box, {box.x, box.y, 0}, {box.w, 0, 0}, {0, box.h, 0}};
            }
        };

        struct SourceView {
            Box crop;
            bool authorized = false;
        };

        struct Vertex {
            Vec2 source;
            Vec3 position;
        };
        // Indices into source_vertices(), in positive winding in source space (y down).
        using SourceTriangle = std::array<std::uint16_t, 3>;
        struct Flash {
            Vec3 position;
            double radius, intensity;
        };
        struct Dust {
            Vec2 center, radii;
            double opacity;
        };
        struct Worker {
            Vec3 floor, hip, shoulder, head, near_hand, far_hand;
            std::array<Vec3, 2> soles;
            double height, facing, opacity;
        };

        class Frame {
          public:
            [[nodiscard]] std::span<const Vertex> source_vertices() const noexcept {
                return {m_vertices.data(), m_vertex_count};
            }
            [[nodiscard]] std::span<const SourceTriangle> source_faces() const noexcept {
                return {m_faces.data(), m_face_count};
            }
            [[nodiscard]] std::span<const Worker> crew() const noexcept {
                return {m_crew.data(), m_crew_count};
            }
            [[nodiscard]] std::span<const Dust> dust() const noexcept {
                return {m_dust.data(), m_dust_count};
            }
            std::optional<Flash> flash;

          private:
            friend class RegionalClose;
            // Disjoint patches share no grid cells. Each contributes an extra row and
            // column of vertices. Only the four crop edges can split cells, each into
            // two polygons of at most seven vertices (triangle intersect rectangle).
            static constexpr std::size_t MAX_CELLS = MAX_COLUMNS * MAX_ROWS;
            static constexpr std::size_t BORDER_CELLS = 2 * (MAX_COLUMNS + MAX_ROWS);

          public:
            static constexpr std::size_t MAX_VERTICES =
                MAX_CELLS + MAX_VISIBLE_FACES * (MAX_COLUMNS + MAX_ROWS + 1) + 14 * BORDER_CELLS;
            static constexpr std::size_t MAX_TRIANGLES = 2 * MAX_CELLS + 10 * BORDER_CELLS;
            static_assert(
                MAX_VERTICES < (1U << 16U));  // Leave one index unassigned for the row cache.

          private:
            std::array<Vertex, MAX_VERTICES> m_vertices;
            std::array<SourceTriangle, MAX_TRIANGLES> m_faces;
            std::array<Worker, 2> m_crew{};
            std::array<Dust, MAX_REGIONS> m_dust{};
            std::size_t m_vertex_count = 0, m_face_count = 0, m_crew_count = 0, m_dust_count = 0;
        };

        // All coordinates use the same logical capture-relative space, y down. Supply only
        // source faces with disjoint interiors; an empty span means nothing was built/visible.
        // Geometry is copied at handoff; the caller owns authorization, image identity and
        // lifetime.
        bool start(Box body,
            std::span<const VisibleFace> visible,
            std::uint64_t seed,
            double duration) noexcept;
        void advance(double elapsed, bool stalled = false) noexcept;
        void cancel() noexcept;
        [[nodiscard]] bool active() const noexcept {
            return m_duration > 0;
        }

        // Evaluate for the current draw only. Always replace the counts, even when the view is
        // denied. Never replay an earlier frame on revocation; cancel on permanent loss of the
        // image.
        void emit(SourceView view, Frame &out, bool with_dust = true) const noexcept;

      private:
        struct Patch {
            VisibleFace face;
            Box visible;
        };
        struct Drive {
            double start, rate;  // Authored normalized time and travel curve, not a simulation.
            double at(double travel) const noexcept;
            double drop(double time) const noexcept;
        };
        struct Region {
            Drive lower, upper;
            double dust_x, dust_start;
            bool has_dust;
        };
        struct Station {
            Vec3 floor, check;
            double flank, height;
        };

        void emit_crew(Frame &out) const noexcept;

        Box m_body;
        std::array<Patch, MAX_VISIBLE_FACES> m_patches;
        std::array<Region, MAX_REGIONS> m_regions{};
        std::array<double, MAX_REGIONS + 1> m_edges{};
        std::array<Station, 2> m_stations{};
        std::array<double, MAX_COLUMNS + 1> m_x_edges{};
        std::array<double, MAX_ROWS + 1> m_y_edges{};
        std::size_t m_x_count = 0, m_y_count = 0;
        std::size_t m_patch_count = 0, m_region_count = 0;
        Vec2 m_cue_source;
        Vec3 m_cue;
        double m_scale = 0, m_floor = 0, m_fall_distance = 0;
        double m_origin = 0, m_bite = 0, m_fold = 0;
        double m_duration = 0, m_progress = 0;
    };

}  // namespace hyprimate::construction::studies
