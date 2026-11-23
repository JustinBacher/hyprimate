#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "window/construction/ConstructionMaterial.hpp"
#include "window/construction/ConstructionProgram.hpp"
#include "window/construction/ConstructionShaders.hpp"
#include "window/construction/studies/RegionalClose.hpp"
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

void check_blur_mask(CConstructionMaterial &material, GLuint image, GLuint target, GLuint framebuffer) {
    // The image crops the first four columns of the client. Pose the client with a
    // subpixel translation so both its clipped edge and the filtered matte edge move.
    SConstructionTarget placement{.canvas = {0, 0, WIDTH, HEIGHT}, .source = {-4, 0, 24, 16},
        .body = {0, 0, 24, 16}, .capture = {0, 0, 24, 16},
        .image_origin = {0, 0}, .image_size = {WIDTH, HEIGHT}};
    const auto normal_view = CConstructionMaterial::prepare_view(placement);
    placement.mask_mode = ConstructionMaskMode::Image;
    const auto mask_view = CConstructionMaterial::prepare_view(placement);
    CHECK(normal_view && mask_view);
    if (!normal_view || !mask_view) return;

    constexpr Pixel BLACK{0, 0, 0, 255}, WHITE{255, 255, 255, 255}, RED{255, 0, 0, 255};
    constexpr Pixel SHADE{0, 0, 0, 128};
    Pixels source;
    source.fill(BLACK);
    for (int y = 2; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            source[y * WIDTH + x] = WHITE;
    glBindTexture(GL_TEXTURE_2D, image);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, source.data());

    ConstructionBatch batch;
    auto client = front(0, 24, 16);
    client.origin = {8.25, 4, 0};
    batch.add(client);
    // Artwork painted after the client overlaps both covered and uncovered matte.
    batch.add({.origin = {22, 7, 0}, .u = {8, 0, 0}, .v = {0, 4, 0}, .uv = {0, 0, 1, 1},
        .tint = RED, .surface = ConstructionSurface::Solid});
    batch.add({.origin = {5, 15, 0}, .u = {6, 0, 0}, .v = {0, 6, 0}, .uv = {0, 0, 1, 1},
        .tint = SHADE, .surface = ConstructionSurface::Shadow});

    Shadow shadow;
    const auto readback = [&](const CConstructionMaterial::View &view, Pixels &pixels) {
        gl::CRawGlState<2, Shadow> state({.viewport_width = WIDTH, .viewport_height = HEIGHT,
            .framebuffer = framebuffer}, shadow);
        CHECK(material.draw(batch, view, image, target));
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };

    Pixels normal_before{}, mask{}, normal_after{};
    readback(*normal_view, normal_before);
    readback(*mask_view, mask);
    readback(*normal_view, normal_after);

    Pixels expected{};
    for (int y = 6; y < 16; ++y) {
        for (int x = 8; x < 20; ++x)
            expected[y * WIDTH + x] = WHITE;
        // At column 20, 1/4 of the linear sample is white. Allow UNORM rounding.
        const GLubyte coverage = mask[y * WIDTH + 20][0];
        CHECK(coverage >= 63 && coverage <= 65);
        expected[y * WIDTH + 20] = {coverage, coverage, coverage, coverage};
    }
    CHECK(mask == expected);
    CHECK(normal_before[8 * WIDTH + 20] == RED);
    CHECK(normal_before[12 * WIDTH + 21] == BLACK);
    CHECK(normal_before[18 * WIDTH + 4] == SHADE);
    CHECK(normal_after == normal_before);
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

void check_retained_regional_close(CConstructionMaterial &material, GLuint image, GLuint target,
    GLuint framebuffer) {
    using Close = construction::studies::RegionalClose;
    Shadow shadow;
    const SConstructionTarget placement{.canvas = {0, 1, WIDTH, HEIGHT},
        .source = {-3.25, 2.25, 18, 14},
        .body = {199, 80, 12, 9},
        .capture = {198, 79, 14.4, 11.2},
        .image_origin = {}, .image_size = {WIDTH, HEIGHT}, .scale = 1.25,
        .allow_empty_crop = true};
    const auto view = CConstructionMaterial::prepare_view(placement);
    CHECK(view && view->authorized_local_crop);
    if (!view || !view->authorized_local_crop) return;

    // Like the monitor snapshot's RPT_EXPORT target, logical top lives at low GL rows.
    // Rasterize a colored print into a whole-output texture, with distinct off-crop
    // poison pixels to detect leaks and an upper-left marker to detect either axis flip.
    constexpr Pixel POISON{255, 0, 255, 255};
    constexpr Pixel UPPER{48, 12, 3, 128}, LOWER{5, 19, 85, 128}, MARKER{0, 96, 7, 128};
    constexpr Pixel EFFECT{242, 220, 32, 255}, CLEAR{};
    GLuint snapshot_fbo = 0, ambient_vao = 0, ambient_ebo = 0;
    glGenFramebuffers(1, &snapshot_fbo);
    glGenVertexArrays(1, &ambient_vao);
    glGenBuffers(1, &ambient_ebo);
    {
        gl::CRawGlState<2, Shadow> state(
            {.viewport_width = WIDTH, .viewport_height = HEIGHT, .framebuffer = snapshot_fbo}, shadow);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
        CHECK(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        const auto paint = [](Pixel pixel) {
            const std::array<float, 4> rgba{pixel[0] / 255.f, pixel[1] / 255.f,
                pixel[2] / 255.f, pixel[3] / 255.f};
            glClearBufferfv(GL_COLOR, 0, rgba.data());
        };
        paint(POISON);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 2, 15, 14);
        paint(LOWER);
        glScissor(0, 2, 15, 5);
        paint(UPPER);
        glScissor(0, 2, 4, 3);
        paint(MARKER);
    }
    glDeleteFramebuffers(1, &snapshot_fbo);

    const Box local{0, 0, placement.capture.w, placement.capture.h};
    const std::array visible{Close::VisibleFace::flat(local)};
    Close model;
    CHECK(model.start(local, visible, 42, 1));
    const auto frame = std::make_unique_for_overwrite<Close::Frame>();
    model.emit({*view->authorized_local_crop, true}, *frame);
    CHECK(!frame->source_faces().empty());
    ConstructionBatch effects;
    effects.add({.origin = {8, 4, 0}, .u = {2, 0, 0}, .v = {0, 2, 0},
        .uv = {}, .tint = EFFECT, .surface = ConstructionSurface::Solid});

    glBindVertexArray(ambient_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ambient_ebo);
    glEnable(GL_DEPTH_TEST);
    const auto draw = [&](const ConstructionBatch &overlays, unsigned fail_allocation = 0) {
        Pixels pixels;
        {
            gl::CRawGlState<2, Shadow> state(
                {.viewport_width = WIDTH, .viewport_height = HEIGHT, .framebuffer = framebuffer}, shadow);
            if (fail_allocation) {
                gl_harness::FailAllocation fault(gl_harness::Allocation::buffer, fail_allocation);
                CHECK(!material.draw_close(*frame, overlays, *view, image, target));
            } else {
                CHECK(material.draw_close(*frame, overlays, *view, image, target));
            }
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        GLint index = -1;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &index);
        CHECK(index == static_cast<GLint>(ambient_ebo) && glIsEnabled(GL_DEPTH_TEST));
        return pixels;
    };
    (void)draw(effects, 1);
    (void)draw(effects, 2);
    const auto initial = draw(effects);
    for (int row = 0; row < HEIGHT; ++row) {
        for (int x = 0; x < WIDTH; ++x) {
            const int snapshot_y = row + 1;
            Pixel expected = CLEAR;
            if (x < 15 && snapshot_y >= 2 && snapshot_y < 16) {
                expected = snapshot_y < 7 ? UPPER : LOWER;
                if (x < 4 && snapshot_y < 5) expected = MARKER;
                if (x >= 7 && x < 9 && snapshot_y >= 7 && snapshot_y < 10) expected = EFFECT;
            }
            CHECK(initial[row * WIDTH + x] == expected);
        }
    }

    {
        gl::CRawGlState<2, Shadow> state(
            {.viewport_width = WIDTH, .viewport_height = HEIGHT, .framebuffer = framebuffer}, shadow);
        effects.add(front(0, WIDTH, HEIGHT));
        CHECK(!material.draw_close(*frame, effects, *view, image, target));
        Pixels unchanged;
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, unchanged.data());
        CHECK(unchanged == initial);
    }

    // Place a fragment center just inside the fractional right crop edge. Its linear
    // footprint would include the purple texel at x=15 unless the last tap is clamped.
    auto shifted = visible;
    shifted[0].origin.x -= .1;
    Close translated;
    CHECK(translated.start(local, shifted, 42, 1));
    translated.emit({*view->authorized_local_crop, true}, *frame);
    effects.count = 0;
    const auto filtered_edge = draw(effects);
    CHECK(filtered_edge[5 * WIDTH + 14] == UPPER);
    CHECK(filtered_edge[5 * WIDTH + 15] == CLEAR);

    // Two disjoint source rectangles with coincident poses. The later, lower print
    // must still show the earlier print through its alpha, in publication order.
    std::array folded{Close::VisibleFace::flat(Box{6, 1, 2, 2}),
        Close::VisibleFace::flat(Box{6, 5, 2, 2})};
    folded[1].origin.y = folded[0].origin.y;
    Close overlapping;
    CHECK(overlapping.start(local, folded, 42, 1));
    overlapping.emit({*view->authorized_local_crop, true}, *frame);
    const auto overlap = draw(effects)[3 * WIDTH + 5];
    constexpr Pixel EXPECTED_OVERLAP{29, 25, 86, 192}; // LOWER over UPPER.
    for (std::size_t i = 0; i < overlap.size(); ++i)
        CHECK(std::abs(int(overlap[i]) - int(EXPECTED_OVERLAP[i])) <= 1);

    model.advance(.21);
    model.advance(.21);
    model.emit({*view->authorized_local_crop, true}, *frame, false);
    CHECK(frame->source_faces().size() > 2);
    const auto bent = draw(effects);
    CHECK(bent != initial);
    CHECK(std::ranges::count_if(bent, [](Pixel pixel) { return pixel[3] == 128; }) > 30);
    CHECK(std::ranges::all_of(bent, [&](Pixel pixel) {
        // Source-over can raise alpha at a fold, but cannot raise the straight red
        // ratio beyond the known print palette. An off-crop magenta tap would.
        return int(pixel[0]) * 128 <= (int(UPPER[0]) + 1) * int(pixel[3]);
    }));

    model.cancel();
    model.emit({*view->authorized_local_crop, true}, *frame);
    CHECK(std::ranges::all_of(draw(effects), [&](Pixel pixel) { return pixel == CLEAR; }));
    auto offscreen = placement;
    offscreen.source.x = -18.25;
    const auto offscreen_view = CConstructionMaterial::prepare_view(offscreen);
    CHECK(offscreen_view && !offscreen_view->authorized_local_crop);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glDeleteBuffers(1, &ambient_ebo);
    glDeleteVertexArrays(1, &ambient_vao);
}

void check_close_body_mask(CConstructionMaterial &material, GLuint image, GLuint target, GLuint framebuffer) {
    using Close = construction::studies::RegionalClose;
    const SConstructionTarget placement{.canvas = {0, 0, WIDTH, HEIGHT},
        .source = {-3, 0, 20, 16}, .body = {102, 202, 12, 8}, .capture = {100, 200, 20, 16},
        .image_origin = {0, 0}, .image_size = {WIDTH, HEIGHT},
        .mask_mode = ConstructionMaskMode::Body, .mask_rounding = 4};
    const auto view = CConstructionMaterial::prepare_view(placement);
    CHECK(view && view->authorized_local_crop);
    if (!view || !view->authorized_local_crop) return;
    auto squared_placement = placement;
    squared_placement.mask_rounding_power = 4;
    const auto squared_view = CConstructionMaterial::prepare_view(squared_placement);
    CHECK(squared_view);
    if (!squared_view) return;

    constexpr Pixel GREEN{0, 255, 0, 255}, RED{255, 0, 0, 255}, WHITE{255, 255, 255, 255}, CLEAR{};
    Pixels source;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            source[y * WIDTH + x] = x < 11 ? GREEN : RED;
    glBindTexture(GL_TEXTURE_2D, image);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, source.data());

    const Box local{0, 0, placement.capture.w, placement.capture.h};
    std::array visible{Close::VisibleFace::flat(local)};
    visible[0].origin.x += 8;
    visible[0].origin.y += 3;
    Close model;
    CHECK(model.start(local, visible, 42, 1));
    const auto frame = std::make_unique_for_overwrite<Close::Frame>();
    model.emit({*view->authorized_local_crop, true}, *frame);
    CHECK(!frame->source_faces().empty());
    ConstructionBatch effects;
    effects.add({.origin = {24, 4, 0}, .u = {4, 0, 0}, .v = {0, 4, 0}, .uv = {},
        .tint = RED, .surface = ConstructionSurface::Solid});

    Shadow shadow;
    const auto readback = [&](const CConstructionMaterial::View &draw_view) {
        Pixels pixels{};
        gl::CRawGlState<2, Shadow> state({.viewport_width = WIDTH, .viewport_height = HEIGHT,
            .framebuffer = framebuffer}, shadow);
        CHECK(material.draw_close(*frame, effects, draw_view, image, target));
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto pixels = readback(*view);
    const auto squared_pixels = readback(*squared_view);

    // The body translates to [7,19) x [5,13), but the source crop removes column 7.
    CHECK(pixels[8 * WIDTH + 7] == CLEAR);
    CHECK(pixels[8 * WIDTH + 8] == WHITE);  // Source green (red = 0) still covers.
    CHECK(pixels[8 * WIDTH + 20] == CLEAR); // Source red outside the body never covers.
    CHECK(pixels[5 * WIDTH + 18] == CLEAR); // Inside the box, outside the rounded corner.
    const auto &edge = pixels[6 * WIDTH + 18];
    CHECK(edge[0] > 0 && edge[0] < 255);
    CHECK((edge == Pixel{edge[0], edge[0], edge[0], edge[0]}));
    CHECK(pixels[5 * WIDTH + 22] == CLEAR); // Close effect overlay never covers.
    // A fourth-power corner includes more of the same diagonal than a circle.
    const auto &squared_edge = squared_pixels[5 * WIDTH + 18];
    CHECK(squared_edge[0] > 0 && squared_edge[0] < 255);
    CHECK((squared_edge == Pixel{squared_edge[0], squared_edge[0], squared_edge[0], squared_edge[0]}));
    CHECK(squared_pixels[8 * WIDTH + 7] == CLEAR);
    CHECK(squared_pixels[8 * WIDTH + 8] == WHITE);
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
    check_retained_regional_close(material, textures[0], textures[1], framebuffer);
    check_pixels(material,textures[0],textures[1],framebuffer, 1.25, false);
    check_blur_mask(material, textures[0], textures[1], framebuffer);
    check_close_body_mask(material, textures[0], textures[1], framebuffer);
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
