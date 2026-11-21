#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "PaperSurface.hpp"

namespace hyprimate::paper {

    struct Node {
        Vec2 uv{};
        Vec3 x{}, prev{}, velocity{};
        // Where an opening found this material point; inserted points interpolate it.
        Vec3 released{};
        double inv_mass = 1;
        unsigned contact_age = 0;
        // Identity survives index compaction; newly inserted material points get fresh IDs.
        std::uint64_t id = 0;
        // Current swept witness only; unlike contact_age, no ring or hysteresis.
        bool contact = false;
    };
    struct Face {
        std::array<std::uint32_t, 3> v{};
    };
    struct Hinge {
        std::uint32_t a = 0, b = 0, c = 0, d = UINT32_MAX;
        double length = 0, rest_angle = 0, memory = 0;
    };

    // UV is the permanent rest metric, not a projection of the deformed surface.
    // Remeshing is conforming midpoint bisection and its exact topological inverse.
    // Active contact triangles are immutable until their contact protection expires.
    class Topology {
      public:
        std::vector<Node> nodes;
        std::vector<Face> faces;
        std::vector<Hinge> hinges;
        Vec2 material_size{1, 1};

        void initialize(Vec2 size, std::uint64_t seed, const PaperParams &params);
        // After editing public nodes/faces, rebuild before using adjacency or adapt.
        // Resets remeshing history and crease state; preserves total movable mass.
        // IDs must be unique; generated IDs advance past the maximum supplied ID.
        // UINT64_MAX marks ID exhaustion and disables further refinement.
        // Invalid indices, repeated vertices/IDs, and non-manifold/winding-conflicted
        // edges throw std::invalid_argument; unrepresentable counts throw
        // std::length_error. Repair and rebuild (or initialize) after rejection.
        void rebuild();
        void adapt(const PaperParams &params, std::uint64_t tick);

      private:
        struct EdgeState {
            std::uint64_t a = 0, b = 0;
            double rest = 0, memory = 0;
        };
        struct EdgeRecord {
            std::uint32_t a = 0, b = 0, opposite = 0;
            std::size_t face = 0;
            unsigned side = 0;
            bool forward = false;
        };
        struct StoredState {
            EdgeState state{};
            std::size_t order = 0;
        };
        struct Split {
            std::uint64_t a = 0, b = 0, midpoint = 0, tick = 0;
            std::vector<std::array<std::uint64_t, 3>> parents, children;
        };
        std::vector<Split> splits_;
        std::uint64_t next_id_ = 1;
        double base_length_ = 0;
        bool adjacency_valid_ = true;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> ids_;
        std::vector<std::size_t> face_offsets_, hinge_offsets_, vertex_faces_, vertex_hinges_;
        std::vector<std::size_t> incidence_cursor_;
        std::vector<EdgeRecord> edge_scratch_;
        std::vector<StoredState> state_scratch_;
        std::vector<double> area_scratch_;
        std::vector<std::array<std::size_t, 2>> edge_faces_;
        std::vector<std::array<std::size_t, 3>> face_edges_;
        std::size_t adjacency_nodes_ = 0;
        struct Candidate {
            double score = 0;
            std::size_t target = 0, order = 0;
        };
        std::vector<unsigned> distance_scratch_;
        std::vector<double> strain_scratch_;
        std::vector<Candidate> candidate_scratch_;

        [[nodiscard]] std::uint32_t node_index(std::uint64_t id) const;
        [[nodiscard]] std::span<const std::size_t> incident_faces(std::uint32_t vertex) const {
            return {vertex_faces_.data() + face_offsets_[vertex],
                face_offsets_[vertex + 1] - face_offsets_[vertex]};
        }
        [[nodiscard]] std::span<const std::size_t> incident_hinges(std::uint32_t vertex) const {
            return {vertex_hinges_.data() + hinge_offsets_[vertex],
                hinge_offsets_[vertex + 1] - hinge_offsets_[vertex]};
        }
        [[nodiscard]] double rest_length(std::uint32_t a, std::uint32_t b) const;
        [[nodiscard]] std::vector<EdgeState> edge_states() const;
        void rebuild(const std::vector<EdgeState> &states, double total_mass);
        [[nodiscard]] std::size_t refinement_edge(std::size_t index, unsigned depth = 0) const;
        [[nodiscard]] bool split_edge(std::size_t index, std::uint64_t tick);
        [[nodiscard]] bool collapse_split(std::size_t index);
    };

}  // namespace hyprimate::paper
