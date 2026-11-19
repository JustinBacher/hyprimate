#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>

// Extension prototypes require core GLES types and calling conventions first.
#include <GLES2/gl2ext.h>
#include <png.h>
#include <unistd.h>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "PlanetTestRenderer.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace {
    using namespace hyprimate::workspace;
    constexpr int WIDTH = 96, HEIGHT = 64;
    bool equal_pixels(const std::vector<unsigned char> &actual,
        const std::vector<unsigned char> &expected,
        int width,
        const char *label,
        double progress) {
        if (actual == expected) {
            return true;
        }
        const int HEIGHT = static_cast<int>(actual.size() / 4 / static_cast<std::size_t>(width));
        int left = width;
        int right = 0;
        int top = HEIGHT;
        int bottom = 0;
        int count = 0;
        std::array<int, 4> maximum{};
        for (std::size_t pixel = 0; pixel < actual.size() / 4; ++pixel) {
            bool differs = false;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const int DELTA = std::abs(static_cast<int>(actual[(pixel * 4) + channel]) -
                                           expected[(pixel * 4) + channel]);
                maximum[channel] = std::max(maximum[channel], DELTA);
                differs = differs || DELTA != 0;
            }
            if (!differs) {
                continue;
            }
            const int COLUMN = static_cast<int>(pixel % static_cast<std::size_t>(width));
            const int ROW = static_cast<int>(pixel / static_cast<std::size_t>(width));
            left = std::min(left, COLUMN);
            right = std::max(right, COLUMN);
            top = std::min(top, ROW);
            bottom = std::max(bottom, ROW);
            if (count < 8) {
                const std::size_t OFFSET = pixel * 4;
                (void)std::fprintf(stderr,
                    "%s p=%.12g pixel(%d,%d) actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                    label,
                    progress,
                    COLUMN,
                    ROW,
                    actual[OFFSET],
                    actual[OFFSET + 1],
                    actual[OFFSET + 2],
                    actual[OFFSET + 3],
                    expected[OFFSET],
                    expected[OFFSET + 1],
                    expected[OFFSET + 2],
                    expected[OFFSET + 3]);
            }
            ++count;
        }
        (void)std::fprintf(stderr,
            "%s p=%.12g differing=%d bbox=%d,%d..%d,%d maxRGBA=%d,%d,%d,%d\n",
            label,
            progress,
            count,
            left,
            top,
            right,
            bottom,
            maximum[0],
            maximum[1],
            maximum[2],
            maximum[3]);
        return false;
    }
    GLuint texture(int width, int height, const unsigned char *pixels) {
        GLuint result = 0;
        glGenTextures(1, &result);
        glBindTexture(GL_TEXTURE_2D, result);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        return result;
    }
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
    void check_native_floating(PlanetTestRenderer &renderer) {
        constexpr int TEST_WIDTH = 96;
        constexpr int TEST_HEIGHT = 64;
        constexpr int CROP_W = 24;
        constexpr int CROP_H = 16;
        using Pixels = std::vector<unsigned char>;
        Pixels wall(static_cast<std::size_t>(TEST_WIDTH * TEST_HEIGHT * 4), 24);
        for (int row = 0; row < TEST_HEIGHT; ++row) {
            for (int column = 0; column < TEST_WIDTH; ++column) {
                const auto OFFSET = static_cast<std::size_t>((row * TEST_WIDTH) + column) * 4;
                wall[OFFSET] = static_cast<unsigned char>(24 + (column / 4));
                wall[OFFSET + 1] = static_cast<unsigned char>(24 + (row / 2));
                wall[OFFSET + 3] = 255;
            }
        }
        Pixels source = wall;
        Pixels destination = wall;
        Pixels crop_a(static_cast<std::size_t>(CROP_W * CROP_H * 4));
        Pixels crop_b = crop_a;
        for (int row = 0; row < CROP_H; ++row) {
            for (int column = 0; column < CROP_W; ++column) {
                const std::array<unsigned char, 4> SOURCE_COLOR{
                    210, static_cast<unsigned char>(row * 12), 35, 255};
                const std::array<unsigned char, 4> DESTINATION_COLOR{
                    30, static_cast<unsigned char>(row * 12), 220, 255};
                const auto COPY =
                    [&](Pixels &target, int target_width, int column, int row, const auto &color) {
                        const auto OFFSET = ((static_cast<std::size_t>(row) *
                                                 static_cast<std::size_t>(target_width)) +
                                                static_cast<std::size_t>(column)) *
                                            4;
                        std::ranges::copy(color, std::span{target}.subspan(OFFSET).begin());
                    };
                COPY(crop_a, CROP_W, column, row, SOURCE_COLOR);
                COPY(crop_b, CROP_W, column, row, DESTINATION_COLOR);
                COPY(source, TEST_WIDTH, 12 + column, 8 + row, SOURCE_COLOR);
                COPY(destination, TEST_WIDTH, 60 + column, 40 + row, DESTINATION_COLOR);
            }
        }
        const GLuint WALL = texture(TEST_WIDTH, TEST_HEIGHT, wall.data());
        const GLuint SRC = texture(TEST_WIDTH, TEST_HEIGHT, source.data());
        const GLuint DST = texture(TEST_WIDTH, TEST_HEIGHT, destination.data());
        const GLuint CROP_A = texture(CROP_W, CROP_H, crop_a.data());
        const GLuint CROP_B = texture(CROP_W, CROP_H, crop_b.data());
        const GLuint OUT = texture(TEST_WIDTH, TEST_HEIGHT, nullptr);
        std::array<PlanetFloatingTextures, 1> source_floating{{{.layer = {.image = CROP_A,
                                                                    .backdrop = 0},
            .rect = {12.F / TEST_WIDTH, 8.F / TEST_HEIGHT, 24.F / TEST_WIDTH, 16.F / TEST_HEIGHT},
            .cropped = true}}};
        std::array<PlanetFloatingTextures, 1> destination_floating{{{.layer = {.image = CROP_B,
                                                                         .backdrop = 0},
            .rect = {60.F / TEST_WIDTH, 40.F / TEST_HEIGHT, 24.F / TEST_WIDTH, 16.F / TEST_HEIGHT},
            .cropped = true}}};
        PlanetFrameTextures textures{};
        textures.source.composite = SRC;
        textures.destination.composite = DST;
        textures.source.wallpaper = textures.destination.wallpaper = WALL;
        textures.source.floating = source_floating;
        textures.destination.floating = destination_floating;
        textures.output = OUT;
        textures.width = TEST_WIDTH;
        textures.height = TEST_HEIGHT;
        textures.top_first = true;
        PlanetRenderSettings settings;
        settings.window_mode = 1;
        settings.overlay_mode = 0;
        settings.depth_enabled = settings.lighting = settings.clouds = false;
        settings.atmosphere_intensity = settings.elevation = 0;
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 2, .numbered = 2, .logical_order = 2});
        Pixels actual(source.size());
        for (const double PROGRESS : {0.0, 1.0e-5, 1.0 - 1.0e-5, 1.0}) {
            CHECK(renderer.draw_raw(
                textures, planet_hop::evaluate(JOURNEY, PROGRESS, 1.5), PROGRESS, settings));
            glReadPixels(0, 0, TEST_WIDTH, TEST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            CHECK(equal_pixels(actual,
                PROGRESS < 0.5 ? source : destination,
                TEST_WIDTH,
                "native-floating",
                PROGRESS));
        }
        Pixels old_wall = wall;
        for (std::size_t i = 0; i < old_wall.size(); ++i) {
            if (i % 4 != 3) {
                old_wall[i] += 2;
            }
        }
        const GLuint OLD_WALL = texture(TEST_WIDTH, TEST_HEIGHT, old_wall.data());
        constexpr std::array<unsigned char, 4> OLD_COLOR{18, 84, 193, 255};
        constexpr std::array<unsigned char, 16> OLD_CROP_PIXELS{
            18, 84, 193, 255, 18, 84, 193, 255, 18, 84, 193, 255, 18, 84, 193, 255};
        const GLuint OLD_CROP = texture(2, 2, OLD_CROP_PIXELS.data());
        textures.source.previous_wallpaper = OLD_WALL;
        textures.source.live_mix = 0.5F;
        source_floating[0].layer.previous_image = OLD_CROP;
        source_floating[0].layer.live_mix = 0.5F;
        for (const int SHIFT : {0, 4}) {
            source_floating[0].rect[0] = static_cast<float>(12 + SHIFT) / TEST_WIDTH;
            source_floating[0].rect[1] = static_cast<float>(8 + SHIFT) / TEST_HEIGHT;
            Pixels expected = wall;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (i % 4 != 3) {
                    ++expected[i];
                }
            }
            for (int row = 0; row < CROP_H; ++row) {
                for (int column = 0; column < CROP_W; ++column) {
                    const auto TARGET = static_cast<std::size_t>(((8 + SHIFT + row) * TEST_WIDTH) +
                                                                 12 + SHIFT + column) *
                                        4;
                    const auto INPUT = static_cast<std::size_t>((row * CROP_W) + column) * 4;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        expected[TARGET + channel] = static_cast<unsigned char>(
                            (crop_a[INPUT + channel] + OLD_COLOR[channel]) / 2);
                    }
                }
            }
            CHECK(renderer.draw_raw(
                textures, planet_hop::evaluate(JOURNEY, 1.0e-5, 1.5), 1.0e-5, settings));
            glReadPixels(0, 0, TEST_WIDTH, TEST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            CHECK(equal_pixels(actual, expected, TEST_WIDTH, "moving-crop-history", SHIFT));
        }
        // Exact native boundaries always use current native composites, not a retained history.
        CHECK(renderer.draw_raw(textures, planet_hop::evaluate(JOURNEY, 0, 1.5), 0, settings));
        glReadPixels(0, 0, TEST_WIDTH, TEST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        CHECK(equal_pixels(actual, source, TEST_WIDTH, "history-native-boundary", 0));
        constexpr std::array<unsigned char, 4> OLD_A_COLOR{16, 32, 48, 255};
        constexpr std::array<unsigned char, 4> A_COLOR{48, 64, 80, 255};
        constexpr std::array<unsigned char, 4> OLD_B_COLOR{80, 96, 112, 255};
        constexpr std::array<unsigned char, 4> B_COLOR{112, 128, 144, 255};
        const GLuint OLD_A = texture(1, 1, OLD_A_COLOR.data());
        const GLuint LIVE_A = texture(1, 1, A_COLOR.data());
        const GLuint OLD_B = texture(1, 1, OLD_B_COLOR.data());
        const GLuint LIVE_B = texture(1, 1, B_COLOR.data());
        textures.source.top = {.image = LIVE_A,
            .backdrop = 0,
            .previous_image = OLD_A,
            .previous_backdrop = 0,
            .live_mix = 0.25F};
        textures.destination.top = {.image = LIVE_B,
            .backdrop = 0,
            .previous_image = OLD_B,
            .previous_backdrop = 0,
            .live_mix = 0.75F};
        settings.overlay_mode = 1;
        const auto EXPECT_HUD = [&](const std::array<unsigned char, 4> &color, const char *label) {
            CHECK(renderer.draw_raw(
                textures, planet_hop::evaluate(JOURNEY, 0.5, 1.5), 0.5, settings));
            glReadPixels(0, 0, TEST_WIDTH, TEST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            Pixels expected(actual.size());
            for (std::size_t offset = 0; offset < expected.size(); offset += 4) {
                std::ranges::copy(color, std::span{expected}.subspan(offset).begin());
            }
            CHECK(equal_pixels(actual, expected, TEST_WIDTH, label, 0.5));
        };
        EXPECT_HUD({64, 80, 96, 255}, "four-capture-HUD-history");
        textures.source.top.previous_image = textures.destination.top.previous_image = 0;
        textures.source.top.live_mix = textures.destination.top.live_mix =
            0;  // No prior capture must select live anyway.
        EXPECT_HUD({80, 96, 112, 255}, "HUD-no-previous");
        textures.source.top.previous_image = OUT;
        CHECK(!renderer.draw_raw(textures, planet_hop::evaluate(JOURNEY, 0.5, 1.5), 0.5, settings));
        GLint fragment_units = 0;
        GLint combined_units = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragment_units);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combined_units);
        CHECK(fragment_units >= 8 && combined_units >= 9);
        const std::array<GLuint, 12> TEXTURES{
            WALL, SRC, DST, CROP_A, CROP_B, OUT, OLD_WALL, OLD_CROP, OLD_A, LIVE_A, OLD_B, LIVE_B};
        glDeleteTextures(static_cast<GLsizei>(TEXTURES.size()), TEXTURES.data());
        CHECK(glGetError() == GL_NO_ERROR);
    }
    unsigned int uncovered_pixels(std::span<const unsigned char> interior) {
        unsigned int missing = 0;
        for (std::size_t index = 0; index < interior.size(); index += 4) {
            if (interior[index] < 200 || interior[index + 2] > 55) {
                ++missing;
            }
        }
        return missing;
    }

    void check_landing_coverage(PlanetTestRenderer &renderer) {
        constexpr std::array<unsigned char, 4> PAPER{0, 0, 255, 255};
        constexpr std::array<unsigned char, 4> INK{255, 0, 0, 255};
        const GLuint WALL = texture(1, 1, PAPER.data());
        const GLuint FOREGROUND = texture(1, 1, INK.data());
        const GLuint OUT = texture(WIDTH, HEIGHT, nullptr);
        PlanetFrameTextures textures{};
        textures.source.wallpaper = textures.destination.wallpaper = WALL;
        textures.source.composite = textures.destination.composite = FOREGROUND;
        textures.output = OUT;
        textures.width = WIDTH;
        textures.height = HEIGHT;
        textures.top_first = true;
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 2, .numbered = 2, .logical_order = 2});
        std::array<unsigned char, std::size_t{WIDTH} * HEIGHT> interior{};
        const std::array<PlanetFloatingTextures, 1> FLOATING{
            {{.layer = {.image = FOREGROUND, .backdrop = 0}, .rect = {0.1F, 0.1F, 0.8F, 0.8F}}}};
        PlanetRenderSettings settings;
        settings.overlay_mode = 0;
        for (int mode = 0; mode < 4; ++mode) {
            settings.window_mode = mode == 3 ? 0 : mode;
            for (auto *capture : {&textures.source, &textures.destination}) {
                capture->windows = mode == 0 || mode == 2
                                       ? PlanetLayerTextures{.image = FOREGROUND, .backdrop = 0}
                                       : PlanetLayerTextures{};
                capture->floating =
                    mode == 1 ? std::span{FLOATING} : std::span<const PlanetFloatingTextures>{};
                capture->bottom = mode == 3
                                      ? PlanetLayerTextures{.image = FOREGROUND, .backdrop = 0}
                                      : PlanetLayerTextures{};
            }
            // A nearly opaque foreground (including a bottom shell on an empty workspace)
            // must cover every interior pixel, not just at progress 0/1 on the composite path.
            for (const double REMAINING : {.025, .018, .013, .010, .007, .005, .003, .001}) {
                for (const double PROGRESS : {REMAINING, 1 - REMAINING}) {
                    CHECK(renderer.draw_raw(textures,
                        planet_hop::evaluate(
                            JOURNEY, PROGRESS, static_cast<double>(WIDTH) / HEIGHT),
                        PROGRESS,
                        settings));
                    glReadPixels(WIDTH / 4,
                        HEIGHT / 4,
                        WIDTH / 2,
                        HEIGHT / 2,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        interior.data());
                    const unsigned int MISSING = uncovered_pixels(interior);
                    if (MISSING != 0) {
                        (void)std::fprintf(stderr,
                            "landing mode=%d p=%.3f wallpaper leaks through %u foreground pixels\n",
                            mode,
                            PROGRESS,
                            MISSING);
                    }
                    CHECK(MISSING == 0);
                }
            }
        }
        const std::array<GLuint, 3> IDS{WALL, FOREGROUND, OUT};
        glDeleteTextures(static_cast<GLsizei>(IDS.size()), IDS.data());
    }
    template <typename Read>
    void check_inventory_filtering(const Read &read,
        std::array<PlanetNeighborTextures, 1> &neighbors,
        const std::vector<unsigned char> &sky,
        GLuint preview) {
        // A one-pixel checker mapped onto a small inactive sphere must sample its
        // cached mip chain, converging to the same appearance as its true mean.
        constexpr int CHECKER_SIZE = 256;
        std::vector<unsigned char> checker(std::size_t{CHECKER_SIZE} * CHECKER_SIZE * 4);
        for (int row = 0; row < CHECKER_SIZE; ++row) {
            for (int column = 0; column < CHECKER_SIZE; ++column) {
                const auto OFFSET = static_cast<std::size_t>((row * CHECKER_SIZE) + column) * 4;
                const auto VALUE = static_cast<unsigned char>((column + row) % 2 != 0 ? 255 : 0);
                checker[OFFSET] = checker[OFFSET + 1] = checker[OFFSET + 2] = VALUE;
                checker[OFFSET + 3] = 255;
            }
        }
        const GLuint CHECKER = texture(CHECKER_SIZE, CHECKER_SIZE, checker.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        constexpr std::array<unsigned char, 4> MEAN{128, 128, 128, 255};
        const GLuint AVERAGE = texture(1, 1, MEAN.data());
        neighbors[0].image = AVERAGE;
        const auto FILTERED_REFERENCE = read(neighbors);
        neighbors[0].image = CHECKER;
        neighbors[0].image_mipped = true;
        const auto FILTERED = read(neighbors);
        CHECK(FILTERED != sky);
        for (std::size_t i = 0; i < FILTERED.size(); ++i) {
            CHECK(std::abs(static_cast<int>(FILTERED[i]) -
                           static_cast<int>(FILTERED_REFERENCE[i])) <= 2);
        }
        neighbors[0].image = preview;
        neighbors[0].image_mipped = false;
        neighbors[0].previous_image = CHECKER;
        neighbors[0].previous_mipped = true;
        neighbors[0].live_mix = 0;
        CHECK(read(neighbors) == FILTERED);
        // Both native primary and history units must be back on ordinary linear
        // sampling before their un-mipped full-size capture textures are drawn.
        GLint sampler = 0;
        GLint filter = 0;
        for (const GLenum UNIT : {GL_TEXTURE0, GL_TEXTURE3}) {
            glActiveTexture(UNIT);
            glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
            CHECK(sampler != 0);
            glGetSamplerParameteriv(static_cast<GLuint>(sampler), GL_TEXTURE_MIN_FILTER, &filter);
            CHECK(filter == GL_LINEAR);
        }
        neighbors[0].previous_image = 0;
        neighbors[0].previous_mipped = false;
        neighbors[0].live_mix = 1;
        const std::array<GLuint, 2> MIP_TEXTURES{CHECKER, AVERAGE};
        glDeleteTextures(static_cast<GLsizei>(MIP_TEXTURES.size()), MIP_TEXTURES.data());
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void check_native_hud(PlanetTestRenderer &renderer,
        PlanetFrameTextures &textures,
        PlanetRenderSettings &settings,
        const std::array<GLuint, 3> &images) {
        const auto [WALL, PREVIEW, WINDOWS] = images;
        planet_hop::Frame frame;
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 5, .numbered = 5, .logical_order = 5});
        settings.overlay_mode = 1;
        textures.source.bottom = textures.source.top = {.image = PREVIEW};
        textures.destination.bottom = textures.destination.top = {.image = PREVIEW};
        textures.source.windows = textures.destination.windows = {.image = WINDOWS};
        const auto NATIVE = [&](double progress) {
            frame = planet_hop::evaluate(JOURNEY, progress, static_cast<double>(WIDTH) / HEIGHT);
            CHECK(renderer.draw_raw(textures, frame, progress, settings));
            std::vector<unsigned char> pixels(std::size_t{WIDTH} * HEIGHT * 4);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const std::array<PlanetFloatingTextures, 1> FLOATING{
            {{.layer = {.image = WALL}, .rect = {.1F, .15F, .3F, .25F}}}};
        textures.source.floating = textures.destination.floating = FLOATING;
        for (const bool TOP_FIRST : {false, true}) {
            textures.top_first = TOP_FIRST;
            for (const double ENDPOINT : {0., 1.}) {
                textures.native_hud = false;
                const auto BAKED = NATIVE(ENDPOINT);
                CHECK(BAKED[1] == 255);  // Legacy mode still uses its original composite.
                textures.native_hud = true;
                const auto CLEAN = NATIVE(ENDPOINT);
                CHECK(CLEAN[0] == 0 && CLEAN[1] == 0 && CLEAN[2] == 255);
                const auto PATCH =
                    static_cast<std::size_t>(
                        ((TOP_FIRST ? HEIGHT / 4 : 3 * HEIGHT / 4) * WIDTH) + (WIDTH / 5)) *
                    4;
                CHECK(CLEAN[PATCH] == 255 && CLEAN[PATCH + 1] == 0 && CLEAN[PATCH + 2] == 0);
            }
        }
        textures.native_hud = true;
        const auto HUD_EXCLUDED = NATIVE(.35);
        textures.source.bottom = textures.source.top = {};
        textures.destination.bottom = textures.destination.top = {};
        CHECK(NATIVE(.35) == HUD_EXCLUDED);
        textures.source.bottom = textures.source.top = {.image = PREVIEW};
        textures.destination.bottom = textures.destination.top = {.image = PREVIEW};
        textures.native_hud = false;
        CHECK(NATIVE(.35) != HUD_EXCLUDED);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void check_inventory_and_native_hud(PlanetTestRenderer &renderer) {
        CHECK(renderer.prepare_depth(""));
        constexpr std::array<unsigned char, 4> RED{255, 0, 0, 255};
        constexpr std::array<unsigned char, 4> GREEN{0, 255, 0, 255};
        constexpr std::array<unsigned char, 4> BLUE{0, 0, 255, 255};
        const GLuint WALL = texture(1, 1, RED.data());
        const GLuint PREVIEW = texture(1, 1, GREEN.data());
        const GLuint WINDOWS = texture(1, 1, BLUE.data());
        const GLuint OUT = texture(WIDTH, HEIGHT, nullptr);
        PlanetFrameTextures textures{};
        textures.source.wallpaper = textures.destination.wallpaper = WALL;
        textures.source.composite = textures.destination.composite = PREVIEW;
        textures.output = OUT;
        textures.width = WIDTH;
        textures.height = HEIGHT;
        textures.source_identity = 1;
        textures.destination_identity = 5;
        PlanetRenderSettings settings;
        settings.clouds = settings.lighting = settings.depth_enabled = false;
        settings.star_density = settings.atmosphere_intensity = 0;
        planet_hop::Frame frame;
        frame.source_center = {.x = -20};
        frame.destination_center = {.x = 20};
        frame.source_rotation = frame.destination_rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        frame.view_projection = {.15F, 0, 0, 0, 0, .225F, 0, 0, 0, 0, -.05F, 0, 0, 0, 0, 1};
        frame.inverse_view_projection = {
            1 / .15F, 0, 0, 0, 0, 1 / .225F, 0, 0, 0, 0, -20, 0, 0, 0, 0, 1};
        frame.camera_position = {.z = 10};
        frame.source_curvature = frame.destination_curvature = frame.endpoint_envelope = 1;
        frame.aspect = static_cast<double>(WIDTH) / HEIGHT;
        std::array<PlanetNeighborTextures, 1> neighbors{
            {{.endpoint = {.identity = 3, .placement = 3},
                .center = {.x = 0, .y = 0, .z = 0},
                .image = PREVIEW,
                .shared_wallpaper = false}}};
        const auto READ = [&](std::span<const PlanetNeighborTextures> input = {},
                              double progress = .5) {
            CHECK(renderer.draw_raw(textures, frame, progress, settings, input));
            std::vector<unsigned char> pixels(std::size_t{WIDTH} * HEIGHT * 4);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const auto SKY = READ();
        const auto POPULATED = READ(neighbors);
        CHECK(POPULATED != SKY);
        constexpr std::array<unsigned char, 4> CAPTURED_GREEN{0, 255, 0, 0};
        const GLuint ALPHA_PREVIEW = texture(1, 1, CAPTURED_GREEN.data());
        neighbors[0].image = ALPHA_PREVIEW;
        CHECK(READ(neighbors) ==
              POPULATED);  // Accumulated RGB is opaque despite capture coverage alpha.
        neighbors[0].image = PREVIEW;
        neighbors[0].endpoint.identity = 4;
        CHECK(READ(neighbors) ==
              POPULATED);  // Native object ID changes do not change durable placement identity.
        neighbors[0].endpoint.placement = 4;
        neighbors[0].shared_wallpaper = true;
        const auto SHARED = READ(neighbors);
        CHECK(SHARED != POPULATED);
        CHECK(READ(neighbors) == SHARED);
        neighbors[0].shared_wallpaper = false;
        neighbors[0].endpoint.identity = 3;
        neighbors[0].endpoint.placement = 3;
        settings.solar_background_scale = 2;
        const auto MOVED = READ(neighbors, .2);
        CHECK(READ(neighbors, .7) != MOVED);
        CHECK(READ(neighbors, .2) == MOVED);  // Reversal retraces the same orientation exactly.
        settings.rotation_enabled = false;
        CHECK(READ(neighbors, .2) == READ(neighbors, .7));
        settings.rotation_enabled = true;
        settings.rotation_limit = 0;
        CHECK(READ(neighbors, .2) == READ(neighbors, .7));
        settings.rotation_limit = .35F;
        settings.solar_background_scale = .75F;
        unsigned int green = 0;
        for (std::size_t i = 0; i < POPULATED.size(); i += 4) {
            green += static_cast<unsigned int>(POPULATED[i + 1] > 200 && POPULATED[i] < 20);
        }
        CHECK(green > 8);
        // Geometry is a bounded globe, not a full-screen preview plate.
        CHECK(POPULATED.front() == SKY.front() && POPULATED[1] == SKY[1]);
        for (int quality = 0; quality < 3; ++quality) {
            settings.quality = quality;
            CHECK(READ(neighbors) != SKY);
        }
        check_inventory_filtering(READ, neighbors, SKY, PREVIEW);
        for (const auto IDENTITY : {textures.source_identity, textures.destination_identity}) {
            neighbors[0].endpoint.identity = IDENTITY;
            CHECK(READ(neighbors) == SKY);
        }
        neighbors[0].endpoint.identity = 3;
        for (const auto CENTER : {planet_hop::Vec3{.x = 100, .y = 0, .z = 0},
                 planet_hop::Vec3{.x = 0, .y = 0, .z = 100},
                 planet_hop::Vec3{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0, .z = 0}}) {
            neighbors[0].center = CENTER;
            CHECK(READ(neighbors) == SKY);
        }
        neighbors[0].center = {.x = 0, .y = 0, .z = 0};
        settings.solar_background_scale = .001F;  // Subpixel planets have no draw footprint.
        CHECK(READ(neighbors) == SKY);
        settings.solar_background_scale = .75F;
        neighbors[0].image = 0;
        GLuint unpack_buffer = 0;
        glGenBuffers(1, &unpack_buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 8, nullptr, GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
        const auto UNKNOWN = READ(neighbors);
        CHECK(UNKNOWN != SKY && UNKNOWN != POPULATED);
        GLint unpack = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
        CHECK(unpack == 8);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack);
        CHECK(unpack == 7);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack);
        CHECK(unpack == 2);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack);
        CHECK(unpack == 3);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
        CHECK(std::cmp_equal(unpack, unpack_buffer));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glDeleteBuffers(1, &unpack_buffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        // A late actual capture crossfades from its own old preview or from
        // genuinely unknown neutral material, never another workspace's paper.
        neighbors[0].image = WALL;
        const auto OLD_PREVIEW = READ(neighbors);
        neighbors[0].image = PREVIEW;
        neighbors[0].previous_image = WALL;
        neighbors[0].live_mix = 0;
        CHECK(READ(neighbors) == OLD_PREVIEW);
        neighbors[0].live_mix = 1;
        CHECK(READ(neighbors) == POPULATED);
        neighbors[0].live_mix = .5F;
        const auto BLENDED = READ(neighbors);
        for (std::size_t i = 0; i < BLENDED.size(); ++i) {
            CHECK(std::abs(static_cast<int>(BLENDED[i]) -
                           ((static_cast<int>(OLD_PREVIEW[i]) + static_cast<int>(POPULATED[i])) /
                               2)) <= 2);
        }
        neighbors[0].previous_image = 0;
        neighbors[0].live_mix = 0;
        CHECK(READ(neighbors) == UNKNOWN);
        neighbors[0].live_mix = .5F;
        CHECK(READ(neighbors) != UNKNOWN && READ(neighbors) != POPULATED);
        neighbors[0].live_mix = 1;
        neighbors[0].previous_image = OUT;
        CHECK(!renderer.draw_raw(textures, frame, .5, settings, neighbors));
        neighbors[0].previous_image = 0;
        neighbors[0].image = OUT;
        CHECK(!renderer.draw_raw(textures, frame, .5, settings, neighbors));
        neighbors[0].image = PREVIEW;
        // Primary opaque geometry occludes the smaller distant inventory world;
        // a genuinely nearer inventory globe must instead cover the primary.
        frame.source_center = {};
        neighbors[0].center = {.x = 0, .y = 0, .z = -2};
        const auto PRIMARY = READ();
        CHECK(READ(neighbors) == PRIMARY);
        neighbors[0].center.z = 2;
        CHECK(READ(neighbors) != PRIMARY);
        CHECK(glGetError() == GL_NO_ERROR);

        check_native_hud(renderer, textures, settings, {WALL, PREVIEW, WINDOWS});
        const std::array<GLuint, 5> IDS{WALL, PREVIEW, WINDOWS, OUT, ALPHA_PREVIEW};
        glDeleteTextures(static_cast<GLsizei>(IDS.size()), IDS.data());
    }
    template <typename Render>
    void profile_frames(const Render &render,
        PlanetFrameTextures &textures,
        PlanetRenderSettings &settings,
        std::vector<unsigned char> &source,
        int frames) {
        const int WIDTH = textures.width;
        const int HEIGHT = textures.height;
        // Exercise the same layer reconstruction/blend as populated desktops. No
        // Wayland surface rendering, screencopy or readback is included in these times.
        for (int row = 0; row < HEIGHT; ++row) {
            for (int column = 0; column < WIDTH; ++column) {
                const auto OFFSET = static_cast<std::size_t>((row * WIDTH) + column) * 4;
                source[OFFSET + 3] = (column > WIDTH / 3 && column < 2 * WIDTH / 3 &&
                                         row > HEIGHT / 3 && row < 2 * HEIGHT / 3)
                                         ? 208
                                         : 0;
            }
        }
        const GLuint PLANE = texture(WIDTH, HEIGHT, source.data());
        for (auto *capture : {&textures.source, &textures.destination}) {
            capture->bottom = capture->windows =
                capture->top = {.image = PLANE, .backdrop = capture->wallpaper};
        }
        bool can_time = false;
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint extension = 0; extension < extensions; ++extension) {
            const std::basic_string_view<GLubyte> NAME(glGetStringi(GL_EXTENSIONS, extension));
            can_time |= std::ranges::equal(NAME, std::string_view{"GL_EXT_disjoint_timer_query"});
        }
        GLuint query = 0;
        if (can_time) {
            glGenQueries(1, &query);
        }
        const auto MEASURE = [&](const char *effects) {
            for (const double POSITION : {.15, .3, .5, .75}) {
                CHECK(render(POSITION));
            }
            glFinish();
            GLint disjoint = 0;
            if (query != 0) {
                glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
                glBeginQuery(GL_TIME_ELAPSED_EXT, query);
            }
            const auto START = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i) {
                CHECK(render(.05 + (.90 * static_cast<double>(i) / std::max(frames - 1, 1))));
            }
            if (query != 0) {
                glEndQuery(GL_TIME_ELAPSED_EXT);
            }
            glFinish();
            const double WALL =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - START)
                    .count() /
                frames;
            (void)std::printf("planet-hop %dx%d quality=%d effects=%s complete-wall-ms/frame=%.3f",
                WIDTH,
                HEIGHT,
                settings.quality,
                effects,
                WALL);
            if (query != 0) {
                GLuint64 nanoseconds = 0;
                glGetQueryObjectui64vEXT(query, GL_QUERY_RESULT, &nanoseconds);
                glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
                if (disjoint == 0) {
                    (void)std::printf(
                        " gpu-ms/frame=%.3f", static_cast<double>(nanoseconds) / (1e6 * frames));
                }
            }
            (void)std::printf(" frames=%d\n", frames);
        };
        (void)std::printf("%s / %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
        const auto ALL = settings;
        for (int quality = 0; quality < 3; ++quality) {
            settings.quality = quality;
            MEASURE("all");
        }
        auto none = ALL;
        none.quality = 2;
        none.star_density = none.atmosphere_intensity = 0;
        none.lighting = none.clouds = none.depth_enabled = false;
        settings = none;
        MEASURE("none");
        settings.star_density = ALL.star_density;
        MEASURE("stars");
        settings = none;
        settings.atmosphere_intensity = ALL.atmosphere_intensity;
        MEASURE("atmosphere");
        settings = none;
        settings.lighting = true;
        MEASURE("lighting");
        settings = none;
        settings.clouds = true;
        MEASURE("clouds");
        settings = none;
        settings.depth_enabled = true;
        MEASURE("relief");
        if (query != 0) {
            glDeleteQueries(1, &query);
        }
        glDeleteTextures(1, &PLANE);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void run(bool profile, int width, int height, int frames) {
        PlanetTestRenderer renderer;
        check_depth(renderer);
        CHECK(renderer.initialize_raw());
        check_native_floating(renderer);
        if (!profile) {
            check_landing_coverage(renderer);
            check_inventory_and_native_hud(renderer);
        }
        std::vector<unsigned char> source(static_cast<std::size_t>(width * height) * 4);
        auto destination = source;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const auto OFFSET = static_cast<std::size_t>((row * width) + column) * 4;
                source[OFFSET] = static_cast<unsigned char>(column * 255 / width);
                source[OFFSET + 1] = static_cast<unsigned char>(row * 255 / height);
                source[OFFSET + 2] = 48;
                source[OFFSET + 3] = 255;
                destination[OFFSET] = 24;
                destination[OFFSET + 1] = static_cast<unsigned char>(column * 255 / width);
                destination[OFFSET + 2] = static_cast<unsigned char>(row * 255 / height);
                destination[OFFSET + 3] = 255;
            }
        }
        const GLuint SRC = texture(width, height, source.data());
        const GLuint DST = texture(width, height, destination.data());
        const GLuint OUT = texture(width, height, nullptr);
        PlanetFrameTextures textures{};
        textures.source.wallpaper = textures.source.composite = SRC;
        textures.destination.wallpaper = textures.destination.composite = DST;
        textures.output = OUT;
        textures.width = width;
        textures.height = height;
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 5, .numbered = 5, .logical_order = 2});
        PlanetRenderSettings settings;
        const auto RENDER = [&](double progress) {
            return renderer.draw_raw(textures,
                planet_hop::evaluate(JOURNEY, progress, static_cast<double>(width) / height),
                progress,
                settings);
        };
        std::vector<unsigned char> actual(source.size());
        for (const double ENDPOINT : {0.0, 1.0}) {
            CHECK(RENDER(ENDPOINT));
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            CHECK(equal_pixels(
                actual, ENDPOINT == 0 ? source : destination, width, "raw-boundary", ENDPOINT));
        }
        // The actual mesh path, not the exact-boundary composite shortcut, must meet native pixels.
        for (const double NEAR_ENDPOINT : {1.0e-9, 1.0 - 1.0e-9}) {
            CHECK(RENDER(NEAR_ENDPOINT));
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            CHECK(equal_pixels(actual,
                NEAR_ENDPOINT < 0.5 ? source : destination,
                width,
                "raw-near-boundary",
                NEAR_ENDPOINT));
        }
        if (!profile) {
            const auto RENDERED = [&] -> const auto & {
                CHECK(RENDER(.25));
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                return actual;
            };
            // Each switch must make a visible difference with every other effect
            // left on. Re-enabling it must not perturb any persistent scene state.
            const auto ENABLED = settings;
            (void)RENDERED();
            const auto ALL = actual;
            const auto CHECK_SWITCH = [&](auto member, auto off) {
                settings.*member = off;
                CHECK(RENDERED() != ALL);
                settings = ENABLED;
                CHECK(RENDERED() == ALL);
            };
            CHECK_SWITCH(&PlanetRenderSettings::star_density, 0.F);
            CHECK_SWITCH(&PlanetRenderSettings::atmosphere_intensity, 0.F);
            CHECK_SWITCH(&PlanetRenderSettings::lighting, false);
            CHECK_SWITCH(&PlanetRenderSettings::clouds, false);
            CHECK_SWITCH(&PlanetRenderSettings::depth_enabled, false);
        }
        for (int quality = 0; quality < 3; ++quality) {
            settings.quality = quality;
            for (const double PROGRESS : {.01, .1, .3, .5, .7, .9, .99}) {
                CHECK(RENDER(PROGRESS));
            }
            CHECK(glGetError() == GL_NO_ERROR);
        }
        // Uploading a changed depth image must not consume the compositor's unpack state.
        GLint alignment = 0;
        GLint row_length = 0;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
        check_depth(renderer);
        CHECK(RENDER(.4));
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        CHECK(alignment == 8 && row_length == 7);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        CHECK(!RENDER(std::numeric_limits<double>::quiet_NaN()));
        textures.output = SRC;
        CHECK(!RENDER(.5));
        textures.output = OUT;
        const GLuint ALTERNATE = texture(width, height / 2, nullptr);
        for (int repeat = 0; repeat < 4; ++repeat) {
            textures.output = ALTERNATE;
            textures.height = height / 2;
            CHECK(RENDER(.4));
            textures.output = OUT;
            textures.height = height;
            CHECK(RENDER(.6));
        }
        glDeleteTextures(1, &ALTERNATE);
        CHECK(glGetError() == GL_NO_ERROR);
        if (profile) {
            CHECK(renderer.prepare_depth(""));  // Profile the default procedural relief.
            profile_frames(RENDER, textures, settings, source, frames);
        }
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
        CHECK(glIsTexture(SRC) == GL_TRUE && glIsTexture(DST) == GL_TRUE &&
              glIsTexture(OUT) == GL_TRUE);
        CHECK(renderer.initialize_raw());
        CHECK(RENDER(1.0));
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        CHECK(actual == destination);
        renderer.release_raw();
        const std::array<GLuint, 3> TEXTURES{SRC, DST, OUT};
        glDeleteTextures(static_cast<GLsizei>(TEXTURES.size()), TEXTURES.data());
    }
    struct ArgumentRange {
        long minimum;
        long maximum;
    };

    int profile_argument(const char *text, ArgumentRange range) {
        char *end = nullptr;
        errno = 0;
        const long VALUE = std::strtol(text, &end, 10);
        if (end == text) {
            return static_cast<int>(range.minimum);
        }
        if (errno == ERANGE) {
            return static_cast<int>(VALUE < 0 ? range.minimum : range.maximum);
        }
        return static_cast<int>(std::clamp(VALUE, range.minimum, range.maximum));
    }
}  // namespace
int main(int argc, char *const *argv) {
    const bool PROFILE = argc > 1 && std::string_view(argv[1]) == "--profile";
    const int WIDTH_ARG =
        PROFILE && argc > 2 ? profile_argument(argv[2], {.minimum = 16, .maximum = 7680}) : WIDTH;
    const int HEIGHT_ARG =
        PROFILE && argc > 3 ? profile_argument(argv[3], {.minimum = 16, .maximum = 4320}) : HEIGHT;
    const int FRAMES =
        PROFILE && argc > 4 ? profile_argument(argv[4], {.minimum = 1, .maximum = 10000}) : 120;
    const gl_harness::CContext CONTEXT(WIDTH_ARG, HEIGHT_ARG);
    if (!CONTEXT.valid()) {
        return 77;
    }
    run(PROFILE, WIDTH_ARG, HEIGHT_ARG, FRAMES);
    return g_failures == 0 ? 0 : 1;
}
