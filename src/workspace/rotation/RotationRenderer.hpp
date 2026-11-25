#pragma once

#include <array>
#include <memory>

#include <GLES3/gl32.h>
#ifndef HYPRIMATE_ROTATION_RAW_ONLY
#include "gl/CompositorContext.hpp"
#endif
namespace Hyprutils::Memory {
    template <typename T>
    class CSharedPointer;
}  // namespace Hyprutils::Memory
class CShader;
namespace Render {
    class CRenderContext;
    class IFramebuffer;
}  // namespace Render
#include "workspace/WorkspaceTransition.hpp"
#include "workspace/rotation/GapFill.hpp"

namespace hyprimate::workspace {
    // The caller owns the program, VAO and GL state bracket.
    using RotationUniforms = std::array<GLint, 13>;
    bool rotation_uniforms(GLuint program, RotationUniforms &uniforms);
    bool draw_rotation(GLuint program,
        GLuint vao,
        const RotationUniforms &uniforms,
        GLuint src,
        GLuint dst,
        GLuint out,
        GLuint source_framebuffer,
        GLuint presentation_framebuffer,
        int width,
        int height,
        double position,
        Direction direction,
        const GapFill &gap,
        bool matching_wallpaper,
        GLuint source_wallpaper,
        GLuint destination_wallpaper);

    class RotationRenderer {
      public:
        bool initialize();
        void release();
        bool draw(Render::CRenderContext &ctx,
            const Hyprutils::Memory::CSharedPointer<Render::IFramebuffer> &source,
            const Hyprutils::Memory::CSharedPointer<Render::IFramebuffer> &destination,
            const Hyprutils::Memory::CSharedPointer<Render::IFramebuffer> &presentation,
            const Hyprutils::Memory::CSharedPointer<Render::IFramebuffer> &source_wallpaper,
            const Hyprutils::Memory::CSharedPointer<Render::IFramebuffer> &destination_wallpaper,
            double position,
            Direction direction,
            const GapFill &gap,
            bool matching_wallpaper);
        RotationRenderer();
        RotationRenderer(const RotationRenderer &) = delete;
        RotationRenderer &operator=(const RotationRenderer &) = delete;
        ~RotationRenderer();
        RotationRenderer(RotationRenderer &&) = delete;
        RotationRenderer &operator=(RotationRenderer &&) = delete;

      private:
#ifndef HYPRIMATE_ROTATION_RAW_ONLY
        gl::CompositorContextOwner m_context;
#endif
        std::unique_ptr<CShader> m_shader;
        GLuint m_program = 0;
        GLuint m_vao = 0;
        std::array<GLint, 13> m_uniforms{};
    };
}  // namespace hyprimate::workspace
