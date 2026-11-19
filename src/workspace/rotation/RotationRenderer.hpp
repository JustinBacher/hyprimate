#pragma once

#include <GLES3/gl32.h>
#include <array>
#ifndef HYPRIMATE_ROTATION_STANDALONE
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#endif
#include "workspace/WorkspaceTransition.hpp"
#include "workspace/rotation/GapFill.hpp"

namespace hyprimate::workspace {
    class RotationRenderer {
      public:
        bool initialize();
        void release();
#ifndef HYPRIMATE_ROTATION_STANDALONE
        bool draw(Render::CRenderContext &ctx, const SP<Render::IFramebuffer> &source,
            const SP<Render::IFramebuffer> &destination, const SP<Render::IFramebuffer> &presentation,
            const SP<Render::IFramebuffer> &sourceWallpaper, const SP<Render::IFramebuffer> &destinationWallpaper,
            double position, Direction direction, const GapFill &gap, bool matchingWallpaper);
#endif
        bool drawRaw(GLuint source, GLuint destination, GLuint presentation, GLuint sourceFramebuffer,
            GLuint presentationFramebuffer, int width, int height, double position, Direction direction,
            const GapFill &gap, bool matchingWallpaper, GLuint sourceWallpaper, GLuint destinationWallpaper);
        RotationRenderer() = default;
        RotationRenderer(const RotationRenderer &) = delete;
        RotationRenderer &operator=(const RotationRenderer &) = delete;
        ~RotationRenderer() = default;
        RotationRenderer(RotationRenderer &&) = delete;
        RotationRenderer &operator=(RotationRenderer &&) = delete;
      private:
        GLuint m_program = 0;
        GLuint m_vao = 0;
        std::array<GLint, 13> m_uniforms{};
    };
}
