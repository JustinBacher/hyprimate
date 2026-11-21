#pragma once

#include <memory>

#include "gl/CompositorContext.hpp"
#include "workspace/planet_hop/PlanetGlRenderer.hpp"

class CShader;
namespace Render {
    class CRenderContext;
}  // namespace Render
namespace hyprimate::workspace {
    struct WorkspaceFrame;
    class PlanetRenderer {
      public:
        PlanetRenderer();
        ~PlanetRenderer();
        PlanetRenderer(const PlanetRenderer &) = delete;
        PlanetRenderer &operator=(const PlanetRenderer &) = delete;
        PlanetRenderer(PlanetRenderer &&) = delete;
        PlanetRenderer &operator=(PlanetRenderer &&) = delete;
        bool initialize();
        void release();
        // Production depth_source is a bounded PNG, prepared only when the cached setting changes.
        bool prepare_depth(const std::string &path) {
            return m_core.prepare_depth(path);
        }
        [[nodiscard]] const std::string &depth_error() const {
            return m_core.depth_error();
        }
        [[nodiscard]] bool has_depth_image() const {
            return m_core.has_depth_image();
        }
        bool draw(Render::CRenderContext & /*ctx*/,
            const WorkspaceFrame & /*frame*/,
            const planet_hop::Journey & /*journey*/,
            double progress,
            Direction /*direction*/,
            bool matching,
            const PlanetRenderSettings & /*settings*/);

      private:
        const std::vector<planet_hop::Vec3> &neighbor_positions(const WorkspaceFrame & /*frame*/,
            const planet_hop::Journey & /*journey*/,
            const planet_hop::Frame & /*scene*/);
        void prepare_neighbors(const WorkspaceFrame & /*frame*/,
            const planet_hop::Journey & /*journey*/,
            const planet_hop::Frame & /*scene*/, double radius);
        gl::CompositorContextOwner m_context;
        PlanetGlRenderer m_core;
        std::array<std::unique_ptr<CShader>, 3> m_shaders;
        std::vector<PlanetFloatingTextures> m_source_floating;
        std::vector<PlanetFloatingTextures> m_destination_floating;
        struct PositionCache {
            // Owner identity only: never keep preview framebuffers alive.
            std::weak_ptr<const void> inventory;
            planet_hop::Layout layout = planet_hop::Layout::Focused;
            // Size, separation, elevation, solar spread, effective monitor aspect.
            std::array<double, 5> key{};
            std::vector<planet_hop::Vec3> positions;
        };
        std::array<PositionCache, 8> m_position_caches{};
        std::size_t m_next_position_cache = 0;
        std::vector<PlanetNeighborTextures> m_neighbors;
    };
}  // namespace hyprimate::workspace
