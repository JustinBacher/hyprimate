#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "gl/RawGlState.hpp"
#include "workspace/cube/CubeMaterial.hpp"

namespace cube_offscreen {
    using Pixel = std::array<std::uint8_t, 4>;
    static_assert(sizeof(Pixel) == 4);

    // The same clean raw-pass state as the compositor adapter, without a compositor shadow.
    struct NoCompositorShadow {
        void sync(const hyprimate::gl::SRawGlShadowState &) const {}
        void forget_framebuffer(GLuint) const {}
    };

    class Frame {
      public:
        Frame(int width, int height) : m_width(width), m_height(height) {
            glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
            glActiveTexture(GL_TEXTURE0);
            for (const auto texture : m_textures) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenFramebuffers(1, &m_framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
            // Deliberately no depth attachment, as on the caller's workspace surface.
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output(), 0);
            glDisable(GL_DITHER);
            const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            m_valid = status == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
        }

        ~Frame() {
            glDeleteFramebuffers(1, &m_framebuffer);
            glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        }

        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;

        [[nodiscard]] bool valid() const { return m_valid; }
        [[nodiscard]] GLuint capture(bool second) const { return m_textures[second ? 1 : 0]; }
        [[nodiscard]] GLuint output() const { return m_textures[2]; }

        void upload(bool second, std::span<const Pixel> pixels) const {
            glBindTexture(GL_TEXTURE_2D, capture(second));
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        void clear(const std::array<GLfloat, 4> &color) const {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffer);
            glClearBufferfv(GL_COLOR, 0, color.data());
        }

        void read(std::span<Pixel> pixels) const {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
            glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }

        bool draw(hyprimate::workspace::cube::Material &material, GLuint source, GLuint destination,
            GLuint output_texture, int width, int height, double progress, hyprimate::workspace::Direction direction,
            const hyprimate::workspace::cube::Settings &settings) const {
            NoCompositorShadow shadow;
            const hyprimate::gl::CRawGlState<2, NoCompositorShadow> guard(
                {.viewport_width = m_width, .viewport_height = m_height, .framebuffer = m_framebuffer}, shadow);
            return material.draw(source, destination, output_texture, width, height, progress, direction, settings);
        }

        bool draw(hyprimate::workspace::cube::Material &material, double progress,
            hyprimate::workspace::Direction direction, const hyprimate::workspace::cube::Settings &settings) const {
            const bool backward = direction == hyprimate::workspace::Direction::Backward;
            return draw(material, capture(backward), capture(!backward), output(), m_width, m_height, progress, direction,
                settings);
        }

      private:
        int m_width;
        int m_height;
        std::array<GLuint, 3> m_textures{};
        GLuint m_framebuffer = 0;
        bool m_valid = false;
    };
}  // namespace cube_offscreen
