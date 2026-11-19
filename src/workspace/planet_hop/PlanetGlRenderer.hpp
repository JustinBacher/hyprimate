#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include <GLES3/gl32.h>

#include "workspace/WorkspaceTransition.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace hyprimate::workspace {
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

    // Programs are borrowed: the native CShader or offscreen harness owns their lifetime.
    struct PlanetPrograms {
        GLuint planet = 0;
        GLuint quad = 0;
        GLuint sky = 0;
    };
    class PlanetGlRenderer {
      public:
        PlanetGlRenderer() = default;
        ~PlanetGlRenderer() = default;
        PlanetGlRenderer(const PlanetGlRenderer &) = delete;
        PlanetGlRenderer &operator=(const PlanetGlRenderer &) = delete;
        PlanetGlRenderer(PlanetGlRenderer &&) = delete;
        PlanetGlRenderer &operator=(PlanetGlRenderer &&) = delete;
        bool initialize_raw(PlanetPrograms programs);
        void release_raw();
        void abandon_raw();
        bool prepare_depth(const std::string &path);
        [[nodiscard]] const std::string &depth_error() const {
            return m_depth_error;
        }
        [[nodiscard]] bool has_depth_image() const {
            return !m_depth_pixels.empty();
        }
        bool draw_raw(const PlanetFrameTextures & /*textures*/,
            const planet_hop::Frame & /*frame*/,
            double progress,
            const PlanetRenderSettings & /*settings*/,
            std::span<const PlanetNeighborTextures> neighbors = {});

      private:
        struct Program {
            GLuint id = 0;
            std::array<GLint, 48> uniforms{};
        };
        bool prepare_mesh(int quality);
        void upload_depth();
        bool prepare_neutral();
        struct DrawContext {
            std::reference_wrapper<const PlanetFrameTextures> textures;
            std::reference_wrapper<const planet_hop::Frame> frame;
            double progress = 0;
            std::reference_wrapper<const PlanetRenderSettings> settings;
            std::reference_wrapper<const std::array<float, 16>> view_projection;
            std::reference_wrapper<const std::array<float, 16>> inverse_projection;
            std::reference_wrapper<const std::array<float, 16>> full_screen;
        };
        struct QuadDraw {
            PlanetLayerTextures primary;
            std::reference_wrapper<const std::array<float, 16>> view_projection;
            std::reference_wrapper<const std::array<float, 16>> model;
            float opacity = 0;
            std::array<float, 4> rect{0, 0, 1, 1};
            bool force_opaque = false;
            PlanetLayerTextures secondary{};
            float mix = 0;
            bool top_first = false;
        };
        struct SurfaceDraw {
            std::reference_wrapper<const PlanetCaptureTextures> capture;
            planet_hop::Vec3 center;
            std::reference_wrapper<const std::array<float, 9>> rotation;
            float envelope = 0;
            PlanetLayerTextures layer;
            int kind = 0;
            float elevation = 0;
            float opacity = 0;
            float radius = 0;
            int quality = -1;
            std::array<float, 3> tint{};
        };
        struct EndpointDraw {
            std::reference_wrapper<const PlanetCaptureTextures> capture;
            planet_hop::Vec3 center;
            std::reference_wrapper<const std::array<float, 9>> rotation;
            float envelope = 0;
            bool source = false;
        };
        struct PreviewSamplers {
            explicit PreviewSamplers(GLuint sampler) : regular(sampler) {
            }
            PreviewSamplers(const PreviewSamplers &) = delete;
            PreviewSamplers &operator=(const PreviewSamplers &) = delete;
            PreviewSamplers(PreviewSamplers &&) = delete;
            PreviewSamplers &operator=(PreviewSamplers &&) = delete;
            GLuint regular = 0;
            bool image = false;
            bool previous = false;
            ~PreviewSamplers();
        };
        void draw_hud(const DrawContext & /*context*/, bool top);
        static float fade_opacity(const DrawContext & /*context*/, bool source);
        struct NeighborView {
            std::array<std::array<double, 5>, 6> planes{};
            double w_radius = 0;
            double pixel_scale = 0;
            float radius = 0;
            float envelope = 0;
            float limit = 0;
            float motion = 0;
        };
        static NeighborView neighbor_view(const DrawContext & /*context*/);
        static int neighbor_quality(const DrawContext & /*context*/,
            const NeighborView & /*view*/,
            const PlanetNeighborTextures & /*neighbor*/);
        bool draw_neighbor(const DrawContext & /*context*/,
            const NeighborView & /*view*/,
            const PlanetNeighborTextures & /*neighbor*/,
            int quality,
            PreviewSamplers & /*sampling*/);
        void draw_quad(const QuadDraw & /*quad*/) const;
        void draw_surface(const DrawContext & /*context*/, const SurfaceDraw & /*surface*/) const;
        void draw_endpoint(const DrawContext & /*context*/);
        void draw_sky(const DrawContext & /*context*/) const;
        bool draw_neighbors(
            const DrawContext & /*context*/, std::span<const PlanetNeighborTextures> /*neighbors*/);
        void draw_layers(const DrawContext & /*context*/, const EndpointDraw & /*endpoint*/);
        void draw_floating(const DrawContext & /*context*/,
            const EndpointDraw & /*endpoint*/,
            const PlanetFloatingTextures & /*item*/);
        bool bind_output(const PlanetFrameTextures & /*textures*/);
        static bool valid_draw(
            const DrawContext & /*context*/, std::span<const PlanetNeighborTextures> /*neighbors*/);
        Program m_planet, m_quad, m_sky;
        GLuint m_vao = 0;
        struct Mesh {
            GLuint vao = 0, vertices = 0, indices = 0;
            GLsizei count = 0;
        };
        std::array<Mesh, 3> m_meshes{};
        GLuint m_mesh_vao = 0;
        GLuint m_framebuffer = 0;
        struct DepthBuffer {
            GLuint id = 0;
            int width = 0;
            int height = 0;
        };
        std::array<DepthBuffer, 8> m_depth_buffers{};
        std::size_t m_next_depth_buffer = 0;
        GLuint m_sampler = 0;
        GLuint m_mip_sampler = 0;
        GLuint m_depth_texture = 0;
        GLuint m_neutral_texture = 0;
        GLsizei m_index_count = 0;
        bool m_depth_dirty = true;
        std::string m_depth_path;
        std::string m_depth_error;
        bool m_depth_prepared = false;
        std::vector<unsigned char> m_depth_pixels;
        int m_depth_width = 0;
        int m_depth_height = 0;
    };
}  // namespace hyprimate::workspace
