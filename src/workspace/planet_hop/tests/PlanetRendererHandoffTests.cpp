#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <GLES3/gl32.h>
#include <png.h>

#include "Check.hpp"
#include "PlanetRendererTestFixture.hpp"
#include "workspace/WorkspaceTransition.hpp"

namespace planet_renderer_tests {
    using namespace hyprimate::workspace;
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
    void check_raw_endpoints(PlanetTestRenderer &renderer,
        const PlanetFrameTextures &textures,
        const planet_hop::Journey &journey,
        const PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &source,
        const std::vector<unsigned char> &destination,
        std::vector<unsigned char> &actual) {
        const auto RENDER = [&](double progress) {
            return renderer.draw_raw(textures,
                planet_hop::evaluate(journey, progress, static_cast<double>(width) / height),
                progress,
                settings);
        };
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
    }
    void check_reversal_landing(PlanetTestRenderer &renderer,
        const PlanetFrameTextures &textures,
        const planet_hop::Journey &journey,
        const PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &source,
        const std::vector<unsigned char> &destination) {
        const auto RENDER = [&](double progress) {
            return renderer.draw_raw(textures,
                planet_hop::evaluate(journey, progress, static_cast<double>(width) / height),
                progress,
                settings);
        };
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
}  // namespace planet_renderer_tests
