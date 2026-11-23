#pragma once

#include <GLES3/gl3.h>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace::cube {
    enum class Variant { Solid, InsideOut };

    struct Settings {
        Variant variant;
        float tilt_degrees;
        float zoom;
    };

    // The caller supplies the current context and raw GL bracket (two texture units).
    // Draw uses the bound presentation framebuffer and Hyprland's top-first captures.
    class Material {
      public:
        Material() = default;
        Material(const Material &) = delete;
        Material &operator=(const Material &) = delete;
        Material(Material &&) = delete;
        Material &operator=(Material &&) = delete;
        ~Material() = default;

        bool initialize();
        void release();
        void abandon();
        bool draw(GLuint source,
            GLuint destination,
            GLuint output_texture,
            int width,
            int height,
            double progress,
            Direction direction,
            const Settings &settings);

      private:
        GLuint m_program = 0;
        GLuint m_vao = 0;
        GLuint m_buffer = 0;
        GLuint m_sampler = 0;
    };
}  // namespace hyprimate::workspace::cube
