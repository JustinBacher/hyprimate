#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "animations/construction/ConstructionMaterial.hpp"
#include "animations/construction/ConstructionProgram.hpp"
#include "animations/construction/ConstructionShaders.hpp"
#include "gl/RawGlState.hpp"

using namespace hyprimate;
namespace {
using Pixel = std::array<GLubyte, 4>;
constexpr int WIDTH = 32, HEIGHT = 24;
using Pixels = std::array<Pixel, WIDTH * HEIGHT>;

struct Shadow {
    void forget_framebuffer(GLuint) const {}
    void sync(const gl::SRawGlShadowState &) const {}
};

ConstructionQuad front(float x, float width, float height) {
    return {.origin = {x,0,0}, .u = {width,0,0}, .v = {0,height,0},
        .uv = {x,0,width,height}, .tint = {255,255,255,255}, .surface = ConstructionSurface::Client};
}

void check_pixels(CConstructionMaterial &material, GLuint image, GLuint target, GLuint framebuffer,
    double scale, bool top_first) {
    // The borrowed image starts two pixels to the right and one above the authorized rectangle.
    // Pixels outside it must remain transparent rather than extruding its nearest texel.
    const double origin_shift = scale == 1 ? 0 : 0.2;
    const SConstructionTarget placement{.canvas = {10, 13, WIDTH, HEIGHT},
        .source = {10 + origin_shift, 13 + origin_shift, WIDTH, HEIGHT},
        .body = {220.2, 95.4, WIDTH / scale, HEIGHT / scale},
        .capture = {220.2, 95.4, WIDTH / scale, HEIGHT / scale},
        .image_origin = {12,12}, .image_size = {WIDTH,HEIGHT}, .scale = scale,
        .output_top_first = top_first};
    const auto view = CConstructionMaterial::prepare_view(placement);
    CHECK(view && material.supports_view(*view));
    if (!view) return;
    ConstructionBatch batch;
    // A fractional seam tests sampling continuity; it is not a texture filtering boundary.
    const auto seam = static_cast<float>(12.25 / scale);
    const auto width = static_cast<float>(WIDTH / scale);
    const auto height = static_cast<float>(HEIGHT / scale);
    batch.add(front(0, seam, height));
    batch.add(front(seam, width - seam, height));
    Pixels source{}, actual{};
    Shadow shadow;
    constexpr Pixel clear{};
    for (int generation : {0,1}) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const bool high = (x + y) % 2 != 0;
                const GLubyte alpha = generation == 0 ? 255 : 128;
                source[y * WIDTH + x] = {static_cast<GLubyte>(high ? alpha : 0),
                    static_cast<GLubyte>(high ? 0 : alpha), 17, alpha};
            }
        }
        glBindTexture(GL_TEXTURE_2D, image);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, source.data());
        {
            gl::CRawGlState<2, Shadow> state({.viewport_width = WIDTH, .viewport_height = HEIGHT,
                .framebuffer = framebuffer}, shadow);
            CHECK(material.draw(batch, *view, image, target));
            glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
        }
        Pixels expected{};
        for (int y = 0; y < HEIGHT; ++y) {
            const int row = top_first ? y : HEIGHT - y - 1;
            for (int x = 0; x < WIDTH; ++x) {
                expected[y*WIDTH+x] = (x < 2 || row+1 >= HEIGHT) ? clear : source[(row+1)*WIDTH+x-2];
            }
        }
        CHECK(actual == expected);
        if (actual != expected) {
            const auto mismatch = std::mismatch(actual.begin(),actual.end(),expected.begin());
            const auto i = mismatch.first - actual.begin();
            std::fprintf(stderr,"first pixel %td,%td got %u/%u/%u/%u expected %u/%u/%u/%u\n", i%WIDTH,i/WIDTH,
                actual[i][0],actual[i][1],actual[i][2],actual[i][3],expected[i][0],expected[i][1],expected[i][2],expected[i][3]);
        }
    }
    auto bad = placement;
    bad.source.w += 2;
    CHECK(!CConstructionMaterial::prepare_view(bad));
}

GLuint check_program_retry() {
    using namespace construction_shader;
    constexpr const char *INVALID = "#version 300 es\nthis is not GLSL\n";
    // Both shaders compile on their own, but Construction exports v_uv as vec2.
    constexpr const char *MISMATCHED_FRAGMENT = R"GLSL(#version 300 es
precision highp float;
in vec3 v_uv;
out vec4 color;
void main() { color = vec4(v_uv, 1.0); }
)GLSL";
    CHECK(build_program(INVALID, FRAGMENT.data()) == 0);
    CHECK(build_program(VERTEX.data(), INVALID) == 0);
    CHECK(build_program(VERTEX.data(), MISMATCHED_FRAGMENT) == 0);
    CHECK(glGetError() == GL_NO_ERROR);

    const GLuint PROGRAM = build_program(VERTEX.data(), FRAGMENT.data());
    if (PROGRAM != 0) {
        GLint attached = -1;
        glGetProgramiv(PROGRAM, GL_ATTACHED_SHADERS, &attached);
        CHECK(attached == 0);
    }
    return PROGRAM;
}
}  // namespace

int main(int argc, char **argv) {
    gl_harness::CContext context(WIDTH,HEIGHT);
    if (!context.valid()) return 77;
    GLint minor = 0;
    glGetIntegerv(GL_MINOR_VERSION,&minor);
    if (argc == 2 && std::string_view(argv[1]) == "--require-es30" && minor != 0) return 77;
    std::printf("renderer=%s version=%s\n", glGetString(GL_RENDERER),glGetString(GL_VERSION));
    const GLuint program = check_program_retry();
    if (!program) return 1;
    std::array<GLuint, 2> textures{};
    glGenTextures(2,textures.data());
    for (const auto name : textures) {
        glBindTexture(GL_TEXTURE_2D,name);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,WIDTH,HEIGHT);
    }
    GLuint framebuffer = 0;
    glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    CConstructionMaterial material;
    Shadow shadow;
    {
        gl::CRawGlState<2, Shadow> state({},shadow);
        { gl_harness::FailAllocation fault(gl_harness::Allocation::texture_2d); CHECK(!material.initialize(program)); }
        { gl_harness::FailAllocation fault(gl_harness::Allocation::buffer); CHECK(!material.initialize(program)); }
        CHECK(material.initialize(program));
    }
    check_pixels(material,textures[0],textures[1],framebuffer, 1.0, true);
    check_pixels(material,textures[0],textures[1],framebuffer, 1.25, true);
    check_pixels(material,textures[0],textures[1],framebuffer, 1.25, false);
    { gl::CRawGlState<2, Shadow> state({},shadow); material.release(); }
    GLint fbo = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    CHECK(fbo == 0);
    glDeleteFramebuffers(1,&framebuffer);
    glDeleteTextures(2,textures.data());
    glDeleteProgram(program);
    CHECK(glGetError() == GL_NO_ERROR);
    return g_failures == 0 ? 0 : 1;
}
