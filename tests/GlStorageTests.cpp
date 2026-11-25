#include <array>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "gl/Storage.hpp"

int main() {
    gl_harness::CContext context{8, 8};
    if (!context.valid()) {
        return 1;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (texture == 0 || glIsTexture(texture) != GL_TRUE) {
        return 1;
    }

    // A caller may already have separate read and draw targets. Neither is the
    // storage we're validating. An object name without a level must be rejected.
    std::array<GLuint, 2> fbos{};
    glGenFramebuffers(2, fbos.data());
    if (fbos[0] == 0 || fbos[1] == 0) {
        return 1;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
    CHECK(!hyprimate::gl::has_color_storage(texture));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    CHECK(hyprimate::gl::has_color_storage(texture));
    GLint read = 0;
    GLint draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    CHECK(static_cast<GLuint>(read) == fbos[0]);
    CHECK(static_cast<GLuint>(draw) == fbos[1]);
    CHECK(glGetError() == GL_NO_ERROR);
    // Texture and renderbuffer namespaces may expose the same integer name. The
    // attachment validator must distinguish object type, not just the name.
    std::array<GLuint, 32> texture_names{};
    std::array<GLuint, 32> renderbuffer_names{};
    glGenTextures(static_cast<GLsizei>(texture_names.size()), texture_names.data());
    glGenRenderbuffers(static_cast<GLsizei>(renderbuffer_names.size()), renderbuffer_names.data());
    GLuint collision = 0;
    for (const GLuint TEXTURE_NAME : texture_names) {
        for (const GLuint RENDERBUFFER_NAME : renderbuffer_names) {
            if (TEXTURE_NAME == RENDERBUFFER_NAME) {
                collision = TEXTURE_NAME;
            }
        }
    }
    CHECK(collision != 0);
    if (collision != 0) {
        GLuint routed = 0;
        glGenFramebuffers(1, &routed);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, routed);
        glBindTexture(GL_TEXTURE_2D, collision);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
        glBindRenderbuffer(GL_RENDERBUFFER, collision);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 8, 8);
        glFramebufferRenderbuffer(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, collision);
        CHECK(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        CHECK(!hyprimate::gl::framebuffer_attachment_is(
            GL_COLOR_ATTACHMENT0, collision, GL_TEXTURE));

        glFramebufferTexture2D(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, collision, 0);
        CHECK(hyprimate::gl::framebuffer_attachment_is(
            GL_COLOR_ATTACHMENT0, collision, GL_TEXTURE));
        const GLenum NONE = GL_NONE;
        glDrawBuffers(1, &NONE);
        CHECK(hyprimate::gl::framebuffer_attachment_is(
            GL_COLOR_ATTACHMENT0, collision, GL_TEXTURE));
        CHECK(!hyprimate::gl::framebuffer_has_single_draw_buffer(GL_COLOR_ATTACHMENT0));
        glDeleteFramebuffers(1, &routed);
        glDeleteRenderbuffers(1, &collision);
        glDeleteTextures(1, &collision);
    }
    glDeleteRenderbuffers(static_cast<GLsizei>(renderbuffer_names.size()),
        renderbuffer_names.data());
    glDeleteTextures(static_cast<GLsizei>(texture_names.size()), texture_names.data());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
    CHECK(glGetError() == GL_NO_ERROR);
    // Exercise both the host's TexImage upload and the ground field's TexSubImage
    // under compositor-owned unpack state, including a bound pixel buffer.
    constexpr std::array<GLenum, 6> FIELDS{GL_UNPACK_ALIGNMENT,
        GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT,
        GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_IMAGES};
    constexpr std::array<GLint, 6> HOSTILE{8, 7, 9, 1, 2, 1};
    constexpr std::array<GLubyte, 16> PIXELS{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
    constexpr std::array<GLubyte, 16> UPDATED{
        12, 34, 56, 255, 78, 90, 123, 255, 21, 43, 65, 255, 87, 109, 135, 255};
    GLuint pbo = 0;
    GLuint uploaded = 0;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512, nullptr, GL_STATIC_DRAW);
    for (std::size_t i = 0; i < FIELDS.size(); ++i) {
        glPixelStorei(FIELDS[i], HOSTILE[i]);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGenTextures(1, &uploaded);
    for (int pass = 0; pass < 2; ++pass) {
        {
            const hyprimate::gl::TextureUploadBinding binding;
            const hyprimate::gl::ClientPixelUnpack unpack;
            glBindTexture(GL_TEXTURE_2D, uploaded);
            if (pass == 0) {
                // The host sets row length from stride and unbinds after upload.
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 2);
                glTexImage2D(
                    GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, PIXELS.data());
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                glBindTexture(GL_TEXTURE_2D, 0);
            } else {
                glTexSubImage2D(
                    GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, UPDATED.data());
            }
        }
        GLint state = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state);
        CHECK(state == GL_TEXTURE3);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state);
        CHECK(static_cast<GLuint>(state) == texture);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state);
        CHECK(static_cast<GLuint>(state) == pbo);
        for (std::size_t i = 0; i < FIELDS.size(); ++i) {
            glGetIntegerv(FIELDS[i], &state);
            CHECK(state == HOSTILE[i]);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
        glFramebufferTexture2D(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, uploaded, 0);
        CHECK(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        std::array<GLubyte, 16> actual{};
        glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        CHECK(actual == (pass == 0 ? PIXELS : UPDATED));
        CHECK(glGetError() == GL_NO_ERROR);
    }
    // A hostile sampler overrides a texture's clamp mode. Native mesh draws must
    // sample using the texture parameters while preserving both source and ICC samplers.
    constexpr auto VERTEX = R"(#version 300 es
        void main() {
            vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        })";
    constexpr auto FRAGMENT = R"(#version 300 es
        precision highp float;
        uniform sampler2D image;
        out vec4 color;
        void main() { color = texture(image, vec2(1.25, 0.5)); })";
    const GLuint program = gl_harness::build_program(VERTEX, FRAGMENT);
    CHECK(program != 0);
    GLuint source = 0, target = 0, vao = 0;
    std::array<GLuint, 2> samplers{};
    glGenTextures(1, &source);
    glGenTextures(1, &target);
    glGenVertexArrays(1, &vao);
    glGenSamplers(2, samplers.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    {
        const hyprimate::gl::ClientPixelUnpack unpack;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, PIXELS.data());
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, target);
    {
        const hyprimate::gl::ClientPixelUnpack unpack;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glSamplerParameteri(samplers[0], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(samplers[0], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(samplers[0], GL_TEXTURE_WRAP_S, GL_REPEAT);
    glBindSampler(0, samplers[0]);
    glBindSampler(8, samplers[1]);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glBindVertexArray(vao);
    glViewport(0, 0, 1, 1);
    std::array<GLubyte, 4> sampled{};
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled.data());
    CHECK((sampled == std::array<GLubyte, 4>{255, 0, 0, 255}));
    {
        const hyprimate::gl::NativeMeshSamplers isolated;
        GLint active = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        CHECK(active == GL_TEXTURE3);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled.data());
        CHECK((sampled == std::array<GLubyte, 4>{0, 255, 0, 255}));
    }
    for (std::size_t i = 0; i < samplers.size(); ++i) {
        glActiveTexture(i == 0 ? GL_TEXTURE0 : GL_TEXTURE8);
        GLint sampler = 0;
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        CHECK(static_cast<GLuint>(sampler) == samplers[i]);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    glDeleteSamplers(2, samplers.data());
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glDeleteTextures(1, &source);
    glDeleteTextures(1, &target);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glDeleteBuffers(1, &pbo);
    glDeleteTextures(1, &uploaded);
    glDeleteFramebuffers(2, fbos.data());
    glDeleteTextures(1, &texture);
    return g_failures == 0 ? 0 : 1;
}
