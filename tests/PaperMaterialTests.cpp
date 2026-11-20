#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>  // gl2ext.h needs the Khronos types and calling-convention macros.
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "PaperPoseBank.hpp"
#include "animations/paper/PaperMaterial.hpp"
#include "animations/paper/PaperShaders.hpp"

namespace {
    constexpr int WIDTH = 128, HEIGHT = 96;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;
    struct Target {
        GLuint framebuffer = 0, texture = 0;
        explicit Target(GLsizei width = WIDTH, GLsizei height = HEIGHT) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        }
        ~Target() {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteTextures(1, &texture);
        }
        Target(const Target &) = delete;
        Target &operator=(const Target &) = delete;
    };
    GLuint upload(const Image &pixels, GLsizei width = WIDTH, GLsizei height = HEIGHT) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return texture;
    }
    Image read(GLuint framebuffer) {
        Image pixels(WIDTH * HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    }
    Pixel top(const Image &pixels, int x, int y) {
        return pixels[(HEIGHT - 1 - y) * WIDTH + x];
    }
    void near(Pixel actual, Pixel expected) {
        bool equal = true;
        for (std::size_t channel = 0; channel < 4; ++channel)
            equal = equal && std::abs(static_cast<int>(actual[channel]) -
                                      static_cast<int>(expected[channel])) <= 1;
        if (!equal)
            std::fprintf(stderr,
                "pixel actual=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
                actual[0],
                actual[1],
                actual[2],
                actual[3],
                expected[0],
                expected[1],
                expected[2],
                expected[3]);
        CHECK(equal);
    }
    SPaperTarget placement() {
        return {.canvas = {WIDTH, HEIGHT},
            .origin = {},
            .source = {0, 0, WIDTH, HEIGHT},
            .texture_origin = {},
            .texture_size = {WIDTH, HEIGHT}};
    }
    hyprimate::PaperSurface sheet() {
        const double UNIT = std::sqrt(static_cast<double>(WIDTH) * HEIGHT);
        const double X = WIDTH / (2 * UNIT), Y = HEIGHT / (2 * UNIT);
        hyprimate::PaperSurface surface;
        surface.size = {WIDTH, HEIGHT};
        surface.shading = 0;
        surface.points = {
            {{-X, -Y, 0}, {0, 0}}, {{X, -Y, 0}, {1, 0}}, {{X, Y, 0}, {1, 1}}, {{-X, Y, 0}, {0, 1}}};
        surface.faces = {{{0, 1, 2}, {}}, {{0, 2, 3}, {}}};
        return surface;
    }
    Image printed_text() {
        Image pixels(WIDTH * HEIGHT);
        // One-pixel stems/counters, alternating lines, translucent coloured header.
        // The exact native endpoint must retain every stroke without resampling blur.
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x) {
                const bool INK = y % 9 >= 2 && y % 9 < 7 && x % 6 < 4 &&
                                 (x % 6 == 0 || y % 9 == 2 || (y % 9 == 4 && x % 6 < 3));
                pixels[y * WIDTH + x] = INK ? Pixel{12, 18, 24, 255} : Pixel{236, 240, 244, 255};
                if (y < 12) pixels[y * WIDTH + x] = {static_cast<unsigned char>(x), 48, 64, 128};
            }
        return pixels;
    }
    void native_text_and_crop(
        CPaperMaterial &material, GLuint texture, const Target &output, const Image &source) {
        auto surface = sheet();
        surface.shading = 1;
        auto target = placement();
        CHECK(material.draw(surface, target, texture, output.framebuffer));
        const auto ACTUAL = read(output.framebuffer);
        std::size_t mismatches = 0;
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x) {
                const auto PIXEL = top(ACTUAL, x, y);
                const auto EXPECTED = source[y * WIDTH + x];
                bool equal = true;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    equal = equal && std::abs(static_cast<int>(PIXEL[channel]) -
                                              static_cast<int>(EXPECTED[channel])) <= 1;
                if (!equal && ++mismatches <= 4) {
                    std::fprintf(stderr, "native mismatch x=%d y=%d ", x, y);
                    near(PIXEL, EXPECTED);
                }
            }
        CHECK(mismatches == 0);
        // Canvas/source/texture origins are independent. Source scaling uses geometric
        // mean units; cropping samples the corresponding capture rectangle, not all UVs.
        target.origin = {100, 200};
        target.source = {124, 216, 64, 48};
        target.texture_origin = {100, 200};
        CHECK(material.draw(surface, target, texture, output.framebuffer));
        const auto CROP = read(output.framebuffer);
        near(top(CROP, 8, 8), {0, 0, 0, 0});
        near(top(CROP, 56, 40), source[40 * WIDTH + 56]);
        target.alpha = .5F;
        surface.opacity = .5F;
        CHECK(material.draw(surface, target, texture, output.framebuffer));
        const auto FADED = read(output.framebuffer);
        auto expected = source[40 * WIDTH + 56];
        for (auto &channel : expected)
            channel = static_cast<unsigned char>(std::lround(channel * .25));
        near(top(FADED, 56, 40), expected);
    }
    void sided_depth_and_holes(CPaperMaterial &material, const Target &output) {
        Image colors(WIDTH * HEIGHT);
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x)
                colors[y * WIDTH + x] =
                    x < WIDTH / 2 ? Pixel{120, 12, 6, 128} : Pixel{4, 100, 20, 128};
        const auto TEXTURE = upload(colors);
        auto surface = sheet();
        // Two overlapping sheets, arbitrary submission order, opposite windings.
        for (auto &point : surface.points) point.uv = {.25, .5};
        auto front = sheet();
        for (auto &point : front.points) {
            point.position.z = .5;
            point.uv = {.75, .5};
        }
        surface.points.insert(surface.points.end(), front.points.begin(), front.points.end());
        surface.faces.push_back({{6, 5, 4}, {}});
        surface.faces.push_back({{7, 6, 4}, {}});
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        near(top(read(output.framebuffer), 64, 48), {4, 100, 20, 128});
        std::reverse(surface.faces.begin(), surface.faces.end());
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        near(top(read(output.framebuffer), 64, 48), {4, 100, 20, 128});
        // Transparent foreground does not hide the sheet behind or contaminate alpha.
        for (auto &pixel : colors)
            if (pixel[1] == 100) pixel = {0, 0, 0, 0};
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, TEXTURE);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        near(top(read(output.framebuffer), 64, 48), {120, 12, 6, 128});
        glDeleteTextures(1, &TEXTURE);
    }
    void crease_and_contact(CPaperMaterial &material, GLuint texture, const Target &output) {
        auto surface = sheet();
        surface.points[3].position.z = .4;
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        auto vertices = material.vertices();
        CHECK(vertices.size() == 24);
        CHECK(vertices[0].nx == vertices[12].nx);
        CHECK(vertices[0].ny == vertices[12].ny);
        CHECK(vertices[0].nz == vertices[12].nz);
        const auto LINEAR_X = (surface.points[0].position.x + surface.points[1].position.x) * .5;
        const auto LINEAR_Y = (surface.points[0].position.y + surface.points[1].position.y) * .5;
        const auto LINEAR_Z = (surface.points[0].position.z + surface.points[1].position.z) * .5;
        const auto DX = vertices[1].x - LINEAR_X, DY = vertices[1].y - LINEAR_Y,
                   DZ = vertices[1].z - LINEAR_Z;
        CHECK(std::sqrt(DX * DX + DY * DY + DZ * DZ) > 1e-5);
        CHECK(std::sqrt(DX * DX + DY * DY + DZ * DZ) <= hyprimate::PAPER_PATCH_BULGE + 1e-6);
        // Only the second triangle marks the shared diagonal. Both sides must respect it.
        surface.faces[1].crease[0] = 1;
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        vertices = material.vertices();
        CHECK(std::abs(vertices[0].nx - vertices[12].nx) > .01F);
        constexpr std::array<std::size_t, 3> CORNERS{0, 4, 8};
        for (std::size_t face = 0; face < surface.faces.size(); ++face)
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto POSITION = surface.points[surface.faces[face].vertex[corner]].position;
                const auto VERTEX = vertices[face * 12 + CORNERS[corner]];
                CHECK(VERTEX.x == static_cast<float>(POSITION.x));
                CHECK(VERTEX.y == static_cast<float>(POSITION.y));
                CHECK(VERTEX.z == static_cast<float>(POSITION.z));
            }
        // A CCD contact anywhere on an incident face makes all its patches linear.
        surface.faces[1].crease[0] = 0;
        surface.points[0].contact = true;
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        vertices = material.vertices();
        CHECK(std::abs(vertices[1].x - LINEAR_X) < 1e-7);
        CHECK(std::abs(vertices[1].y - LINEAR_Y) < 1e-7);
        CHECK(std::abs(vertices[1].z - LINEAR_Z) < 1e-7);
        // A render-only remesh of a planar panel must not change geometry or text.
        surface = sheet();
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        const auto BEFORE = read(output.framebuffer);
        surface.points.push_back({{0, 0, 0}, {.5, .5}});
        surface.faces = {{{0, 1, 4}, {}}, {{1, 2, 4}, {}}, {{2, 3, 4}, {}}, {{3, 0, 4}, {}}};
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        CHECK(BEFORE == read(output.framebuffer));
    }
    void continuous_crease(CPaperMaterial &material, GLuint texture, const Target &output) {
        auto surface = sheet();
        surface.points[3].position.z = .4;
        SPaperVertex previous_normal{}, previous_midpoint{};
        for (int step = 0; step <= 100; ++step) {
            surface.faces[1].crease[0] = static_cast<float>(step) / 100;
            CHECK(material.draw(surface, placement(), texture, output.framebuffer));
            const auto VERTICES = material.vertices();
            if (step > 0) {
                CHECK(std::abs(VERTICES[0].nx - previous_normal.nx) < .03F);
                CHECK(std::abs(VERTICES[0].ny - previous_normal.ny) < .03F);
                CHECK(std::abs(VERTICES[0].nz - previous_normal.nz) < .03F);
                CHECK(std::abs(VERTICES[1].x - previous_midpoint.x) < .0005F);
                CHECK(std::abs(VERTICES[1].y - previous_midpoint.y) < .0005F);
                CHECK(std::abs(VERTICES[1].z - previous_midpoint.z) < .0005F);
            }
            previous_normal = VERTICES[0];
            previous_midpoint = VERTICES[1];
        }
    }
    void composite_orientation(CPaperMaterial &material, GLuint texture, const Target &output) {
        auto surface = sheet();
        surface.shading = 1;
        auto target = placement();
        target.origin = {100, 200};
        target.source = {112, 208, 60, 40};
        target.texture_origin = {100, 200};
        // Asymmetric physical deformation distinguishes a framebuffer-boundary fix
        // from merely flipping the captured UVs inside otherwise inverted geometry.
        surface.points[1].position.x *= .7;
        surface.points[2].position.z = .15;
        CHECK(material.draw(surface, target, texture, output.framebuffer));
        const auto GL_LAYOUT = read(output.framebuffer);
        target.output_top_first = true;
        CHECK(material.draw(surface, target, texture, output.framebuffer));
        const auto COMPOSITOR_LAYOUT = read(output.framebuffer);
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x)
                CHECK(COMPOSITOR_LAYOUT[y * WIDTH + x] == top(GL_LAYOUT, x, y));
        CHECK(COMPOSITOR_LAYOUT[10 * WIDTH + 20][3] > 0);
        CHECK(COMPOSITOR_LAYOUT[70 * WIDTH + 20][3] == 0);
        CHECK(COMPOSITOR_LAYOUT[10 * WIDTH + 90][3] == 0);
    }
    void thickness_and_matte(CPaperMaterial &material, const Target &output) {
        const auto TEXTURE = upload(Image(WIDTH * HEIGHT, Pixel{210, 210, 210, 230}));
        auto surface = sheet();
        surface.shading = 1;
        for (auto &point : surface.points) {
            const double Y = point.position.y;
            point.position.y = Y * std::cos(1.25);
            point.position.z = Y * std::sin(1.25);
        }
        surface.thickness = .003F;
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto THIN = read(output.framebuffer);
        surface.thickness = .006F;
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto THICK = read(output.framebuffer);
        CHECK(top(THIN, 64, 48)[0] > top(THICK, 64, 48)[0] + 1);
        for (std::size_t pixel = 0; pixel < THIN.size(); ++pixel)
            CHECK(THIN[pixel][3] == THICK[pixel][3]);
        // Repeatability under a fixed physical/material camera: no temporal grain.
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        CHECK(THICK == read(output.framebuffer));
        // The minified fibre ensemble keeps a matte diffuse lobe even when both
        // spatial grain bands are filtered out. Compare against the otherwise
        // identical Lambert+thickness response, not unrelated surface samples.
        const double LIGHT_Z = 1 / std::sqrt(1 + .32 * .32 + .42 * .42);
        const double DIFFUSE = (.42 * std::sin(1.25) + std::cos(1.25)) * LIGHT_Z;
        const double LAMBERT = std::clamp(1 + .62 * (DIFFUSE - LIGHT_Z), .42, 1.08);
        const double GRAZING = 1 - std::cos(1.25);
        const double WITHOUT_MATTE = 210 * LAMBERT * (1 - .18 * GRAZING * GRAZING * .6);
        CHECK(top(THICK, 64, 48)[0] + 2 < WITHOUT_MATTE);
        glDeleteTextures(1, &TEXTURE);
    }
    void fold_shadow(CPaperMaterial &material, const Target &output) {
        const Image WHITE(WIDTH * HEIGHT, Pixel{200, 200, 200, 220});
        const auto TEXTURE = upload(WHITE);
        auto surface = sheet();
        surface.shading = 1;
        // A parallel, raised half-panel: same normals, so only geometric occlusion can
        // darken the receiver just to its right. No crease/depth means no fake decal.
        auto raised = sheet();
        for (auto &point : raised.points) {
            point.position.x = point.position.x * .3 - .15;
            point.position.y *= .5;
            point.position.z = .22;
        }
        surface.points.insert(surface.points.end(), raised.points.begin(), raised.points.end());
        surface.faces.push_back({{4, 5, 6}, {}});
        surface.faces.push_back({{4, 6, 7}, {}});
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto SHADOW = read(output.framebuffer);
        bool found = false;
        for (int y = HEIGHT / 3; y < HEIGHT * 2 / 3; ++y)
            for (int x = WIDTH / 3; x < WIDTH * 2 / 3; ++x) {
                const auto PIXEL = top(SHADOW, x, y);
                CHECK(PIXEL[3] == 220);
                found = found || PIXEL[0] < 190;
            }
        CHECK(found);
        surface.shading = 0;
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto UNSHADED = read(output.framebuffer);
        surface.shading = .5F;
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto HALF_SHADE = read(output.framebuffer);
        for (std::size_t pixel = 0; pixel < SHADOW.size(); ++pixel) {
            CHECK(SHADOW[pixel][3] == UNSHADED[pixel][3]);
            CHECK(HALF_SHADE[pixel][3] == UNSHADED[pixel][3]);
            CHECK(HALF_SHADE[pixel][0] >= SHADOW[pixel][0]);
            CHECK(HALF_SHADE[pixel][0] <= UNSHADED[pixel][0]);
        }
        surface.shading = 1;
        surface.points.resize(4);
        surface.faces.resize(2);
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        near(top(read(output.framebuffer), 64, 48), {200, 200, 200, 220});
        // A caster entirely outside the CAMERA image must still darken a visible
        // receiver. Screen-depth effects cannot pass this directional-shadow test.
        auto offscreen = sheet();
        for (auto &point : offscreen.points) {
            point.position.x = point.position.x * .1 - .68;
            point.position.y *= .25;
            point.position.z = .7;
        }
        surface.points.insert(
            surface.points.end(), offscreen.points.begin(), offscreen.points.end());
        surface.faces.push_back({{4, 5, 6}, {}});
        surface.faces.push_back({{4, 6, 7}, {}});
        CHECK(material.draw(surface, placement(), TEXTURE, output.framebuffer));
        const auto OFFSCREEN = read(output.framebuffer);
        bool cast = false;
        for (int y = HEIGHT / 3; y < HEIGHT * 3 / 4; ++y)
            for (int x = 0; x < WIDTH / 4; ++x) cast = cast || top(OFFSCREEN, x, y)[0] < 180;
        CHECK(cast);
        glDeleteTextures(1, &TEXTURE);
    }
    void isolation_and_resize(
        GLuint program, CPaperMaterial &material, GLuint texture, const Target &output) {
        CPaperMaterial scratch;
        CHECK(scratch.initialize(program));
        auto surface = sheet();
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        const auto BEFORE = read(output.framebuffer);
        auto small = placement();
        small.canvas = {64, 48};
        small.source = {0, 0, 64, 48};
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        glClearDepthf(.25F);
        glClearColor(.1F, .2F, .3F, .4F);
        CHECK(scratch.draw(surface, small, texture, output.framebuffer));
        GLint function = 0;
        GLboolean mask = GL_TRUE;
        GLfloat clear = 0;
        glGetIntegerv(GL_DEPTH_FUNC, &function);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear);
        CHECK(function == GL_GEQUAL);
        CHECK(mask == GL_FALSE);
        CHECK(clear == .25F);
        CHECK(material.draw(surface, placement(), texture, output.framebuffer));
        CHECK(BEFORE == read(output.framebuffer));
        surface.faces[0].vertex[0] = 99999;
        CHECK(!material.draw(surface, placement(), texture, output.framebuffer));
        CHECK(!material.draw(sheet(), placement(), 0, output.framebuffer));
        // Grow in each axis independently, then return to a small shaded canvas.
        // Shadow allocation is separately capped, not the retained colour high-water.
        surface = sheet();
        surface.shading = 1;
        auto tall = placement();
        tall.canvas = {64, 1536};
        CHECK(scratch.draw(surface, tall, texture, output.framebuffer));
        auto wide = placement();
        wide.canvas = {1536, 64};
        CHECK(scratch.draw(surface, wide, texture, output.framebuffer));
        GLint shadow_width = 0, shadow_height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &shadow_width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &shadow_height);
        CHECK(shadow_width == 1024);
        CHECK(shadow_height == 1024);
        CHECK(scratch.draw(surface, placement(), texture, output.framebuffer));
        CHECK(BEFORE == read(output.framebuffer));
        CHECK(glGetError() == GL_NO_ERROR);
        scratch.release();
    }

    void benchmark(GLuint program,
        int frames,
        int windows,
        float shading,
        int width,
        int height,
        int cells,
        bool crumpled) {
        std::vector<CPaperMaterial> materials(static_cast<std::size_t>(windows));
        for (auto &material : materials) CHECK(material.initialize(program));
        const Target output(width, height);
        const auto TILE = printed_text();
        Image dense(static_cast<std::size_t>(width) * height);
        for (int row = 0; row < height; ++row)
            for (int col = 0; col < width; ++col)
                dense[static_cast<std::size_t>(row) * width + col] =
                    TILE[(row % HEIGHT) * WIDTH + col % WIDTH];
        const auto TEXTURE = upload(dense, width, height);
        const SPaperTarget TARGET{
            .canvas = {static_cast<double>(width), static_cast<double>(height)},
            .source = {0, 0, static_cast<double>(width), static_cast<double>(height)},
            .texture_size = {static_cast<double>(width), static_cast<double>(height)},
            .output_top_first = true};
        auto bank = crumpled ? bench::paper_pose_bank(windows,
                                   {static_cast<double>(width), static_cast<double>(height)},
                                   shading)
                             : std::vector<std::vector<hyprimate::PaperSurface>>{};
        if (crumpled)
            for (const auto &poses : bank) CHECK(!poses.empty());
        std::printf("gl_renderer=%s\n", reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
        auto surface = sheet();
        surface.size = {static_cast<double>(width), static_cast<double>(height)};
        surface.shading = shading;
        // Typical 169-node sheet by default; a larger cell count stress-tests refinement.
        const int CELLS = cells;
        const double UNIT = std::sqrt(static_cast<double>(width) * height);
        surface.points.clear();
        surface.faces.clear();
        for (int row = 0; row <= CELLS; ++row)
            for (int col = 0; col <= CELLS; ++col) {
                const double U = static_cast<double>(col) / CELLS,
                             V = static_cast<double>(row) / CELLS;
                surface.points.push_back({{(U - .5) * width / UNIT,
                                              (V - .5) * height / UNIT,
                                              .12 * std::sin(U * 18) * std::sin(V * 15)},
                    {U, V}});
            }
        for (int row = 0; row < CELLS; ++row)
            for (int col = 0; col < CELLS; ++col) {
                const auto FIRST = static_cast<std::uint32_t>(row * (CELLS + 1) + col);
                surface.faces.push_back({{FIRST, FIRST + 1, FIRST + CELLS + 2}, {0, 0, 0}});
                surface.faces.push_back({{FIRST, FIRST + CELLS + 2, FIRST + CELLS + 1}, {0, 0, 0}});
            }
        for (std::size_t window = 0; window < materials.size(); ++window) {
            const auto &POSE = crumpled ? bank[window].front() : surface;
            CHECK(materials[window].draw(POSE, TARGET, TEXTURE, output.framebuffer));
        }
        (void)read(output.framebuffer);  // Warmup synchronization is outside the timed interval.
        const auto GEN =
            reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
        const auto BEGIN =
            reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
        const auto END = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
        const auto RESULT = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(
            eglGetProcAddress("glGetQueryObjectui64vEXT"));
        const auto DELETE =
            reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));
        bool timer = false;
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint index = 0; index < extensions; ++index)
            timer = timer ||
                    std::strcmp(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, index)),
                        "GL_EXT_disjoint_timer_query") == 0;
        timer = timer && GEN && BEGIN && END && RESULT && DELETE;
        GLuint query = 0;
        if (timer) {
            GEN(1, &query);
            BEGIN(GL_TIME_ELAPSED_EXT, query);
        }
        const auto START = std::chrono::steady_clock::now();
        bool success = true;
        for (int frame = 0; frame < frames; ++frame) {
            if (!crumpled)
                for (auto &point : surface.points)
                    point.position.z =
                        .12 * std::sin(point.uv.x * 18 + frame * .01) * std::sin(point.uv.y * 15);
            for (std::size_t window = 0; window < materials.size(); ++window) {
                const auto &POSE =
                    crumpled ? bank[window][static_cast<std::size_t>(frame) % bank[window].size()]
                             : surface;
                success =
                    materials[window].draw(POSE, TARGET, TEXTURE, output.framebuffer) && success;
            }
        }
        const auto STOP = std::chrono::steady_clock::now();
        if (timer) END(GL_TIME_ELAPSED_EXT);
        const double CPU = std::chrono::duration<double, std::milli>(STOP - START).count();
        (void)read(output.framebuffer);  // Resolve after submission, never glFinish in timed path.
        std::printf(
            "paper mode=%s frames=%d windows=%d canvas=%dx%d shading=%.3f "
            "first_physical_triangles=%zu cpu_submit_ms=%.3f cpu_ms_per_draw=%.6f success=%d\n",
            crumpled ? "crumpled" : "sine",
            frames,
            windows,
            width,
            height,
            shading,
            crumpled ? bank.front().front().faces.size() : surface.faces.size(),
            CPU,
            CPU / (frames * windows),
            success ? 1 : 0);
        if (timer) {
            GLuint64 elapsed = 0;
            GLint disjoint = 0;
            RESULT(query, GL_QUERY_RESULT_EXT, &elapsed);
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            std::printf("gpu_elapsed_ms=%.3f gpu_ms_per_draw=%.6f gpu_disjoint=%d\n",
                static_cast<double>(elapsed) / 1e6,
                static_cast<double>(elapsed) / (1e6 * frames * windows),
                disjoint);
            DELETE(1, &query);
        } else
            std::puts("gpu_timer=unavailable");
        CHECK(success);
        for (auto &material : materials) material.release();
        glDeleteTextures(1, &TEXTURE);
    }
}  // namespace

// NOLINTNEXTLINE(misc-const-correctness): standard hosted main signature.
int main(int argc, char **argv) {
    const gl_harness::CContext CONTEXT(WIDTH, HEIGHT);
    if (!CONTEXT.valid()) {
        std::fprintf(stderr, "PaperMaterialTests: GLES 3 context unavailable\n");
        return 77;
    }
    const auto PROGRAM = gl_harness::build_program(paper_shader::VERTEX, paper_shader::FRAGMENT);
    CHECK(PROGRAM != 0);
    if (PROGRAM == 0) return 1;
    glDisable(GL_DITHER);
    const Target OUTPUT;
    const auto SOURCE = printed_text();
    const auto TEXTURE = upload(SOURCE);
    CPaperMaterial material;
    CHECK(material.initialize(PROGRAM));
    if (argc > 1 &&
        (std::strcmp(argv[1], "--benchmark") == 0 || std::strcmp(argv[1], "--crumpled") == 0)) {
        const int FRAMES = argc > 2 ? std::clamp(std::atoi(argv[2]), 1, 100000) : 240;
        const int WINDOWS = argc > 3 ? std::clamp(std::atoi(argv[3]), 1, 32) : 4;
        const float SHADING = argc > 4 ? std::clamp(std::strtof(argv[4], nullptr), 0.F, 1.F) : .7F;
        const int BENCH_WIDTH = argc > 5 ? std::clamp(std::atoi(argv[5]), 64, 4096) : 1280;
        const int BENCH_HEIGHT = argc > 6 ? std::clamp(std::atoi(argv[6]), 64, 4096) : 720;
        const int CELLS = argc > 7 ? std::clamp(std::atoi(argv[7]), 2, 64) : 12;
        benchmark(PROGRAM,
            FRAMES,
            WINDOWS,
            SHADING,
            BENCH_WIDTH,
            BENCH_HEIGHT,
            CELLS,
            std::strcmp(argv[1], "--crumpled") == 0);
    } else {
        native_text_and_crop(material, TEXTURE, OUTPUT, SOURCE);
        sided_depth_and_holes(material, OUTPUT);
        crease_and_contact(material, TEXTURE, OUTPUT);
        continuous_crease(material, TEXTURE, OUTPUT);
        composite_orientation(material, TEXTURE, OUTPUT);
        thickness_and_matte(material, OUTPUT);
        fold_shadow(material, OUTPUT);
        isolation_and_resize(PROGRAM, material, TEXTURE, OUTPUT);
    }
    material.release();
    glDeleteTextures(1, &TEXTURE);
    glDeleteProgram(PROGRAM);
    return g_failures == 0 ? 0 : 1;
}
