#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "animations/construction/ConstructionMaterial.hpp"
#include "animations/construction/ConstructionProgram.hpp"
#include "animations/construction/ConstructionShaders.hpp"
#include "gl/RawGlState.hpp"
#include "gl/Storage.hpp"

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

// Take 13 installation-small: (96,96) 480x320 logical, scale 1.25, normal
// 1280x720 output. Derive the seated geometry from the pinned host's 2-logical-pixel
// bounding-box outsets and 1-device-pixel canvas padding (floor/ceil). The archived
// 705x468 number is the event's peak work allocation, not its seated canvas.
constexpr double IDENTITY_SCALE = 1.25;
constexpr Box IDENTITY_BODY{96, 96, 480, 320};
constexpr Box IDENTITY_CAPTURE{94, 94, 484, 324};
constexpr Box IDENTITY_SOURCE{117.5, 117.5, 605, 405};
struct PixelRect { int x, y, w, h; };
constexpr PixelRect BODY_PIXELS{120, 120, 600, 400};
constexpr PixelRect SEATED_CANVAS{116, 116, 608, 408};
constexpr PixelRect OUTPUT_VIEWPORT{0, 0, 1280, 720};

Pixel identity_pixel(int x, int y) {
    if (x < BODY_PIXELS.w / 3) {
        return (x + y) % 2 == 0 ? Pixel{245, 4, 200, 255} : Pixel{6, 234, 29, 255};
    }
    const auto r = static_cast<GLubyte>(x * 255 / (BODY_PIXELS.w - 1));
    const auto g = static_cast<GLubyte>(y * 255 / (BODY_PIXELS.h - 1));
    const auto b = static_cast<GLubyte>((3 * x + 5 * y) % 256);
    if (x < 2 * BODY_PIXELS.w / 3) return {r, g, b, 255};
    const unsigned alpha = (13 * x + 29 * y) % 256;
    const auto premultiply = [alpha](unsigned c) {
        return static_cast<GLubyte>((c * alpha + 127) / 255);
    };
    return {premultiply(r), premultiply(g), premultiply(b), static_cast<GLubyte>(alpha)};
}

// CRawGlState covers the draw bindings, not the CPU readback pack state or dither.
struct IdentityReadbackState {
    static constexpr std::array<GLenum, 4> PACK{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS};
    std::array<GLint, PACK.size()> prior{};
    GLint buffer = 0;
    const bool dither = glIsEnabled(GL_DITHER) == GL_TRUE;

    IdentityReadbackState() {
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        for (std::size_t i = 0; i < PACK.size(); ++i) {
            glGetIntegerv(PACK[i], &prior[i]);
            glPixelStorei(PACK[i], PACK[i] == GL_PACK_ALIGNMENT ? 1 : 0);
        }
        glDisable(GL_DITHER);
    }
    ~IdentityReadbackState() {
        if (dither) glEnable(GL_DITHER);
        for (std::size_t i = 0; i < PACK.size(); ++i) glPixelStorei(PACK[i], prior[i]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(buffer));
    }
};

struct IdentityObjects {
    GLuint program = 0, vao = 0, sampler = 0;
    std::array<GLuint, 4> textures{};
    std::array<GLuint, 2> framebuffers{};
    ~IdentityObjects() {
        glDeleteFramebuffers(2, framebuffers.data());
        glDeleteTextures(4, textures.data());
        glDeleteSamplers(1, &sampler);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
    }
};

struct PixelDifferences {
    unsigned compared = 0, mismatched = 0;
    std::array<unsigned, 4> channel_counts{}, channel_max{};

    void add(const Pixel &actual, const Pixel &expected) {
        ++compared;
        mismatched += actual != expected;
        for (std::size_t c = 0; c < channel_max.size(); ++c) {
            const auto delta = static_cast<unsigned>(std::abs(int(actual[c]) - int(expected[c])));
            channel_counts[c] += delta != 0;
            channel_max[c] = std::max(channel_max[c], delta);
        }
    }
    void print(const char *path, const char *reference, const char *region) const {
        std::printf("identity %s vs %s %s: pixels=%u/%u max=%u "
            "rgba_changed=(%u,%u,%u,%u) rgba_max=(%u,%u,%u,%u)\n",
            path, reference, region, mismatched, compared,
            *std::max_element(channel_max.begin(), channel_max.end()),
            channel_counts[0], channel_counts[1], channel_counts[2], channel_counts[3],
            channel_max[0], channel_max[1], channel_max[2], channel_max[3]);
    }
};

template<typename Reference>
void compare_identity(const char *path, const char *reference, const std::vector<Pixel> &actual,
    PixelRect box, bool top_first, Reference expected) {
    PixelDifferences all, interior;
    for (int row = 0; row < box.h; ++row) {
        const int y = box.y + (top_first ? row : box.h - 1 - row);
        for (int col = 0; col < box.w; ++col) {
            const int x = box.x + col;
            const auto &pixel = actual[row * box.w + col];
            const auto want = expected(x, y);
            all.add(pixel, want);
            // Same two-device-pixel interior inset as the take-13 recorder comparison.
            if (x >= BODY_PIXELS.x + 2 && x < BODY_PIXELS.x + BODY_PIXELS.w - 2 &&
                y >= BODY_PIXELS.y + 2 && y < BODY_PIXELS.y + BODY_PIXELS.h - 2)
                interior.add(pixel, want);
        }
    }
    interior.print(path, reference, "interior");
    all.print(path, reference, "full-readback");
    CHECK(interior.compared == 236016);
    CHECK(all.mismatched == 0);
}

void check_identity_transfer(CConstructionMaterial &material) {
    constexpr auto &C = SEATED_CANVAS;
    constexpr auto &B = BODY_PIXELS;
    constexpr auto &O = OUTPUT_VIEWPORT;
    const SConstructionTarget placement{.canvas = {C.x, C.y, C.w, C.h},
        .source = IDENTITY_SOURCE, .body = IDENTITY_BODY, .capture = IDENTITY_CAPTURE,
        .image_origin = {C.x, C.y}, .image_size = {C.w, C.h}, .scale = IDENTITY_SCALE};
    std::printf("identity fixture=take-13/installation-small derived-seated scale=%.2f "
        "body_logical=(%.0f,%.0f %.0fx%.0f) capture_logical=(%.0f,%.0f %.0fx%.0f) "
        "source=(%.1f,%.1f %.0fx%.0f) image_and_canvas=(%d,%d %dx%d) "
        "body_dst=(%d,%d %dx%d) work_viewport=(0,0 %dx%d) output_viewport=(%d,%d %dx%d) "
        "format=linear-RGBA8 dither=off blend=premultiplied-over-transparent\n",
        placement.scale, placement.body.x, placement.body.y, placement.body.w, placement.body.h,
        placement.capture.x, placement.capture.y, placement.capture.w, placement.capture.h,
        placement.source.x, placement.source.y, placement.source.w, placement.source.h,
        C.x, C.y, C.w, C.h, B.x, B.y, B.w, B.h, C.w, C.h, O.x, O.y, O.w, O.h);
    const auto view = CConstructionMaterial::prepare_view(placement);
    if (!view || !material.supports_view(*view)) {
        CHECK(false && "invalid identity view");
        return;
    }

    // The native-sized synthetic consumer uses real normalized texture sampling,
    // not a blit or texelFetch. Both inputs land on the same device-pixel centers.
    constexpr auto *VERTEX = R"GLSL(#version 300 es
precision highp float;
uniform vec4 u_rect;
uniform vec2 u_viewport;
out vec2 uv;
const vec2 CORNERS[4] = vec2[4](vec2(0,0), vec2(1,0), vec2(0,1), vec2(1,1));
void main() {
    uv = CORNERS[gl_VertexID];
    vec2 ndc = (u_rect.xy + uv * u_rect.zw) / u_viewport * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0, 1);
}
)GLSL";
    constexpr auto *FRAGMENT = R"GLSL(#version 300 es
precision highp float;
precision highp sampler2D;
uniform sampler2D u_image;
in vec2 uv;
out vec4 color;
void main() { color = texture(u_image, uv); }
)GLSL";
    IdentityObjects gpu;
    gpu.program = gl_harness::build_program(VERTEX, FRAGMENT);
    if (!gpu.program) {
        CHECK(gpu.program != 0);
        return;
    }
    glGenTextures(4, gpu.textures.data());
    glGenFramebuffers(2, gpu.framebuffers.data());
    glGenVertexArrays(1, &gpu.vao);
    glGenSamplers(1, &gpu.sampler);
    const auto [source_texture, work_texture, direct_texture, output_texture] = gpu.textures;
    const auto [work_fbo, output_fbo] = gpu.framebuffers;

    const auto authorized = [&placement](int x, int y) {
        return x + .5 >= placement.source.x && x + .5 < placement.source.x + placement.source.w &&
            y + .5 >= placement.source.y && y + .5 < placement.source.y + placement.source.h;
    };
    std::vector<Pixel> source(C.w * C.h), direct_input(B.w * B.h);
    for (int y = 0; y < C.h; ++y) {
        for (int x = 0; x < C.w; ++x) {
            if (!authorized(C.x + x, C.y + y)) source[y * C.w + x] = {255, 0, 255, 255};
        }
    }
    for (int y = 0; y < B.h; ++y) {
        for (int x = 0; x < B.w; ++x) {
            const auto pixel = identity_pixel(x, y);
            direct_input[y * B.w + x] = pixel;
            source[(B.y - C.y + y) * C.w + (B.x - C.x + x)] = pixel;
        }
    }
    const auto source_at = [&](int x, int y) {
        return authorized(x, y) ? source[(y - C.y) * C.w + x - C.x] : Pixel{};
    };

    Shadow shadow;
    gl::CRawGlState<2, Shadow> state({.viewport_width = C.w, .viewport_height = C.h,
        .framebuffer = work_fbo}, shadow);
    const gl::ClientPixelUnpack unpack;
    const IdentityReadbackState readback_state;
    const auto allocate = [](GLuint texture, PixelRect size, const Pixel *pixels = nullptr) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, size.w, size.h);
        if (pixels) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.w, size.h,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    };
    allocate(source_texture, C, source.data());
    allocate(work_texture, C);
    allocate(direct_texture, B, direct_input.data());
    allocate(output_texture, O);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, work_texture, 0);

    // Draw the production material's single live, seated client quad. Read the
    // work texture directly before it has passed through a presentation sampler.
    ConstructionBatch batch;
    batch.add(front(0, IDENTITY_CAPTURE.w, IDENTITY_CAPTURE.h));
    if (!material.draw(batch, *view, source_texture, work_texture)) {
        CHECK(false && "failed active identity work draw");
        return;
    }
    std::vector<Pixel> work(C.w * C.h);
    glReadPixels(0, 0, C.w, C.h, GL_RGBA, GL_UNSIGNED_BYTE, work.data());
    compare_identity("work-layer", "authorized-source", work, C, true, source_at);

    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        CHECK(false && "incomplete identity presentation FBO");
        return;
    }
    glViewport(O.x, O.y, O.w, O.h);
    glUseProgram(gpu.program);
    glUniform1i(glGetUniformLocation(gpu.program, "u_image"), 0);
    glUniform2f(glGetUniformLocation(gpu.program, "u_viewport"), O.w, O.h);
    const GLint rect = glGetUniformLocation(gpu.program, "u_rect");
    glBindVertexArray(gpu.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, gpu.sampler);
    glSamplerParameteri(gpu.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(gpu.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const auto present = [&](GLuint texture, PixelRect dst, std::vector<Pixel> &pixels) {
        constexpr std::array<GLfloat, 4> CLEAR{};
        glClearBufferfv(GL_COLOR, 0, CLEAR.data());
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform4f(rect, dst.x, dst.y, dst.w, dst.h);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, O.w, O.h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };
    std::vector<Pixel> direct(O.w * O.h), presented(O.w * O.h);
    const auto work_at = [&work](int x, int y) {
        return x >= C.x && x < C.x + C.w && y >= C.y && y < C.y + C.h
            ? work[(y - C.y) * C.w + x - C.x] : Pixel{};
    };
    const auto direct_at = [&direct](int x, int y) { return direct[(O.h - 1 - y) * O.w + x]; };
    for (const auto filter : {GL_NEAREST, GL_LINEAR}) {
        glSamplerParameteri(gpu.sampler, GL_TEXTURE_MIN_FILTER, filter);
        glSamplerParameteri(gpu.sampler, GL_TEXTURE_MAG_FILTER, filter);
        present(direct_texture, B, direct);
        present(work_texture, C, presented);
        const bool nearest = filter == GL_NEAREST;
        const auto *baseline = nearest ? "direct/GL_NEAREST" : "direct/GL_LINEAR";
        const auto *transfer = nearest ? "presented/GL_NEAREST" : "presented/GL_LINEAR";
        compare_identity(baseline, "authorized-source", direct, O, false, source_at);
        compare_identity(transfer, "authorized-source", presented, O, false, source_at);
        compare_identity(transfer, baseline, presented, O, false, direct_at);
        compare_identity(transfer, "work-layer", presented, O, false, work_at);
    }
    CHECK(glGetError() == GL_NO_ERROR);
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
    check_identity_transfer(material);
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
