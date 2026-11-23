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
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>

// Extension prototypes require core GLES types and calling conventions first.
#include <GLES2/gl2ext.h>
#include <png.h>
#include <unistd.h>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "PlanetTestRenderer.hpp"
#include "workspace/WorkspaceTransition.hpp"
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

    void dump_frame(std::string_view label,
        double progress,
        int width,
        int height,
        const std::vector<unsigned char> &pixels) {
        const char *directory = std::getenv("HYPRIMATE_PLANET_FRAME_DIR");
        if (directory == nullptr || *directory == '\0') {
            return;
        }
        std::filesystem::create_directories(directory);
        const auto PATH = std::filesystem::path(directory) /
                          (std::string(label) + "-" + std::to_string(progress) + ".png");
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = static_cast<png_uint_32>(width);
        image.height = static_cast<png_uint_32>(height);
        image.format = PNG_FORMAT_RGBA;
        CHECK(png_image_write_to_file(&image, PATH.c_str(), 0, pixels.data(), 0, nullptr) != 0);
    }

    // Independent orthographic oracle for the production square-to-disk chart.
    // Invert its monotone XY map rather than assuming an expanding ellipsoid.
    std::array<double, 2> chart_xy(double x, double y, double bend, double aspect) {
        const double CX = .7 * std::min(1., aspect);
        const double CY = .7 * std::min(1., 1 / aspect);
        const double SX = x * (CX + (1 - CX) * x * x);
        const double SY = y * (CY + (1 - CY) * y * y);
        return {(1 - bend) * aspect * x + bend * SX * std::sqrt(std::max(0., 1 - SY * SY / 2)),
            (1 - bend) * y + bend * SY * std::sqrt(std::max(0., 1 - SX * SX / 2))};
    }
    std::array<double, 3> inverse_chart(
        double world_x, double world_y, double bend, double aspect) {
        double x = std::clamp(world_x / ((1 - bend) * aspect + bend), -1., 1.);
        double y = std::clamp(world_y, -1., 1.);
        constexpr double STEP = 1.0e-5;
        for (int iteration = 0; iteration < 20; ++iteration) {
            const auto P = chart_xy(x, y, bend, aspect);
            const double EX = world_x - P[0], EY = world_y - P[1];
            if (std::hypot(EX, EY) < 1.0e-10) break;
            const auto EAST = chart_xy(x + STEP, y, bend, aspect);
            const auto WEST = chart_xy(x - STEP, y, bend, aspect);
            const auto NORTH = chart_xy(x, y + STEP, bend, aspect);
            const auto SOUTH = chart_xy(x, y - STEP, bend, aspect);
            const double XX = (EAST[0] - WEST[0]) / (2 * STEP);
            const double YX = (EAST[1] - WEST[1]) / (2 * STEP);
            const double XY = (NORTH[0] - SOUTH[0]) / (2 * STEP);
            const double YY = (NORTH[1] - SOUTH[1]) / (2 * STEP);
            const double DET = XX * YY - XY * YX;
            // The disk's four corners have a singular boundary Jacobian.
            // Retain a finite residual there so exterior pixels are still
            // checked as sky rather than disappearing through a NaN oracle.
            if (std::abs(DET) < 1.0e-10) break;
            x = std::clamp(x + (YY * EX - XY * EY) / DET, -1., 1.);
            y = std::clamp(y + (XX * EY - YX * EX) / DET, -1., 1.);
        }
        const auto P = chart_xy(x, y, bend, aspect);
        return {x, y, std::hypot(world_x - P[0], world_y - P[1])};
    }

    void check_continuous_surface(PlanetTestRenderer &renderer) {
        CHECK(renderer.prepare_depth(""));
        std::vector<unsigned char> pattern(std::size_t{WIDTH} * HEIGHT * 4);
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const auto OFFSET = static_cast<std::size_t>(y * WIDTH + x) * 4;
                pattern[OFFSET] = static_cast<unsigned char>(64 + 128 * x / WIDTH);
                pattern[OFFSET + 1] = static_cast<unsigned char>(64 + 128 * y / HEIGHT);
                pattern[OFFSET + 2] = 80;
                pattern[OFFSET + 3] = 255;
            }
        }
        const GLuint WALL = texture(WIDTH, HEIGHT, pattern.data());
        const GLuint OUT = texture(WIDTH, HEIGHT, nullptr);
        PlanetFrameTextures textures{};
        textures.source.wallpaper = textures.source.composite = WALL;
        textures.destination.wallpaper = textures.destination.composite = WALL;
        textures.width = WIDTH;
        textures.height = HEIGHT;
        textures.output = OUT;
        textures.top_first = true;
        PlanetRenderSettings settings;
        settings.depth_enabled = settings.lighting = settings.clouds = false;
        settings.star_density = settings.atmosphere_intensity = 0;
        planet_hop::Frame frame;
        frame.aspect = 1.5;
        frame.radius = 1;
        frame.coverage = .7;
        frame.source_center = frame.destination_center = {.x = 400};
        frame.source_rotation = frame.destination_rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        frame.source_curvature = frame.destination_curvature = frame.endpoint_envelope = 1;
        frame.camera_position = {.z = 30};
        frame.view_projection = {.4F, 0, 0, 0, 0, .6F, 0, 0, 0, 0, -.02F, 0, 0, 0, 0, 1};
        frame.inverse_view_projection = {
            1 / .4F, 0, 0, 0, 0, 1 / .6F, 0, 0, 0, 0, -50, 0, 0, 0, 0, 1};
        const auto READ = [&](std::span<const PlanetNeighborTextures> neighbors = {}) {
            CHECK(renderer.draw_raw(textures, frame, .5, settings, neighbors));
            std::vector<unsigned char> pixels(pattern.size());
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const auto SKY = READ();
        frame.destination_center = {};
        for (int quality = 0; quality < 3; ++quality) {
            settings.quality = quality;
            for (const double BEND : {.25, .5, .75, 1.0}) {
                frame.destination_curvature = BEND;
                const auto PIXELS = READ();
                dump_frame(
                    "closed-surface-q" + std::to_string(quality), BEND, WIDTH, HEIGHT, PIXELS);
                const double PIXEL_X = .4 * WIDTH / 2, PIXEL_Y = .6 * HEIGHT / 2;
                for (int y = 0; y < HEIGHT; ++y) {
                    for (int x = 0; x < WIDTH; ++x) {
                        const double WX = (x + .5 - WIDTH / 2) / PIXEL_X;
                        const double WY = -(y + .5 - HEIGHT / 2) / PIXEL_Y;
                        const auto INVERSE = inverse_chart(WX, WY, BEND, frame.aspect);
                        const auto OFFSET = static_cast<std::size_t>(y * WIDTH + x) * 4;
                        const bool SAME = std::equal(PIXELS.begin() + OFFSET,
                            PIXELS.begin() + OFFSET + 4,
                            SKY.begin() + OFFSET);
                        // Check actual boundary support and all safe interior
                        // pixels against meaningful desktop texture coordinates.
                        if (INVERSE[2] < 1.0e-7 &&
                            std::max(std::abs(INVERSE[0]), std::abs(INVERSE[1])) < .9) {
                            CHECK(!SAME);
                            const double U = (1 + INVERSE[0]) / 2;
                            const double V = (1 - INVERSE[1]) / 2;
                            CHECK(std::abs(PIXELS[OFFSET] - (64 + 128 * U)) < 12);
                            CHECK(std::abs(PIXELS[OFFSET + 1] - (64 + 128 * V)) < 12);
                        }
                        if (!SAME) {
                            CHECK(PIXELS[OFFSET] >= 64);
                            CHECK(PIXELS[OFFSET + 1] >= 64);
                            CHECK(PIXELS[OFFSET + 2] == 80);
                        }
                        if (INVERSE[2] * std::min(PIXEL_X, PIXEL_Y) > 1.5) CHECK(SAME);
                    }
                }
            }
        }
        // Exercise the real inactive-world pass while a primary is still
        // unrolling, not just when every surface is already a round globe.
        std::array<PlanetNeighborTextures, 1> neighbors{
            {{.endpoint = {.identity = 3, .placement = 3}, .center = {.z = -2}, .image = WALL}}};
        for (const double BEND : {.25, .5, .75, 1.0}) {
            frame.destination_curvature = BEND;
            neighbors[0].center.z = -2;
            const auto PRIMARY = READ();
            CHECK(READ(neighbors) == PRIMARY);
            neighbors[0].center.z = 2;
            CHECK(READ(neighbors) != PRIMARY);
        }
        // Keep chart coordinates attached to that same geometry; a patterned
        // desktop is stronger evidence than a solid-colour silhouette.
        settings.quality = 2;
        frame.view_projection[0] = .5F;
        frame.view_projection[5] = .75F;
        frame.inverse_view_projection[0] = 2;
        frame.inverse_view_projection[5] = 1 / .75F;
        for (const double BEND : {.1, .3, .6, 1.0}) {
            frame.destination_curvature = BEND;
            const auto PIXELS = READ();
            for (const auto UV : {std::array{.2, .35}, std::array{.5, .5}, std::array{.8, .65}}) {
                const auto POINT = planet_hop::project_surface({.u = UV[0],
                    .v = UV[1],
                    .aspect = frame.aspect,
                    .radius = 1,
                    .coverage = .7,
                    .curvature = BEND,
                    .rotation = frame.destination_rotation});
                const int X = static_cast<int>((.5 + .25 * POINT.position.x) * WIDTH);
                const int Y = static_cast<int>((.5 - .375 * POINT.position.y) * HEIGHT);
                const auto OFFSET = static_cast<std::size_t>(Y * WIDTH + X) * 4;
                CHECK(std::abs(static_cast<int>(PIXELS[OFFSET]) - (64 + 128 * UV[0])) < 12);
                CHECK(std::abs(static_cast<int>(PIXELS[OFFSET + 1]) - (64 + 128 * UV[1])) < 12);
                CHECK(PIXELS[OFFSET + 2] == 80);
            }
        }
        // Conspicuous edge colours must reach the geometric perimeter itself.
        // Beyond it is sky, not a dark margin or clamped wallpaper extension.
        constexpr std::array<unsigned char, 4> RIGHT_EDGE{230, 30, 180, 255};
        constexpr std::array<unsigned char, 4> LEFT_EDGE{20, 200, 220, 255};
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const auto OFFSET = static_cast<std::size_t>(y * WIDTH + x) * 4;
                const auto COLOR = x >= WIDTH * 5 / 6 ? RIGHT_EDGE
                                   : x < WIDTH / 6
                                       ? LEFT_EDGE
                                       : ((x / 8 + y / 8) % 2 == 0
                                                 ? std::array<unsigned char, 4>{235, 190, 30, 255}
                                                 : std::array<unsigned char, 4>{35, 60, 130, 255});
                std::ranges::copy(COLOR, std::span{pattern}.subspan(OFFSET).begin());
            }
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, WALL);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pattern.data());
        for (const double BEND : {.15, .3, .6, 1.}) {
            frame.destination_curvature = BEND;
            const auto EDGE = planet_hop::project_surface({.u = 1,
                .v = .5,
                .aspect = 1.5,
                .radius = 1,
                .coverage = .7,
                .curvature = BEND,
                .rotation = frame.destination_rotation});
            for (const double SCALE : {.9, 1., 1.1}) {
                frame.view_projection[0] = static_cast<float>(SCALE / EDGE.position.x);
                frame.inverse_view_projection[0] = 1 / frame.view_projection[0];
                const auto PIXELS = READ();
                dump_frame("textured-material-perimeter-" + std::to_string(BEND),
                    SCALE,
                    WIDTH,
                    HEIGHT,
                    PIXELS);
                const double PERIMETER = (.5 + .5 * SCALE) * WIDTH;
                const int INSIDE = std::min(WIDTH - 3, static_cast<int>(PERIMETER) - 3);
                const auto OFFSET = static_cast<std::size_t>((HEIGHT / 2) * WIDTH + INSIDE) * 4;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    CHECK(std::abs(static_cast<int>(PIXELS[OFFSET + channel]) -
                                   RIGHT_EDGE[channel]) <= 1);
                for (int x = static_cast<int>(std::ceil(PERIMETER)) + 1; x < WIDTH; ++x) {
                    const auto OUTSIDE = static_cast<std::size_t>((HEIGHT / 2) * WIDTH + x) * 4;
                    CHECK(std::equal(PIXELS.begin() + OUTSIDE,
                        PIXELS.begin() + OUTSIDE + 4,
                        SKY.begin() + OUTSIDE));
                }
            }
        }
        frame.view_projection[0] = .5F;
        frame.inverse_view_projection[0] = 2;
        auto windows = pattern;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const auto OFFSET = static_cast<std::size_t>(y * WIDTH + x) * 4;
                const bool WINDOW =
                    x >= WIDTH / 8 && x < WIDTH * 3 / 8 && y >= HEIGHT / 8 && y < HEIGHT * 3 / 8;
                windows[OFFSET] = WINDOW ? 220 : 0;
                windows[OFFSET + 1] = WINDOW ? 20 : 0;
                windows[OFFSET + 2] = WINDOW ? 30 : 0;
                windows[OFFSET + 3] = WINDOW ? 255 : 0;
            }
        }
        const GLuint WINDOWS = texture(WIDTH, HEIGHT, windows.data());
        constexpr std::array<unsigned char, 4> GREEN{20, 220, 30, 255};
        const GLuint CROP = texture(1, 1, GREEN.data());
        const std::array<PlanetFloatingTextures, 1> FLOATING{
            {{.layer = {.image = CROP}, .rect = {.62F, .27F, .16F, .16F}}}};
        textures.source.windows = textures.destination.windows = {.image = WINDOWS};
        textures.source.floating = textures.destination.floating = FLOATING;
        // Recompose native endpoints from the same layers, as the production
        // native-HUD path does; a wallpaper-only composite would fake a jump.
        textures.native_hud = true;
        settings.elevation = 0;
        for (const double BEND : {.1, .3, .6, 1.0}) {
            frame.destination_curvature = BEND;
            const auto PIXELS = READ();
            for (const auto UV : {std::array{.25, .25}, std::array{.7, .35}}) {
                const auto POINT = planet_hop::project_surface({.u = UV[0],
                    .v = UV[1],
                    .aspect = 1.5,
                    .radius = 1,
                    .coverage = .7,
                    .curvature = BEND,
                    .rotation = frame.destination_rotation});
                const int X = static_cast<int>((.5 + .25 * POINT.position.x) * WIDTH);
                const int Y = static_cast<int>((.5 - .375 * POINT.position.y) * HEIGHT);
                const auto OFFSET = static_cast<std::size_t>(Y * WIDTH + X) * 4;
                CHECK(PIXELS[OFFSET] == (UV[0] < .5 ? 220 : 20));
                CHECK(PIXELS[OFFSET + 1] == (UV[0] < .5 ? 20 : 220));
                CHECK(PIXELS[OFFSET + 2] == 30);
            }
            dump_frame("attached-windows", BEND, WIDTH, HEIGHT, PIXELS);
        }
        const auto JOURNEY =
            planet_hop::make_journey({.identity = 1, .numbered = 1, .logical_order = 1},
                {.identity = 2, .numbered = 2, .logical_order = 2});
        // Inspect real journey frames, not only the analytic orthographic fixture.
        settings = {};
        for (const double POSITION : {0., .01, .05, .1, .18, .3, .5, .7, .82, .9, .95, .99, 1.}) {
            frame = planet_hop::evaluate(JOURNEY, POSITION, 1.5);
            CHECK(renderer.draw_raw(textures, frame, POSITION, settings));
            std::vector<unsigned char> pixels(pattern.size());
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            dump_frame("journey", POSITION, WIDTH, HEIGHT, pixels);
        }
        for (const double ENDPOINT : {0., 1.}) {
            frame = planet_hop::evaluate(JOURNEY, ENDPOINT, 1.5);
            CHECK(renderer.draw_raw(textures, frame, ENDPOINT, settings));
            std::vector<unsigned char> native(pattern.size());
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, native.data());
            const double NEAR = ENDPOINT == 0 ? 1.0e-9 : 1.0 - 1.0e-9;
            frame = planet_hop::evaluate(JOURNEY, NEAR, 1.5);
            CHECK(renderer.draw_raw(textures, frame, NEAR, settings));
            std::vector<unsigned char> pixels(pattern.size());
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            CHECK(equal_pixels(pixels, native, WIDTH, "populated-native-handoff", NEAR));
        }
        const std::array<GLuint, 4> IDS{WALL, OUT, WINDOWS, CROP};
        glDeleteTextures(static_cast<GLsizei>(IDS.size()), IDS.data());
        CHECK(glGetError() == GL_NO_ERROR);
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
        // A uniform opaque globe has no observable rotational texture features.
        // Use an asymmetric striped image so this checks material motion rather
        // than incidental silhouette pixel changes.
        std::array<unsigned char, 64 * 4> stripes{};
        for (std::size_t x = 0; x < 64; ++x) {
            stripes[x * 4] = static_cast<unsigned char>((x % 8) * 36);
            stripes[(x * 4) + 1] = static_cast<unsigned char>(255 - (x * 3));
            stripes[(x * 4) + 3] = 255;
        }
        const GLuint ROTATION_IMAGE = texture(64, 1, stripes.data());
        neighbors[0].image = ROTATION_IMAGE;
        settings.solar_background_scale = 2;
        const auto MOVED = READ(neighbors, .2);
        CHECK(READ(neighbors, .7) != MOVED);
        CHECK(READ(neighbors, .2) == MOVED);  // Reversal retraces the same orientation exactly.
        settings.rotation_enabled = false;
        CHECK(READ(neighbors, .2) == READ(neighbors, .7));
        // Remote previews retain their owner's chart aspect, independently of
        // the viewer/camera aspect. A portrait capture is not a wide desktop.
        neighbors[0].endpoint.monitor_width = 100;
        neighbors[0].endpoint.monitor_height = 300;
        const auto PORTRAIT_OWNER = READ(neighbors);
        neighbors[0].endpoint.monitor_width = 300;
        neighbors[0].endpoint.monitor_height = 100;
        const auto WIDE_OWNER = READ(neighbors);
        CHECK(WIDE_OWNER != PORTRAIT_OWNER);
        const auto LANDMARK = static_cast<std::size_t>((HEIGHT / 2) * WIDTH + WIDTH / 2 + 6) * 4;
        const double RADIUS = frame.radius * settings.solar_background_scale;
        const double LANDMARK_X = 6.5 / (.15 * WIDTH / 2 * RADIUS);
        const double LANDMARK_Y = .5 / (.225 * HEIGHT / 2 * RADIUS);
        const auto WIDE_UV = inverse_chart(LANDMARK_X, LANDMARK_Y, 1, 3.);
        const auto PORTRAIT_UV = inverse_chart(LANDMARK_X, LANDMARK_Y, 1, 1. / 3);
        // Green is the stripe fixture's linear ramp: texture coordinate u
        // samples texel 64*u-.5, whose green value is 255-3*texel.
        CHECK(std::abs(WIDE_OWNER[LANDMARK + 1] - (256.5 - 192 * (1 + WIDE_UV[0]) / 2)) < 3);
        CHECK(
            std::abs(PORTRAIT_OWNER[LANDMARK + 1] - (256.5 - 192 * (1 + PORTRAIT_UV[0]) / 2)) < 3);
        CHECK(WIDE_OWNER[LANDMARK + 1] > PORTRAIT_OWNER[LANDMARK + 1]);
        // Keep the actual projection matrix fixed: changing only the viewer's
        // chart metadata must not remap the remote owner's recognizable image.
        for (const double VIEWER_ASPECT : {.5, 4.}) {
            frame.aspect = VIEWER_ASPECT;
            CHECK(READ(neighbors) == WIDE_OWNER);
        }
        frame.aspect = static_cast<double>(WIDTH) / HEIGHT;
        neighbors[0].endpoint.monitor_width = neighbors[0].endpoint.monitor_height = 1;
        settings.rotation_enabled = true;
        settings.rotation_limit = 0;
        CHECK(READ(neighbors, .2) == READ(neighbors, .7));
        settings.rotation_limit = .35F;
        settings.solar_background_scale = .75F;
        neighbors[0].image = PREVIEW;
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
        const std::array<GLuint, 6> IDS{WALL, PREVIEW, WINDOWS, OUT, ALPHA_PREVIEW, ROTATION_IMAGE};
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

    void check_topology_layout_visibility(PlanetTestRenderer &renderer) {
        constexpr int HEIGHT = 240;  // Keep actual cruise bodies above subpixel LOD.
        const std::array<unsigned char, 4> BLUE{0, 0, 255, 255};
        const std::array<unsigned char, 4> GREEN{0, 255, 0, 255};
        const GLuint DESKTOP = texture(1, 1, BLUE.data());
        const GLuint PREVIEW = texture(1, 1, GREEN.data());
        PlanetRenderSettings settings;
        settings.clouds = settings.lighting = settings.depth_enabled = false;
        settings.star_density = settings.atmosphere_intensity = 0;
        for (const auto LAYOUT : {planet_hop::Layout::Monitor,
                 planet_hop::Layout::All,
                 planet_hop::Layout::SolarSystem}) {
            for (const double ASPECT : {.6, 1., 16. / 9, 2.4}) {
                const int WIDTH = static_cast<int>(HEIGHT * ASPECT);
                const GLuint OUT = texture(WIDTH, HEIGHT, nullptr);
                PlanetFrameTextures textures{};
                textures.source.wallpaper = textures.source.composite = DESKTOP;
                textures.destination.wallpaper = textures.destination.composite = DESKTOP;
                textures.source_identity = 1;
                textures.destination_identity = 3;
                textures.output = OUT;
                textures.width = WIDTH;
                textures.height = HEIGHT;
                auto from = JourneyEndpoint{.identity = 1,
                    .numbered = 1,
                    .placement = 2,
                    .owner = 10,
                    .monitor_width = 1920,
                    .monitor_height = 1080};
                auto to = from;
                to.identity = 3;
                to.numbered = 3;
                to.placement = 6;
                planet_hop::SceneOptions options;
                options.planet_layout = LAYOUT;
                const double RENDER_ASPECT = static_cast<double>(WIDTH) / HEIGHT;
                const auto JOURNEY = planet_hop::make_journey(from, to, options, RENDER_ASPECT);
                const auto READ = [&](const planet_hop::Frame &scene,
                                      double progress,
                                      std::span<const PlanetNeighborTextures> neighbors) {
                    CHECK(renderer.draw_raw(textures, scene, progress, settings, neighbors));
                    std::vector<unsigned char> pixels(static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
                    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    return pixels;
                };
                unsigned revealed_frames = 0;
                std::array<unsigned, 2> owner_visible_frames{};
                for (const double PROGRESS :
                    {1e-8, .32, .4, .45, .475, .5, .525, .55, .6, .68, 1 - 1e-8}) {
                    const auto SCENE = planet_hop::evaluate(JOURNEY, PROGRESS, RENDER_ASPECT);
                    std::vector<PlanetNeighborTextures> neighbors;
                    for (const std::uint32_t number : {2U, 4U, 5U, 6U, 7U}) {
                        auto endpoint = from;
                        endpoint.identity = number;
                        endpoint.numbered = number;
                        endpoint.placement = number * 2;
                        if (number % 2 == 0 && number != 2) {
                            endpoint.owner = 20;
                            endpoint.monitor_x = 1920;
                            endpoint.monitor_width = 1280;
                            endpoint.monitor_height = 720;
                        }
                        if (number == 7) {
                            endpoint.numbered.reset();
                            endpoint.placement = 65;
                        }
                        if (!planet_hop::scene_member(endpoint, from, to, LAYOUT)) {
                            continue;
                        }
                        const auto POSITION =
                            planet_hop::layout_position(endpoint, options, RENDER_ASPECT);
                        const planet_hop::Vec3 CENTER{POSITION.x - SCENE.world_origin.x,
                            POSITION.y - SCENE.world_origin.y,
                            POSITION.z - SCENE.world_origin.z};
                        const double OWNER_BOUND =
                            .75 * std::max(1., endpoint.monitor_width / endpoint.monitor_height) /
                            std::max(1., SCENE.aspect);
                        if (planet_hop::neighbor_visible(SCENE, CENTER, OWNER_BOUND)) {
                            neighbors.push_back({.endpoint = endpoint,
                                .center = CENTER,
                                .image = PREVIEW,
                                .shared_wallpaper = false});
                        }
                    }
                    const auto EMPTY = READ(SCENE, PROGRESS, {});
                    const auto POPULATED = READ(SCENE, PROGRESS, neighbors);
                    for (std::size_t owner = 0; owner < owner_visible_frames.size(); ++owner) {
                        auto endpoint = from;
                        endpoint.identity = 2;
                        endpoint.numbered = 2;
                        endpoint.placement = 4;
                        if (owner == 1) {
                            endpoint.owner = 20;
                            endpoint.monitor_x = 1920;
                            endpoint.monitor_width = 1280;
                            endpoint.monitor_height = 720;
                        }
                        const auto POSITION =
                            planet_hop::layout_position(endpoint, options, RENDER_ASPECT);
                        const planet_hop::Vec3 CENTER{POSITION.x - SCENE.world_origin.x,
                            POSITION.y - SCENE.world_origin.y,
                            POSITION.z - SCENE.world_origin.z};
                        const double BOUND =
                            .75 * std::max(1., endpoint.monitor_width / endpoint.monitor_height) /
                            std::max(1., SCENE.aspect);
                        std::array<PlanetNeighborTextures, 1> body{{{.endpoint = endpoint,
                            .center = CENTER,
                            .image = PREVIEW,
                            .shared_wallpaper = false}}};
                        const bool ELIGIBLE =
                            planet_hop::scene_member(endpoint, from, to, LAYOUT) &&
                            planet_hop::neighbor_visible(SCENE, CENTER, BOUND);
                        const auto PIXELS = READ(SCENE,
                            PROGRESS,
                            ELIGIBLE ? std::span<const PlanetNeighborTextures>{body}
                                     : std::span<const PlanetNeighborTextures>{});
                        if (PROGRESS < .01 || PROGRESS > .99) {
                            CHECK(PIXELS == EMPTY);
                        } else {
                            owner_visible_frames[owner] += static_cast<unsigned>(PIXELS != EMPTY);
                        }
                    }
                    if (PROGRESS < .01 || PROGRESS > .99) {
                        CHECK(POPULATED == EMPTY);
                    } else {
                        revealed_frames += static_cast<unsigned>(POPULATED != EMPTY);
                    }
                }
                CHECK(revealed_frames >= 2);
                CHECK(owner_visible_frames[0] >= 2);
                CHECK(LAYOUT == planet_hop::Layout::Monitor ? owner_visible_frames[1] == 0
                                                            : owner_visible_frames[1] >= 2);
                glDeleteTextures(1, &OUT);
            }
        }
        const std::array<GLuint, 2> IDS{DESKTOP, PREVIEW};
        glDeleteTextures(static_cast<GLsizei>(IDS.size()), IDS.data());
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
        namespace shader = planet_hop::shader;
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

    void run(bool profile, int width, int height, int frames) {
        PlanetTestRenderer renderer;
        check_depth(renderer);
        if (!renderer.initialize_raw()) {
            CHECK(false);
            std::exit(EXIT_FAILURE);
        }
        check_native_floating(renderer);
        if (!profile) {
            check_allocation_retry();
            check_continuous_surface(renderer);
            check_landing_coverage(renderer);
            check_inventory_and_native_hud(renderer);
            check_topology_layout_visibility(renderer);
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
            Transition motion;
            CHECK(motion.begin(Direction::Forward, .8, [](double time) { return time; }));
            motion.advance(.28);
            const auto READ = [&](double progress) {
                CHECK(RENDER(progress));
                std::vector<unsigned char> pixels(source.size());
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                return pixels;
            };
            const auto INTERRUPTED = READ(motion.progress());
            motion.retarget(false, .8, motion.motion(), [](double position, bool destination) {
                return planet_hop::remaining_travel(position, destination, false);
            });
            CHECK(READ(motion.progress()) == INTERRUPTED);
            motion.advance(.012);
            CHECK(motion.progress() > .35);
            const auto BRAKING = READ(motion.progress());
            CHECK(BRAKING != INTERRUPTED && BRAKING != destination);
            motion.advance(.15);
            CHECK(motion.active() && motion.progress() < .35);
            CHECK(READ(motion.progress()) != destination);
            motion.advance(.103);
            CHECK(!motion.active());
            CHECK(equal_pixels(READ(motion.progress()), source, width, "raw-reversal-landing", 0));

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
