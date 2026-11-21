#pragma once

#include <array>
#include <cstddef>

#include <GLES3/gl3.h>

namespace hyprimate::gl {
    // Initialization uploads provide tightly packed CPU data, not offsets into the
    // compositor's pixel-unpack buffer. Preserve its state even on a failed allocation.
    class ClientPixelUnpack {
      public:
        ClientPixelUnpack() {
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &m_buffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            for (std::size_t i = 0; i < FIELDS.size(); ++i) {
                glGetIntegerv(FIELDS[i], &m_saved[i]);
                glPixelStorei(FIELDS[i], FIELDS[i] == GL_UNPACK_ALIGNMENT ? 1 : 0);
            }
        }
        ~ClientPixelUnpack() {
            for (std::size_t i = 0; i < FIELDS.size(); ++i) { glPixelStorei(FIELDS[i], m_saved[i]); }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(m_buffer));
        }
        ClientPixelUnpack(const ClientPixelUnpack &) = delete;
        ClientPixelUnpack &operator=(const ClientPixelUnpack &) = delete;
        ClientPixelUnpack(ClientPixelUnpack &&) = delete;
        ClientPixelUnpack &operator=(ClientPixelUnpack &&) = delete;

      private:
        static constexpr std::array<GLenum, 6> FIELDS{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
            GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES};
        std::array<GLint, FIELDS.size()> m_saved{};
        GLint m_buffer = 0;
    };

    [[nodiscard]] inline bool framebuffer_attachment_is(GLenum attachment, GLuint name) {
        GLint attached = 0;
        glGetFramebufferAttachmentParameteriv(
            GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attached);
        return static_cast<GLuint>(attached) == name;
    }

    // Hyprland's createTexture returns a wrapper even when its first TexImage2D fails.
    // Only for fresh color-renderable 2D textures: an empty level cannot complete a fresh FBO.
    [[nodiscard]] inline bool has_color_storage(GLuint texture) {
        if (texture == 0) { return false; }
        GLuint probe = 0;
        glGenFramebuffers(1, &probe);
        if (probe == 0) { return false; }
        GLint previous = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probe);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
        glDeleteFramebuffers(1, &probe);
        return complete;
    }

    // A fresh, nonzero texture must be bound. GLES 3.0 sets IMMUTABLE_FORMAT only when
    // storage was actually created; querying the object leaves the caller's error flag alone.
    [[nodiscard]] inline bool allocate_texture_2d(GLenum format, GLsizei width, GLsizei height) {
        glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
        GLint immutable = GL_FALSE;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
        return immutable == GL_TRUE;
    }

    [[nodiscard]] inline bool allocate_texture_3d(GLenum format, GLsizei width, GLsizei height, GLsizei depth) {
        glTexStorage3D(GL_TEXTURE_3D, 1, format, width, height, depth);
        GLint immutable = GL_FALSE;
        glGetTexParameteriv(GL_TEXTURE_3D, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
        return immutable == GL_TRUE;
    }

    // Fresh storage or growth to different dimensions; framebuffer completeness alone
    // cannot tell an unchanged, smaller attachment from the requested one.
    [[nodiscard]] inline bool allocate_renderbuffer(GLenum format, GLsizei width, GLsizei height) {
        glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
        GLint allocated_width = 0;
        GLint allocated_height = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &allocated_width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &allocated_height);
        return width > 0 && height > 0 && allocated_width == width && allocated_height == height;
    }

    // Call only on a fresh buffer or for a change of size. A failed same-size reupload
    // would be indistinguishable from the old data store. Use SubData for existing capacity.
    [[nodiscard]] inline bool allocate_buffer(GLenum target, GLsizeiptr bytes, const void *data, GLenum usage) {
        glBufferData(target, bytes, data, usage);
        GLint64 allocated = -1;
        glGetBufferParameteri64v(target, GL_BUFFER_SIZE, &allocated);
        return bytes > 0 && allocated == bytes;
    }
} // namespace hyprimate::gl
