#pragma once

#include <array>
#include <memory>
#include <optional>

#include <GLES3/gl32.h>

#ifdef HYPRIMATE_LAVA_RAW_ONLY
#include "gl/EglContext.hpp"
#else
#include "gl/CompositorContext.hpp"
#endif
#include "workspace/WorkspaceTransition.hpp"

#ifndef HYPRIMATE_LAVA_RAW_ONLY
class CShader;
#endif

namespace hyprimate::workspace {
    struct WorkspaceFrame;

    namespace lava {
        // Prepared by the style; inactive variants do not contribute to these packs.
        struct Material {
            int variant = 0;
            // NOLINTBEGIN(readability-identifier-naming) Match the shader uniform pack names.
            std::array<float, 4> sharedA{};
            std::array<float, 4> sharedB{};
            std::array<float, 4> specialA{};
            std::array<float, 2> specialB{};
            // NOLINTEND(readability-identifier-naming)
            bool burn_up = false;

            bool operator==(const Material &) const = default;
        };

        // Each plane carries native accumulated RGB and this pass's coverage in alpha.
        // With these planes, the full scene images carry top-pass coverage in alpha.
        struct WindowPlanes {
            GLuint source_bottom = 0;
            GLuint source_windows = 0;
            GLuint destination_bottom = 0;
            GLuint destination_windows = 0;
        };

        class Renderer {
          public:
            Renderer();
            ~Renderer();
            Renderer(const Renderer &) = delete;
            Renderer &operator=(const Renderer &) = delete;
            Renderer(Renderer &&) = delete;
            Renderer &operator=(Renderer &&) = delete;

            bool initialize();
            void release();
#ifndef HYPRIMATE_LAVA_RAW_ONLY
            bool draw(const WorkspaceFrame &frame,
                double progress,
                Direction direction,
                const Material &material);
#endif

            // Requires the owning GLES context current. The three images are distinct 2D
            // textures of the given extent; output is level zero at the framebuffer's sole
            // color attachment (COLOR_ATTACHMENT0). Caller GL state is restored on return.
            // Burn requires four matching linear RGBA planes, none aliased with output.
            // Without native allocation metadata, burn validation needs GLES 3.1 level queries.
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            bool draw_raw(GLuint source,
                GLuint destination,
                GLuint output,
                GLuint framebuffer,
                int width,
                int height,
                double progress,
                Direction direction,
                const Material &material,
                WindowPlanes planes = {});

          private:
            bool initialize_bindings();
            void release_objects();
            void draw_material(int width,
                int height,
                double progress,
                Direction direction,
                const Material &material);

            struct Uniforms {
                GLint source_image = -1;
                GLint destination_image = -1;
                GLint source_bottom = -1;
                GLint source_windows = -1;
                GLint destination_bottom = -1;
                GLint destination_windows = -1;
                GLint burn_up = -1;
                GLint size = -1;
                GLint progress = -1;
                GLint direction = -1;
                GLint variant = -1;
                GLint shared_a = -1;
                GLint shared_b = -1;
                GLint special_a = -1;
                GLint special_b = -1;
            };

#ifdef HYPRIMATE_LAVA_RAW_ONLY
            gl::EglContextId m_context;
#else
            gl::CompositorContextOwner m_context;
            std::unique_ptr<CShader> m_shader;
#endif
            GLuint m_program = 0;
            GLuint m_vao = 0;
            Uniforms m_uniforms;
            std::optional<Material> m_bound_material;
        };
    }  // namespace lava
}  // namespace hyprimate::workspace
