#pragma once

#include <vector>

#include <GLES3/gl32.h>

#include "workspace/WorkspaceTransition.hpp"
#include "workspace/lava/LavaRenderer.hpp"

namespace lava_renderer_tests {
    using hyprimate::workspace::Direction;
    using hyprimate::workspace::lava::Material;
    using hyprimate::workspace::lava::Renderer;
    using hyprimate::workspace::lava::WindowPlanes;
    using Pixels = std::vector<unsigned char>;

    Material default_material(int variant);
    GLuint framebuffer_for(GLuint texture);
    Pixels read_output(GLuint fbo, int width, int height);
    bool draw(Renderer &renderer, GLuint source, GLuint destination, GLuint output, GLuint fbo,
        int width, int height, double progress, Direction direction, const Material &material,
        WindowPlanes planes = {});

    void check_binding_restore(Renderer &renderer, GLuint source, GLuint destination, GLuint output,
        GLuint fbo, int width, int height, const Material &material, WindowPlanes planes = {});
    void check_gl_contract(Renderer &renderer, GLuint source_texture, GLuint destination_texture,
        GLuint output, GLuint fbo, WindowPlanes burn_planes, const Material &base_material);
}
