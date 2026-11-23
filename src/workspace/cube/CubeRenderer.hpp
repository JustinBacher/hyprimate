#pragma once

#include "gl/CompositorContext.hpp"
#include "workspace/cube/CubeMaterial.hpp"

namespace Render {
    class CRenderContext;
}

namespace hyprimate::workspace {
    struct WorkspaceFrame;

    class CubeRenderer {
      public:
        CubeRenderer() = default;
        ~CubeRenderer();
        CubeRenderer(const CubeRenderer &) = delete;
        CubeRenderer &operator=(const CubeRenderer &) = delete;
        CubeRenderer(CubeRenderer &&) = delete;
        CubeRenderer &operator=(CubeRenderer &&) = delete;

        bool initialize();
        void release();
        bool draw(Render::CRenderContext &context,
            const WorkspaceFrame &frame,
            double progress,
            Direction direction,
            const cube::Settings &settings);

      private:
        gl::CompositorContextOwner m_owner;
        cube::Material m_material;
    };
}  // namespace hyprimate::workspace
