#pragma once

#include <GLES3/gl32.h>
#include <array>
#include <span>
#include <memory>
#include <string>
#include <vector>

#include "workspace/WorkspaceTransition.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace Render { class CRenderContext; } // NOLINT(readability-identifier-naming)
namespace hyprimate::workspace {
    struct WorkspaceFrame;
    namespace planet_hop {
        struct Journey;
        struct Frame;
    }

    struct PlanetRenderSettings {
        int quality = 2;
        bool depth_enabled = true;
        float depth_strength = 0.018F;
        bool lighting = true;
        bool clouds = true;
        float cloud_coverage = 0.45F;
        float cloud_opacity = 0.18F;
        float cloud_speed = 0.025F;
        float atmosphere_intensity = 0.3F;
        float atmosphere_thickness = 0.035F;
        float night_brightness = 0.45F;
        float indirect_brightness = 0.18F;
        float star_density = 0.5F;
        float star_parallax = 1.0F;
        std::array<float, 3> light_direction{-0.6F, -0.3F, 1.0F};
        int window_mode = 0;
        int overlay_mode = 2;
        float fade = 0.2F;
        float elevation = 0.2F;
        float duration = 0.8F;
        float solar_background_scale = 0.75F;
        // Mirrored from journey SceneOptions by the native adapter, not new config.
        bool rotation_enabled = true;
        float rotation_limit = 0.35F;
    };

    // Compositor-independent texture view, also consumed by the surfaceless profiler.
    struct PlanetLayerTextures {
        GLuint image = 0;
        GLuint backdrop = 0;
        GLuint previous_image = 0;
        GLuint previous_backdrop = 0;
        float live_mix = 1;
    };
    struct PlanetFloatingTextures {
        PlanetLayerTextures layer;
        std::array<float, 4> rect{};
        bool cropped = true;
        float opacity = 1;
    };
    struct PlanetCaptureTextures {
        GLuint composite = 0;
        GLuint wallpaper = 0;
        PlanetLayerTextures bottom, windows, top;
        std::span<const PlanetFloatingTextures> floating;
        GLuint previous_wallpaper = 0;
        float live_mix = 1;
    };
    // Only real workspace inventory entries reach this interface. Zero image
    // means genuinely unknown, rendered neutrally, never borrowed from a neighbor.
    struct PlanetNeighborTextures {
        JourneyEndpoint endpoint;
        planet_hop::Vec3 center;
        GLuint image = 0;
        bool shared_wallpaper = false;
        GLuint previous_image = 0;
        float live_mix = 1;
        bool image_mipped = false;
        bool previous_mipped = false;
    };
    struct PlanetFrameTextures {
        PlanetCaptureTextures source, destination;
        GLuint output = 0;
        int width = 0;
        int height = 0;
        // Native Hyprland captures store desktop top at texture row zero.
        bool top_first = false;
        bool native_hud = false;
        std::int64_t source_identity = 0;
        std::int64_t destination_identity = 0;
    };

    class PlanetRenderer {
      public:
        PlanetRenderer() = default;
        ~PlanetRenderer() = default;
        PlanetRenderer(const PlanetRenderer &) = delete;
        PlanetRenderer &operator=(const PlanetRenderer &) = delete;
        PlanetRenderer(PlanetRenderer &&) = delete;
        PlanetRenderer &operator=(PlanetRenderer &&) = delete;
        bool initialize();
        void release();
        // CPU-only, called by style preparation. PNG only; invalid paths select procedural relief.
        // The path is cached even on failure, so malformed input is never retried every frame.
        bool prepare_depth(const std::string &path);
        [[nodiscard]] const std::string &depth_error() const { return m_depth_error; }
        [[nodiscard]] bool has_depth_image() const { return !m_depth_pixels.empty(); }
        bool draw(Render::CRenderContext &, const WorkspaceFrame &, const planet_hop::Journey &,
            double progress, Direction, bool matching, const PlanetRenderSettings &);

        // Raw entry points require a current GLES3 context. Caller brackets state/binds output FBO.
        bool initialize_raw();
        void release_raw();
        bool draw_raw(const PlanetFrameTextures &, const planet_hop::Frame &, double progress,
            const PlanetRenderSettings &, std::span<const PlanetNeighborTextures> neighbors = {});

      private:
        struct Program {
            GLuint id = 0;
            std::array<GLint, 48> uniforms{};
        };
        bool prepare_mesh(int quality);
        void upload_depth();
        bool prepare_neutral();
        void draw_quad(const PlanetLayerTextures &primary,
            const std::array<float, 16> &view_projection, const std::array<float, 16> &model,
            float opacity, const std::array<float, 4> &rect = {0,0,1,1}, bool force_opaque = false,
            PlanetLayerTextures secondary = {}, float mix = 0, bool top_first = false);
        Program m_planet, m_quad, m_sky;
        GLuint m_vao = 0;
        struct Mesh {
            GLuint vao = 0, vertices = 0, indices = 0;
            GLsizei count = 0;
        };
        std::array<Mesh, 3> m_meshes{};
        GLuint m_mesh_vao = 0;
        GLuint m_framebuffer = 0;
        struct DepthBuffer { GLuint id = 0; int width = 0; int height = 0; };
        std::array<DepthBuffer, 8> m_depth_buffers{};
        std::size_t m_next_depth_buffer = 0;
        GLuint m_sampler = 0;
        GLuint m_mip_sampler = 0;
        GLuint m_depth_texture = 0;
        GLuint m_neutral_texture = 0;
        GLsizei m_index_count = 0;
        std::vector<PlanetFloatingTextures> m_source_floating;
        std::vector<PlanetFloatingTextures> m_destination_floating;
        bool m_depth_dirty = true;
        struct PositionCache {
            // Owner identity only: never keep preview framebuffers alive.
            std::weak_ptr<const void> inventory;
            planet_hop::Layout layout = planet_hop::Layout::Linear;
            // Size, separation, elevation, solar spread, effective monitor aspect.
            std::array<double, 5> key{};
            std::vector<planet_hop::Vec3> positions;
        };
        std::array<PositionCache, 8> m_position_caches{};
        std::size_t m_next_position_cache = 0;
        std::vector<PlanetNeighborTextures> m_neighbors;
        std::string m_depth_path;
        std::string m_depth_error;
        bool m_depth_prepared = false;
        std::vector<unsigned char> m_depth_pixels;
        int m_depth_width = 0;
        int m_depth_height = 0;
    };
}
