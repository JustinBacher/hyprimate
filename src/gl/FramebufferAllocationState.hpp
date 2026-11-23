#pragma once

#include "gl/Storage.hpp"

namespace hyprimate::gl {
    // Native framebuffer allocation uploads through the current texture unit and
    // may disable depth writes when allocating a stencil attachment. Do not reset
    // framebuffer/viewport state here: beginRender intentionally establishes those.
    class FramebufferAllocationState {
      public:
        FramebufferAllocationState() : m_depth(glIsEnabled(GL_DEPTH_TEST)) {
            glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depth_mask);
            // TexImage2D(nullptr) allocates storage, not client pixel data. Row
            // length/alignment/skips are immaterial, but a PBO makes nullptr offset 0.
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &m_unpack_buffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        }
        ~FramebufferAllocationState() {
            if (m_depth != GL_FALSE) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(m_depth_mask);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(m_unpack_buffer));
        }
        FramebufferAllocationState(const FramebufferAllocationState &) = delete;
        FramebufferAllocationState &operator=(const FramebufferAllocationState &) = delete;
        FramebufferAllocationState(FramebufferAllocationState &&) = delete;
        FramebufferAllocationState &operator=(FramebufferAllocationState &&) = delete;

      private:
        TextureUploadBinding m_texture;
        GLint m_unpack_buffer = 0;
        GLboolean m_depth = GL_FALSE;
        GLboolean m_depth_mask = GL_TRUE;
    };
}  // namespace hyprimate::gl
