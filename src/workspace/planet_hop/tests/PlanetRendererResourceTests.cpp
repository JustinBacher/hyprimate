#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <GLES3/gl32.h>

// Extension prototypes require core GLES types and calling conventions first.
#include <GLES2/gl2ext.h>
#include <png.h>
#include <unistd.h>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "PlanetRendererTestFixture.hpp"

namespace planet_renderer_tests {
    using namespace hyprimate::workspace;
    void check_depth(PlanetTestRenderer &renderer) {
        CHECK(renderer.prepare_depth(""));
        CHECK(!renderer.has_depth_image());
        CHECK(!renderer.prepare_depth("/planet-hop-missing-depth.png"));
        CHECK(!renderer.depth_error().empty());
        const auto PATH = (std::filesystem::temp_directory_path() /
                           ("planet-hop-depth-" + std::to_string(getpid()) + ".png"))
                              .string();
        const std::string OVERSIZE = PATH + ".oversize";
        png_image oversized{};
        oversized.version = PNG_IMAGE_VERSION;
        oversized.width = 4097;
        oversized.height = 1;
        oversized.format = PNG_FORMAT_GRAY;
        const std::vector<unsigned char> LARGE_ROW(4097);
        CHECK(png_image_write_to_file(
                  &oversized, OVERSIZE.c_str(), 0, LARGE_ROW.data(), 0, nullptr) != 0);
        CHECK(!renderer.prepare_depth(OVERSIZE));
        CHECK(!renderer.has_depth_image());
        std::filesystem::remove(OVERSIZE);
        const std::string MALFORMED = PATH + ".bad";
        {
            std::ofstream invalid(MALFORMED);
            invalid << "not a PNG";
        }
        CHECK(!renderer.prepare_depth(MALFORMED));
        CHECK(!renderer.has_depth_image());
        std::filesystem::remove(MALFORMED);
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = image.height = 2;
        image.format = PNG_FORMAT_GRAY;
        constexpr std::array<unsigned char, 4> PIXELS{0, 85, 170, 255};
        CHECK(png_image_write_to_file(&image, PATH.c_str(), 0, PIXELS.data(), 0, nullptr) != 0);
        CHECK(renderer.prepare_depth(PATH));
        CHECK(renderer.has_depth_image());
        CHECK(renderer.depth_error().empty());
        std::filesystem::remove(PATH);
        // A cached path is not synchronously re-decoded or restatted during rendering.
        CHECK(renderer.prepare_depth(PATH));
        CHECK(renderer.has_depth_image());
    }
    void check_allocation_retry() {
        const auto REQUIRE = [](bool condition) {
            CHECK(condition);
            if (!condition) {
                std::exit(EXIT_FAILURE);
            }
        };
        PlanetTestRenderer renderer;
        REQUIRE(renderer.initialize_raw());
        REQUIRE(renderer.prepare_depth(""));
        constexpr std::array<unsigned char, 4> COLOR{160, 90, 40, 255};
        const GLuint INPUT = texture(1, 1, COLOR.data());
        const GLuint OUTPUT = texture(WIDTH, HEIGHT, nullptr);
        REQUIRE(INPUT != 0 && OUTPUT != 0);
        PlanetFrameTextures textures;
        textures.source.wallpaper = textures.source.composite = INPUT;
        textures.destination.wallpaper = textures.destination.composite = INPUT;
        textures.output = OUTPUT;
        textures.width = WIDTH;
        textures.height = HEIGHT;
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 5, .numbered = 5, .logical_order = 2});
        auto frame = planet_hop::evaluate(JOURNEY, .5, static_cast<double>(WIDTH) / HEIGHT);
        PlanetRenderSettings settings;
        settings.quality = 0;
        settings.clouds = settings.lighting = false;
        settings.star_density = settings.atmosphere_intensity = 0;
        const auto DRAW = [&](double progress = .5,
                              std::span<const PlanetNeighborTextures> neighbors = {}) {
            return renderer.draw_raw(textures, frame, progress, settings, neighbors);
        };
        const auto READ = [&] {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(WIDTH * HEIGHT * 4));
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        REQUIRE(DRAW());
        const auto BASELINE = READ();
        REQUIRE(glGetError() == GL_NO_ERROR);
        // Neither a failed vertex store nor a failed index store publishes a VAO.
        for (unsigned occurrence : {1U, 2U}) {
            settings.quality = static_cast<int>(occurrence);
            {
                gl_harness::FailAllocation fail(gl_harness::Allocation::buffer, occurrence);
                CHECK(!DRAW());
            }
            settings.quality = 0;
            REQUIRE(DRAW());
            CHECK(READ() == BASELINE);
            settings.quality = static_cast<int>(occurrence);
            REQUIRE(DRAW());
            const auto RETRIED = READ();
            REQUIRE(DRAW());
            CHECK(READ() == RETRIED);
        }
        settings.quality = 0;
        // Fill all eight depth slots, then fail the growth that would evict the
        // original size. The old small allocation and its pixels must still work.
        std::vector<GLuint> outputs;
        for (int index = 1; index < 8; ++index) {
            textures.width = WIDTH + index;
            textures.output = texture(textures.width, HEIGHT, nullptr);
            outputs.push_back(textures.output);
            REQUIRE(textures.output != 0 && DRAW(0));
        }
        textures.width = WIDTH * 2;
        textures.height = HEIGHT * 2;
        textures.output = texture(textures.width, textures.height, nullptr);
        const GLuint LARGE = textures.output;
        REQUIRE(LARGE != 0);
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::renderbuffer);
            CHECK(!DRAW(0));
        }
        textures.width = WIDTH;
        textures.height = HEIGHT;
        textures.output = OUTPUT;
        REQUIRE(DRAW());
        CHECK(READ() == BASELINE);
        textures.width = WIDTH * 2;
        textures.height = HEIGHT * 2;
        textures.output = LARGE;
        REQUIRE(DRAW(0));
        std::array<unsigned char, 4> center{};
        glReadPixels(WIDTH, HEIGHT, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center.data());
        CHECK(center == COLOR);
        textures.width = WIDTH;
        textures.height = HEIGHT;
        textures.output = OUTPUT;
        REQUIRE(DRAW());
        CHECK(READ() == BASELINE);

        // A failed color rebind can leave a COMPLETE FBO pointing at the old
        // image. Reject it rather than drawing into that retained attachment.
        GLuint volume = 0;
        glGenTextures(1, &volume);
        glBindTexture(GL_TEXTURE_3D, volume);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        REQUIRE(volume != 0 && glGetError() == GL_NO_ERROR);
        textures.output = volume;
        CHECK(!DRAW(0));
        CHECK(glGetError() == GL_INVALID_OPERATION);
        textures.output = OUTPUT;
        REQUIRE(DRAW());
        CHECK(READ() == BASELINE);

        // The optional custom PNG falls back procedurally on allocation failure,
        // retaining decoded pixels even after the source file has disappeared.
        const auto PATH = (std::filesystem::temp_directory_path() /
                           ("planet-hop-retry-" + std::to_string(getpid()) + ".png"))
                              .string();
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = image.height = 2;
        image.format = PNG_FORMAT_GRAY;
        constexpr std::array<unsigned char, 4> DEPTH{0, 255, 85, 170};
        REQUIRE(png_image_write_to_file(&image, PATH.c_str(), 0, DEPTH.data(), 0, nullptr) != 0);
        REQUIRE(renderer.prepare_depth(PATH));
        std::filesystem::remove(PATH);
        const auto DEPTH_SOURCE = [&] {
            GLint program = 0;
            GLint custom = -1;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            const GLint LOCATION =
                glGetUniformLocation(static_cast<GLuint>(program), "depthSource");
            REQUIRE(LOCATION >= 0);
            glGetUniformiv(static_cast<GLuint>(program), LOCATION, &custom);
            return custom;
        };
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::texture_2d);
            REQUIRE(DRAW());
            CHECK(DEPTH_SOURCE() == 0);
            CHECK(READ() == BASELINE);
        }
        CHECK(renderer.has_depth_image());
        REQUIRE(DRAW());
        CHECK(DEPTH_SOURCE() == 1);
        const auto CUSTOM = READ();
        REQUIRE(DRAW());
        CHECK(READ() == CUSTOM);
        const std::string REPLACEMENT = PATH + ".replacement";
        constexpr std::array<unsigned char, 4> OTHER_DEPTH{255, 0, 170, 85};
        REQUIRE(png_image_write_to_file(
                    &image, REPLACEMENT.c_str(), 0, OTHER_DEPTH.data(), 0, nullptr) != 0);
        REQUIRE(renderer.prepare_depth(REPLACEMENT));
        std::filesystem::remove(REPLACEMENT);
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::texture_2d);
            REQUIRE(DRAW());
            CHECK(DEPTH_SOURCE() == 0);
            CHECK(READ() == BASELINE);  // Never sample the retained previous PNG.
        }
        REQUIRE(DRAW());
        CHECK(DEPTH_SOURCE() == 1);
        // Empty configuration intentionally releases custom depth.
        REQUIRE(renderer.prepare_depth(""));
        REQUIRE(DRAW());
        CHECK(DEPTH_SOURCE() == 0);
        CHECK(READ() == BASELINE);

        // Unknown inventory bodies require the neutral material. Failure is not
        // cached as readiness, and the same instance retries without reinitializing.
        frame.source_center = {.x = -20};
        frame.destination_center = {.x = 20};
        frame.source_rotation = frame.destination_rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        frame.view_projection = {.15F, 0, 0, 0, 0, .225F, 0, 0, 0, 0, -.05F, 0, 0, 0, 0, 1};
        frame.inverse_view_projection = {
            1 / .15F, 0, 0, 0, 0, 1 / .225F, 0, 0, 0, 0, -20, 0, 0, 0, 0, 1};
        frame.camera_position = {.z = 10};
        frame.source_curvature = frame.destination_curvature = frame.endpoint_envelope = 1;
        settings.depth_enabled = false;
        const std::array<PlanetNeighborTextures, 1> UNKNOWN{
            {{.endpoint = {.identity = 3, .placement = 3}, .center = {}}}};
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::texture_2d);
            CHECK(!DRAW(.5, UNKNOWN));
        }
        REQUIRE(DRAW(.5, UNKNOWN));
        const auto NEUTRAL = READ();
        REQUIRE(DRAW(.5, UNKNOWN));
        CHECK(READ() == NEUTRAL);
        REQUIRE(DRAW());
        CHECK(READ() != NEUTRAL);
        // Initialization caches the complete borrowed program tuple, not merely
        // the presence of a planet program or the first successful initialization.
        namespace shader = planet_shader;
        const PlanetPrograms PROGRAMS{
            .planet = gl_harness::build_program(shader::VERTEX, shader::FRAGMENT),
            .quad = gl_harness::build_program(shader::QUAD_VERTEX, shader::QUAD_FRAGMENT),
            .sky = gl_harness::build_program(shader::SKY_VERTEX, shader::SKY_FRAGMENT)};
        const GLuint OTHER_QUAD =
            gl_harness::build_program(shader::QUAD_VERTEX, shader::QUAD_FRAGMENT);
        REQUIRE(PROGRAMS.planet != 0 && PROGRAMS.quad != 0 && PROGRAMS.sky != 0 && OTHER_QUAD != 0);
        PlanetGlRenderer raw;
        CHECK(!raw.initialize_raw({}));
        REQUIRE(raw.initialize_raw(PROGRAMS));
        REQUIRE(raw.draw_raw(textures, frame, 0, settings));
        GLint current = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        CHECK(static_cast<GLuint>(current) == PROGRAMS.quad);
        auto replacement = PROGRAMS;
        replacement.quad = OTHER_QUAD;
        REQUIRE(raw.initialize_raw(replacement));
        REQUIRE(raw.draw_raw(textures, frame, 0, settings));
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        CHECK(static_cast<GLuint>(current) == OTHER_QUAD);
        CHECK(!raw.initialize_raw({}));
        REQUIRE(raw.draw_raw(textures, frame, 0, settings));
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        CHECK(static_cast<GLuint>(current) == OTHER_QUAD);
        raw.release_raw();
        for (const GLuint program : {PROGRAMS.planet, PROGRAMS.quad, PROGRAMS.sky, OTHER_QUAD}) {
            glDeleteProgram(program);
        }
        CHECK(glGetError() == GL_NO_ERROR);
        outputs.insert(outputs.end(), {INPUT, OUTPUT, LARGE, volume});
        glDeleteTextures(static_cast<GLsizei>(outputs.size()), outputs.data());
    }
    void check_renderer_ownership(PlanetTestRenderer &renderer,
        PlanetFrameTextures &textures,
        const planet_hop::Journey &journey,
        PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &expected,
        GLuint source,
        GLuint destination,
        GLuint output) {
        // The renderer owns the target framebuffer and depth attachment, not the
        // compositor-supplied color attachments. Test both sides of that boundary.
        GLint framebuffer = 0;
        GLint depth = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
            &depth);
        CHECK(framebuffer != 0 && depth != 0);
        renderer.release_raw();
        CHECK(glIsFramebuffer(static_cast<GLuint>(framebuffer)) == GL_FALSE);
        CHECK(glIsRenderbuffer(static_cast<GLuint>(depth)) == GL_FALSE);
        CHECK(glIsTexture(source) == GL_TRUE && glIsTexture(destination) == GL_TRUE &&
              glIsTexture(output) == GL_TRUE);
        CHECK(renderer.initialize_raw());
        CHECK(renderer.draw_raw(textures,
            planet_hop::evaluate(journey, 1.0, static_cast<double>(width) / height),
            1.0,
            settings));
        std::vector<unsigned char> actual(expected.size());
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        CHECK(actual == expected);
        renderer.release_raw();
        const std::array<GLuint, 3> TEXTURES{source, destination, output};
        glDeleteTextures(static_cast<GLsizei>(TEXTURES.size()), TEXTURES.data());
    }
}  // namespace planet_renderer_tests
