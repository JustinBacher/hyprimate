#include <array>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "gl/Storage.hpp"

int main() {
    gl_harness::CContext context{8, 8};
    if (!context.valid()) { return 1; }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (texture == 0 || glIsTexture(texture) != GL_TRUE) { return 1; }

    // A caller may already have separate read and draw targets. Neither is the
    // storage we're validating. An object name without a level must be rejected.
    std::array<GLuint, 2> fbos{};
    glGenFramebuffers(2, fbos.data());
    if (fbos[0] == 0 || fbos[1] == 0) { return 1; }
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
    glDeleteFramebuffers(2, fbos.data());
    glDeleteTextures(1, &texture);
    return g_failures == 0 ? 0 : 1;
}
