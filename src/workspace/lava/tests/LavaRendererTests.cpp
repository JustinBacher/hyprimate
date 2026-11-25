#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "workspace/WorkspaceTransition.hpp"
#include "LavaRendererTestFixture.hpp"
#include "workspace/lava/LavaRenderer.hpp"

namespace {
    using namespace hyprimate::workspace;
    using hyprimate::workspace::lava::Material;
    using hyprimate::workspace::lava::Renderer;
    using hyprimate::workspace::lava::WindowPlanes;
    using lava_renderer_tests::check_binding_restore;
    using lava_renderer_tests::check_gl_contract;
    using lava_renderer_tests::default_material;
    using lava_renderer_tests::draw;
    using lava_renderer_tests::framebuffer_for;
    using lava_renderer_tests::read_output;

    constexpr int WIDTH = 128;
    constexpr int HEIGHT = 96;
    using Pixels = std::vector<unsigned char>;

    GLuint make_texture(int width, int height, const Pixels &pixels) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.empty() ? nullptr : pixels.data());
        return texture;
    }

    bool inside(int column, int row, int left, int right, int top, int bottom) {
        return column > left && column < right && row > top && row < bottom;
    }

    Pixels desktop(int variant, int width = WIDTH, int height = HEIGHT) {
        Pixels pixels(static_cast<std::size_t>(width) * height * 4, 255);
        const bool ALTERNATE_LAYOUT = (variant % 2) != 0;
        const int LEFT = ALTERNATE_LAYOUT ? width / 3 : width / 8;
        const int RIGHT = ALTERNATE_LAYOUT ? width * 7 / 8 : width * 5 / 8;
        const int TOP = ALTERNATE_LAYOUT ? height / 7 : height / 5;
        const int BOTTOM = ALTERNATE_LAYOUT ? height * 3 / 4 : height * 4 / 5;
        const auto SECONDARY =
            ALTERNATE_LAYOUT
                ? std::array{width / 12, width / 3, height / 2, height * 5 / 6}
                : std::array{width * 7 / 10, width * 9 / 10, height / 3, height * 2 / 3};
        using RGB = std::array<unsigned char, 3>;
        const RGB ICON = ALTERNATE_LAYOUT ? RGB{226, 64, 152} : RGB{38, 92, 238};
        for (int line = 0; line < height; ++line) {
            for (int column = 0; column < width; ++column) {
                const std::size_t OFFSET = ((static_cast<std::size_t>(line) * width) + column) * 4;
                // Fine grid plus large, color-coded desktop landmarks: a status bar, a dock,
                // two windows with borders, and a high-contrast icon. No random/environment data.
                pixels[OFFSET] = static_cast<unsigned char>(
                    18 + (((column * 7) + (line * 3) + (variant * 29)) % 51));
                pixels[OFFSET + 1] = static_cast<unsigned char>(
                    24 + (((column * 2) + (line * 9) + (variant * 17)) % 47));
                pixels[OFFSET + 2] = static_cast<unsigned char>(
                    31 + (((column * 5) + (line * 4) + (variant * 11)) % 43));
                if (line < height / 10) {
                    pixels[OFFSET] = 224;
                    pixels[OFFSET + 1] = 176;
                    pixels[OFFSET + 2] = 24;
                }
                if (line > height * 8 / 9 && column > width / 5 && column < width * 4 / 5) {
                    pixels[OFFSET] = 34;
                    pixels[OFFSET + 1] = 218;
                    pixels[OFFSET + 2] = 203;
                }
                if (inside(column, line, LEFT, RIGHT, TOP, BOTTOM)) {
                    RGB window{246, 75, 32};
                    if (inside(column, line, LEFT + 2, RIGHT - 3, TOP + 2, BOTTOM - 3)) {
                        window = {static_cast<unsigned char>(40 + (column % 55)),
                            static_cast<unsigned char>(56 + (line % 67)),
                            168};
                    }
                    std::ranges::copy(window, pixels.data() + OFFSET);
                }
                if (inside(column, line, SECONDARY[0], SECONDARY[1], SECONDARY[2], SECONDARY[3])) {
                    std::ranges::copy(ICON, pixels.data() + OFFSET);
                }
            }
        }
        return pixels;
    }

    Pixels empty_desktop(int width = WIDTH, int height = HEIGHT) {
        Pixels pixels(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
            pixels[offset] = 28;
            pixels[offset + 1] = 39;
            pixels[offset + 2] = 53;
            pixels[offset + 3] = 255;
        }
        return pixels;
    }
    struct BurnFixture {
        Pixels source;
        Pixels destination;
        WindowPlanes planes;

        BurnFixture() = default;
        BurnFixture(const BurnFixture &) = delete;
        BurnFixture &operator=(const BurnFixture &) = delete;
        BurnFixture(BurnFixture &&other) noexcept
            : source(std::move(other.source)),
              destination(std::move(other.destination)),
              planes(std::exchange(other.planes, {})) {
        }
        ~BurnFixture() {
            const std::array TEXTURES{planes.source_bottom,
                planes.source_windows,
                planes.destination_bottom,
                planes.destination_windows};
            glDeleteTextures(static_cast<GLsizei>(TEXTURES.size()), TEXTURES.data());
        }
    };

    BurnFixture burn_fixture(int width = WIDTH, int height = HEIGHT, bool alternate_ink = false) {
        BurnFixture fixture;
        fixture.source = desktop(0, width, height);
        fixture.destination = desktop(1, width, height);
        Pixels source_bottom = fixture.source;
        Pixels destination_bottom = fixture.destination;
        Pixels source_windows = fixture.source;
        Pixels destination_windows = fixture.destination;
        for (Pixels *windows :
            {&source_windows, &destination_windows, &fixture.source, &fixture.destination}) {
            for (std::size_t offset = 3; offset < windows->size(); offset += 4) {
                (*windows)[offset] = 0;
            }
        }
        const auto sx = [width](int value) { return value * width / WIDTH; };
        const auto sy = [height](int value) { return value * height / HEIGHT; };
        const auto composite = [width, alternate_ink](Pixels &scene,
                                   Pixels &windows,
                                   int left,
                                   int right,
                                   int top,
                                   int bottom_edge,
                                   std::array<int, 3> color,
                                   float coverage) {
            for (int row = top; row < bottom_edge; ++row) {
                for (int col = left; col < right; ++col) {
                    if (col >= left + 8 && col < left + 14 && row >= top + 7 && row < top + 13) {
                        continue;
                    }
                    const std::size_t offset = (static_cast<std::size_t>(row) * width + col) * 4;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const float backdrop = windows[offset + channel];
                        windows[offset + channel] = static_cast<unsigned char>(
                            backdrop * (1.F - coverage) +
                            (alternate_ink ? 255 - color[channel] : color[channel]) * coverage +
                            0.5F);
                        scene[offset + channel] = windows[offset + channel];
                    }
                    windows[offset + 3] = static_cast<unsigned char>(
                        windows[offset + 3] * (1.F - coverage) + coverage * 255.F + 0.5F);
                    scene[offset + 3] = 0;
                }
            }
        };
        composite(
            fixture.source, source_windows, sx(12), sx(62), sy(18), sy(72), {240, 32, 16}, 0.8F);
        composite(
            fixture.source, source_windows, sx(36), sx(88), sy(34), sy(78), {24, 224, 48}, 0.45F);
        composite(fixture.destination,
            destination_windows,
            sx(16),
            sx(76),
            sy(16),
            sy(70),
            {20, 48, 240},
            0.8F);
        composite(fixture.destination,
            destination_windows,
            sx(52),
            sx(104),
            sy(38),
            sy(82),
            {224, 176, 16},
            0.45F);
        for (Pixels *scene : {&fixture.source, &fixture.destination}) {
            for (int row = sy(30); row < sy(60); ++row) {
                for (int col = sx(48); col < sx(80); ++col) {
                    const std::size_t offset = (static_cast<std::size_t>(row) * width + col) * 4;
                    (*scene)[offset] = 72;
                    (*scene)[offset + 1] = 95;
                    (*scene)[offset + 2] = 138;
                    (*scene)[offset + 3] = 255;
                }
            }
        }
        fixture.planes.source_bottom = make_texture(width, height, source_bottom);
        fixture.planes.source_windows = make_texture(width, height, source_windows);
        fixture.planes.destination_bottom = make_texture(width, height, destination_bottom);
        fixture.planes.destination_windows = make_texture(width, height, destination_windows);
        return fixture;
    }


    GLuint output_texture(int width, int height) {
        return make_texture(width, height, {});
    }


    std::size_t difference_count(const Pixels &first, const Pixels &second, int threshold = 2) {
        if (first.size() != second.size()) {
            return std::max(first.size(), second.size());
        }
        std::size_t count = 0;
        for (std::size_t i = 0; i < first.size(); i += 4) {
            const int DELTA = std::max({std::abs(static_cast<int>(first[i]) - second[i]),
                std::abs(static_cast<int>(first[i + 1]) - second[i + 1]),
                std::abs(static_cast<int>(first[i + 2]) - second[i + 2])});
            count += static_cast<std::size_t>(DELTA > threshold);
        }
        return count;
    }

    struct Region {
        int left, right, top = 0, bottom = HEIGHT;
    };

    double matching_fraction(const Pixels &actual, const Pixels &expected, Region area) {
        std::size_t matches = 0;
        for (int row = area.top; row < area.bottom; ++row) {
            for (int col = area.left; col < area.right; ++col) {
                const std::size_t OFFSET = ((static_cast<std::size_t>(row) * WIDTH) + col) * 4;
                matches += static_cast<std::size_t>(std::equal(
                    actual.data() + OFFSET, actual.data() + OFFSET + 3, expected.data() + OFFSET));
            }
        }
        return static_cast<double>(matches) / ((area.right - area.left) * (area.bottom - area.top));
    }

    struct PigmentMeasure {
        double x = 0.0;
        double y = 0.0;
        double mass = 0.0;
    };

    PigmentMeasure pigment_measure(const Pixels &pigmented, const Pixels &plain) {
        double x_sum = 0.0;
        double y_sum = 0.0;
        double mass = 0.0;
        for (int row = 0; row < HEIGHT; ++row) {
            for (int col = 0; col < WIDTH; ++col) {
                const std::size_t OFFSET = ((static_cast<std::size_t>(row) * WIDTH) + col) * 4;
                const double WEIGHT =
                    std::abs(static_cast<int>(pigmented[OFFSET]) - plain[OFFSET]) +
                    std::abs(static_cast<int>(pigmented[OFFSET + 1]) - plain[OFFSET + 1]) +
                    std::abs(static_cast<int>(pigmented[OFFSET + 2]) - plain[OFFSET + 2]);
                mass += WEIGHT;
                x_sum += col * WEIGHT;
                y_sum += row * WEIGHT;
            }
        }
        if (mass == 0.0) {
            return {};
        }
        return {.x = x_sum / mass, .y = y_sum / mass, .mass = mass};
    }

    struct PigmentTarget {
        GLuint plain, destination, output, fbo;
    };

    void check_pigment_transport(
        Renderer &renderer, const Pixels &plain, PigmentTarget target, Direction direction) {
        const bool FORWARD = direction == Direction::Forward;
        const int CENTER = FORWARD ? WIDTH * 3 / 10 : WIDTH * 7 / 10;
        Pixels marked = plain;
        for (int row = (HEIGHT / 2) - 4; row <= (HEIGHT / 2) + 4; ++row) {
            for (int col = CENTER - 4; col <= CENTER + 4; ++col) {
                const std::size_t OFFSET = ((static_cast<std::size_t>(row) * WIDTH) + col) * 4;
                marked[OFFSET] = 246;
                marked[OFFSET + 1] = 241;
                marked[OFFSET + 2] = 226;
            }
        }
        const PigmentMeasure INITIAL = pigment_measure(marked, plain);
        const GLuint INK = make_texture(WIDTH, HEIGHT, marked);
        Pixels base(plain.size());
        Pixels ink(plain.size());
        for (int variant = 0; variant != 4; ++variant) {
            const Material MATERIAL = default_material(variant);
            const auto SAMPLE = [&](GLuint source, double progress, Pixels &pixels) {
                CHECK(draw(renderer,
                    source,
                    target.destination,
                    target.output,
                    target.fbo,
                    WIDTH,
                    HEIGHT,
                    progress,
                    direction,
                    MATERIAL));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
                glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            };
            double excursion = 0.0;
            double drift = 0.0;
            for (int step = 3; step <= 16; ++step) {
                SAMPLE(target.plain, step * 0.05, base);
                SAMPLE(INK, step * 0.05, ink);
                const PigmentMeasure CURRENT = pigment_measure(ink, base);
                if (CURRENT.mass >= INITIAL.mass * 0.08) {
                    excursion = std::max(
                        excursion, FORWARD ? CURRENT.x - INITIAL.x : INITIAL.x - CURRENT.x);
                    drift = std::max(drift, std::abs(CURRENT.y - INITIAL.y));
                }
            }
            std::printf("pigment variant=%d direction=%s excursion_px=%.2f vertical_px=%.2f\n",
                variant,
                FORWARD ? "right" : "left",
                excursion,
                drift);
            CHECK(excursion >= WIDTH * 0.08);
            CHECK(drift <= HEIGHT * 0.12);
        }
        glDeleteTextures(1, &INK);
    }

    std::uint64_t image_energy(const Pixels &pixels) {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            total += pixels[i] + pixels[i + 1] + pixels[i + 2];
        }
        return total;
    }

    bool write_ppm(const std::filesystem::path &path, const Pixels &pixels, int width, int height) {
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        file << "P6\n" << width << ' ' << height << "\n255\n";
        std::vector<char> row(static_cast<std::size_t>(width) * 3);
        for (int line = height - 1; line >= 0; --line) {
            for (int column = 0; column < width; ++column) {
                const std::size_t SOURCE = ((static_cast<std::size_t>(line) * width) + column) * 4;
                const std::size_t TARGET = static_cast<std::size_t>(column) * 3;
                row[TARGET] = static_cast<char>(pixels[SOURCE]);
                row[TARGET + 1] = static_cast<char>(pixels[SOURCE + 1]);
                row[TARGET + 2] = static_cast<char>(pixels[SOURCE + 2]);
            }
            file.write(row.data(), static_cast<std::streamsize>(row.size()));
        }
        return static_cast<bool>(file);
    }

    void check_pixel_diff(const Pixels &first,
        const Pixels &second,
        const char *description,
        std::size_t minimum = 32) {
        const std::size_t COUNT = difference_count(first, second);
        if (COUNT < minimum) {
            std::cerr << description << ": only " << COUNT << " pixels differ (need " << minimum
                      << ")\n";
        }
        CHECK(COUNT >= minimum);
    }

    void run_case(Renderer &renderer,
        int width,
        int height,
        const Pixels &source_pixels,
        const Pixels &destination_pixels,
        const Material &material,
        Direction direction,
        double progress,
        Pixels *result = nullptr,
        WindowPlanes planes = {}) {
        const GLuint SOURCE = make_texture(width, height, source_pixels);
        const GLuint DESTINATION = make_texture(width, height, destination_pixels);
        const GLuint OUTPUT = output_texture(width, height);
        const GLuint FBO = framebuffer_for(OUTPUT);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        CHECK(draw(renderer,
            SOURCE,
            DESTINATION,
            OUTPUT,
            FBO,
            width,
            height,
            progress,
            direction,
            material,
            planes));
        Pixels pixels = read_output(FBO, width, height);
        if (result != nullptr) {
            *result = std::move(pixels);
        }
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &OUTPUT);
        glDeleteTextures(1, &DESTINATION);
        glDeleteTextures(1, &SOURCE);
    }
    void check_destination_pigment_transport(
        Renderer &renderer, const Pixels &source, const Pixels &destination, Direction direction) {
        const bool FORWARD = direction == Direction::Forward;
        const int REST_X = WIDTH / 2;
        Pixels marked_destination = destination;
        for (int row = HEIGHT / 2 - 6; row <= HEIGHT / 2 + 6; ++row) {
            for (int col = REST_X - 6; col <= REST_X + 6; ++col) {
                const std::size_t offset = (static_cast<std::size_t>(row) * WIDTH + col) * 4;
                marked_destination[offset] = 250;
                marked_destination[offset + 1] = 12;
                marked_destination[offset + 2] = 8;
            }
        }
        const double REST = pigment_measure(marked_destination, destination).x;
        constexpr std::array PROGRESSES{0.78, 0.84, 0.91, 1.0};
        for (int variant = 0; variant != 4; ++variant) {
            const Material MATERIAL = default_material(variant);
            std::array<PigmentMeasure, PROGRESSES.size()> positions{};
            for (std::size_t i = 0; i < PROGRESSES.size(); ++i) {
                Pixels plain;
                Pixels marked;
                run_case(renderer,
                    WIDTH,
                    HEIGHT,
                    source,
                    destination,
                    MATERIAL,
                    direction,
                    PROGRESSES[i],
                    &plain);
                run_case(renderer,
                    WIDTH,
                    HEIGHT,
                    source,
                    marked_destination,
                    MATERIAL,
                    direction,
                    PROGRESSES[i],
                    &marked);
                positions[i] = pigment_measure(marked, plain);
                CHECK(positions[i].mass > 0.0);
            }
            CHECK((FORWARD ? REST - positions[0].x : positions[0].x - REST) > 2.0);
            for (std::size_t i = 1; i < positions.size(); ++i) {
                CHECK(FORWARD ? positions[i].x + 1.0 >= positions[i - 1].x
                              : positions[i].x - 1.0 <= positions[i - 1].x);
            }
            CHECK(std::abs(positions.back().x - REST) < 0.01);
        }
    }

    void check_controls(Renderer &renderer, const Pixels &source, const Pixels &destination) {
        static constexpr std::array KNOBS{"viscosity",
            "cooling_rate",
            "crust_amount",
            "glow_intensity",
            "detail_preservation",
            "trail_smear",
            "edge_drip",
            "ash_amount"};
        const std::array<std::array<float, 6>, 4> ALTERED{{
            {{0.96F, 0.08F, 0.91F, 0.F, 0.F, 0.F}},
            {{0.12F, 0.91F, 0.96F, 0.08F, 0.F, 0.F}},
            {{0.98F, 0.08F, 0.96F, 0.08F, 0.F, 0.F}},
            {{0.08F, 0.96F, 0.08F, 0.96F, 0.08F, 0.96F}},
        }};
        const std::array<int, 4> CONTROL_COUNT{3, 4, 4, 6};
        const auto RENDER = [&](const Material &material, Pixels &pixels, double progress = 0.43) {
            run_case(renderer,
                WIDTH,
                HEIGHT,
                source,
                destination,
                material,
                Direction::Forward,
                progress,
                &pixels);
        };
        for (int variant = 0; variant != 4; ++variant) {
            const Material DEFAULTS = default_material(variant);
            Pixels original;
            Pixels changed_frame;
            RENDER(DEFAULTS, original);
            Pixels cooling;
            RENDER(DEFAULTS, cooling, 0.78);
            for (std::size_t knob = 0; knob < KNOBS.size(); ++knob) {
                Material changed = DEFAULTS;
                if (knob < 4) {
                    changed.sharedA[knob] = knob == 0 ? 0.16F : 0.98F;
                } else {
                    changed.sharedB[knob - 4] = knob == 7 ? 0.92F : 0.02F;
                }
                RENDER(changed, changed_frame, knob == 1 ? 0.78 : 0.43);
                check_pixel_diff(knob == 1 ? cooling : original, changed_frame, KNOBS[knob], 12);
            }
            for (int knob = 0; knob != CONTROL_COUNT[variant]; ++knob) {
                Material changed = DEFAULTS;
                if (knob < 4) {
                    changed.specialA[static_cast<std::size_t>(knob)] = ALTERED[variant][knob];
                } else {
                    changed.specialB[static_cast<std::size_t>(knob - 4)] = ALTERED[variant][knob];
                }
                RENDER(changed, changed_frame);
                check_pixel_diff(original, changed_frame, "variant control", 12);
            }
        }
    }

    void run() {
        const gl_harness::CContext CONTEXT(WIDTH, HEIGHT);
        if (!CONTEXT.valid()) {
            std::cerr << "could not create surfaceless GLES context\n";
            CHECK(false);
            return;
        }
        Renderer renderer;
        CHECK(renderer.initialize());
        if (g_failures != 0) {
            renderer.release();
            return;
        }

        const Pixels EMPTY = empty_desktop();
        const Pixels SOURCE = desktop(0);
        const Pixels DESTINATION = desktop(1);
        const GLuint SOURCE_TEXTURE = make_texture(WIDTH, HEIGHT, SOURCE);
        const GLuint DESTINATION_TEXTURE = make_texture(WIDTH, HEIGHT, DESTINATION);
        const GLuint EMPTY_TEXTURE = make_texture(WIDTH, HEIGHT, EMPTY);
        const GLuint EMPTY_DESTINATION_TEXTURE = make_texture(WIDTH, HEIGHT, EMPTY);
        const GLuint IDENTICAL_TEXTURE = make_texture(WIDTH, HEIGHT, SOURCE);
        const GLuint OUTPUT = output_texture(WIDTH, HEIGHT);
        const GLuint FBO = framebuffer_for(OUTPUT);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        // Every style preserves native composites exactly at both endpoints. Identical scenes
        // still use distinct capture storage and must form visible molten material between them.
        for (int variant = 0; variant != 4; ++variant) {
            const Material MATERIAL = default_material(variant);
            CHECK(draw(renderer,
                SOURCE_TEXTURE,
                DESTINATION_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                0.0,
                Direction::Forward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == SOURCE);
            CHECK(draw(renderer,
                SOURCE_TEXTURE,
                DESTINATION_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                1.0,
                Direction::Forward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == DESTINATION);
            CHECK(draw(renderer,
                EMPTY_TEXTURE,
                EMPTY_DESTINATION_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                0.0,
                Direction::Backward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == EMPTY);
            CHECK(draw(renderer,
                EMPTY_TEXTURE,
                EMPTY_DESTINATION_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                1.0,
                Direction::Backward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == EMPTY);
            CHECK(draw(renderer,
                SOURCE_TEXTURE,
                IDENTICAL_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                0.0,
                Direction::Forward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == SOURCE);
            CHECK(draw(renderer,
                SOURCE_TEXTURE,
                IDENTICAL_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                1.0,
                Direction::Forward,
                MATERIAL));
            CHECK(read_output(FBO, WIDTH, HEIGHT) == SOURCE);

            CHECK(draw(renderer,
                EMPTY_TEXTURE,
                EMPTY_DESTINATION_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                0.5,
                Direction::Forward,
                MATERIAL));
            const Pixels MOLTEN_EMPTY = read_output(FBO, WIDTH, HEIGHT);
            CHECK(difference_count(MOLTEN_EMPTY, EMPTY) > 128);
            Pixels uniform(MOLTEN_EMPTY.size());
            for (std::size_t i = 0; i < MOLTEN_EMPTY.size(); i += 4) {
                std::copy_n(
                    MOLTEN_EMPTY.begin(), 4, uniform.begin() + static_cast<std::ptrdiff_t>(i));
            }
            CHECK(difference_count(MOLTEN_EMPTY, uniform) > 128);

            CHECK(draw(renderer,
                SOURCE_TEXTURE,
                IDENTICAL_TEXTURE,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                0.5,
                Direction::Forward,
                MATERIAL));
            CHECK(difference_count(read_output(FBO, WIDTH, HEIGHT), SOURCE) > 128);
        }
        check_binding_restore(renderer,
            SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            default_material(3));
        BurnFixture burn = burn_fixture();
        const GLuint BURN_SOURCE = make_texture(WIDTH, HEIGHT, burn.source);
        const GLuint BURN_DESTINATION = make_texture(WIDTH, HEIGHT, burn.destination);
        const auto alternate = burn_fixture(WIDTH, HEIGHT, true);
        const GLuint ALT_SOURCE = make_texture(WIDTH, HEIGHT, alternate.source);
        const GLuint ALT_DEST = make_texture(WIDTH, HEIGHT, alternate.destination);
        Pixels final_scene = burn.destination;
        for (std::size_t i = 3; i < final_scene.size(); i += 4) {
            final_scene[i] = 255;  // Pass coverage in alpha is not presentation opacity.
        }
        for (int variant = 0; variant != 4; ++variant) {
            for (const Direction DIRECTION : {Direction::Forward, Direction::Backward}) {
                const auto FRAME = [&](double progress, bool burn_up, bool alternate_ink = false) {
                    auto material = default_material(variant);
                    material.burn_up = burn_up;
                    CHECK(draw(renderer,
                        alternate_ink ? ALT_SOURCE : BURN_SOURCE,
                        alternate_ink ? ALT_DEST : BURN_DESTINATION,
                        OUTPUT,
                        FBO,
                        WIDTH,
                        HEIGHT,
                        progress,
                        DIRECTION,
                        material,
                        alternate_ink ? alternate.planes : burn.planes));
                    return read_output(FBO, WIDTH, HEIGHT);
                };
                // Only the visible window ink varies. The printed window remains at
                // peak in unified mode, is fully consumed in burn mode, then reforms.
                CHECK(difference_count(FRAME(.08, true), FRAME(.08, true, true)) > 512);
                CHECK(difference_count(FRAME(.50, false), FRAME(.50, false, true)) > 128);
                const auto peak = FRAME(.50, true);
                CHECK(difference_count(peak, FRAME(.50, true, true), 1) == 0);
                CHECK(difference_count(FRAME(.88, true), FRAME(.88, true, true)) > 256);
                CHECK(FRAME(.99, true) == final_scene);
                CHECK(FRAME(1.0, true) == final_scene);
                CHECK(FRAME(.50, true) == peak);
            }
        }
        glDeleteTextures(1, &ALT_SOURCE);
        glDeleteTextures(1, &ALT_DEST);
        Material burn_material = default_material(3);
        burn_material.burn_up = true;
        // Changing window content *behind* an opaque top surface must not
        // become visible at the curled burn edge or during reconstruction.
        const auto BURN_FRAME = [&](double progress) {
            CHECK(draw(renderer,
                BURN_SOURCE,
                BURN_DESTINATION,
                OUTPUT,
                FBO,
                WIDTH,
                HEIGHT,
                progress,
                Direction::Forward,
                burn_material,
                burn.planes));
            return read_output(FBO, WIDTH, HEIGHT);
        };
        const Pixels BEFORE_CHAR = BURN_FRAME(.30);
        const Pixels BEFORE_COOLING = BURN_FRAME(.85);
        std::array<unsigned char, 16 * 12 * 4> hidden{};
        hidden.fill(123);
        for (const GLuint PLANE : {burn.planes.source_windows, burn.planes.destination_windows}) {
            glBindTexture(GL_TEXTURE_2D, PLANE);
            glTexSubImage2D(
                GL_TEXTURE_2D, 0, 55, 37, 16, 12, GL_RGBA, GL_UNSIGNED_BYTE, hidden.data());
        }
        CHECK(BURN_FRAME(.30) == BEFORE_CHAR);
        CHECK(BURN_FRAME(.85) == BEFORE_COOLING);
        check_binding_restore(renderer,
            BURN_SOURCE,
            BURN_DESTINATION,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            burn_material,
            burn.planes);
        glDeleteTextures(1, &BURN_DESTINATION);
        glDeleteTextures(1, &BURN_SOURCE);

        // A slab is the whole captured desktop, not two static lands separated by a wipe.
        // Even uniform regions must be deformed across the molten peak in both directions;
        // the four models retain their own morphology and settings.
        std::array<Pixels, 4> variant_frames;
        for (int variant = 0; variant != 4; ++variant) {
            const Material MATERIAL = default_material(variant);
            Pixels early;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.22,
                &early);
            CHECK(difference_count(early, SOURCE) > static_cast<std::size_t>(WIDTH * HEIGHT / 16));
            CHECK(difference_count(early, DESTINATION) >
                  static_cast<std::size_t>(WIDTH * HEIGHT / 16));

            for (const Direction DIRECTION : {Direction::Forward, Direction::Backward}) {
                Pixels peak;
                run_case(
                    renderer, WIDTH, HEIGHT, SOURCE, DESTINATION, MATERIAL, DIRECTION, 0.5, &peak);
                CHECK(
                    difference_count(peak, SOURCE) > static_cast<std::size_t>(WIDTH * HEIGHT / 8));
                CHECK(difference_count(peak, DESTINATION) >
                      static_cast<std::size_t>(WIDTH * HEIGHT / 8));
                CHECK(matching_fraction(peak, SOURCE, {.left = 0, .right = WIDTH}) < 0.10);
                CHECK(matching_fraction(peak, DESTINATION, {.left = 0, .right = WIDTH}) < 0.10);
                if (DIRECTION == Direction::Forward) {
                    variant_frames[static_cast<std::size_t>(variant)] = std::move(peak);
                }
            }

            // The outgoing composition is still exact at zero; at .99 it is almost wholly
            // consumed, and the incoming composition is already reconstructed before handoff.
            Pixels start;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.0,
                &start);
            CHECK(start == SOURCE);
            Pixels near_end;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.99,
                &near_end);
            CHECK(matching_fraction(near_end, DESTINATION, {.left = 0, .right = WIDTH}) >= 0.99);
            Pixels backward_near_end;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Backward,
                0.99,
                &backward_near_end);
            CHECK(matching_fraction(backward_near_end, DESTINATION, {.left = 0, .right = WIDTH}) >=
                  0.99);
            Pixels backward_start;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Backward,
                0.0,
                &backward_start);
            CHECK(backward_start == SOURCE);
            Pixels backward_end;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Backward,
                1.0,
                &backward_end);
            CHECK(backward_end == DESTINATION);
            Pixels end;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                1.0,
                &end);
            CHECK(end == DESTINATION);
        }
        for (int first = 0; first != 4; ++first) {
            for (int second = first + 1; second != 4; ++second) {
                check_pixel_diff(
                    variant_frames[first], variant_frames[second], "variant morphology", 96);
            }
        }
        // Paired captures differ only by one light ink patch. Subtracting the plain render cancels
        // material lighting and geometry, so its centroid follows source pigment instead of a
        // static screen-space color wipe. Search progress coarsely until the visible ink dissolves.
        const PigmentTarget TARGET{.plain = EMPTY_TEXTURE,
            .destination = EMPTY_DESTINATION_TEXTURE,
            .output = OUTPUT,
            .fbo = FBO};
        check_pigment_transport(renderer, EMPTY, TARGET, Direction::Forward);
        check_pigment_transport(renderer, EMPTY, TARGET, Direction::Backward);
        check_destination_pigment_transport(renderer, SOURCE, DESTINATION, Direction::Forward);
        check_destination_pigment_transport(renderer, SOURCE, DESTINATION, Direction::Backward);

        check_controls(renderer, SOURCE, DESTINATION);
        const Material BASE = default_material(3);

        // Forward/backward signs are explicit; retracing the same two immutable captures and
        // permanent direction back to the same position reproduces the exact stateless frame.
        for (int variant = 0; variant != 4; ++variant) {
            Pixels forward;
            Pixels backward;
            Pixels intermediate;
            Pixels retraced;
            const Material MATERIAL = default_material(variant);
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.31,
                &forward);
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Backward,
                0.31,
                &backward);
            check_pixel_diff(forward, backward, "direction sign", 32);
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.82,
                &intermediate);
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE,
                DESTINATION,
                MATERIAL,
                Direction::Forward,
                0.31,
                &retraced);
            CHECK(retraced == forward);
        }

        // Raw GLES behavior is independent of desktop aspect ratio; these catches coordinate
        // normalization accidentally using one fixed resolution.
        for (const auto &[WIDTH, HEIGHT] :
            {std::pair{160, 64}, std::pair{80, 128}, std::pair{96, 96}}) {
            const Pixels SOURCE_PIXELS = desktop(2, WIDTH, HEIGHT);
            const Pixels DESTINATION_PIXELS = desktop(3, WIDTH, HEIGHT);
            Pixels interior;
            run_case(renderer,
                WIDTH,
                HEIGHT,
                SOURCE_PIXELS,
                DESTINATION_PIXELS,
                default_material(2),
                Direction::Forward,
                0.48,
                &interior);
            CHECK(difference_count(interior, SOURCE_PIXELS) >
                  static_cast<std::size_t>(WIDTH * HEIGHT / 12));
        }

        check_gl_contract(
            renderer, SOURCE_TEXTURE, DESTINATION_TEXTURE, OUTPUT, FBO, burn.planes, BASE);
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &OUTPUT);
        glDeleteTextures(1, &EMPTY_TEXTURE);
        glDeleteTextures(1, &EMPTY_DESTINATION_TEXTURE);
        glDeleteTextures(1, &IDENTICAL_TEXTURE);
        glDeleteTextures(1, &DESTINATION_TEXTURE);
        glDeleteTextures(1, &SOURCE_TEXTURE);
        renderer.release();
    }

    void run_optional_mode(
        int width, int height, bool profile, const std::filesystem::path &capture_dir) {
        const gl_harness::CContext CONTEXT(width, height);
        CHECK(CONTEXT.valid());
        if (!CONTEXT.valid()) {
            return;
        }
        Renderer renderer;
        CHECK(renderer.initialize());
        if (g_failures != 0) {
            renderer.release();
            return;
        }
        BurnFixture BURN = burn_fixture(width, height);
        const Pixels &SOURCE_PIXELS = BURN.source;
        const Pixels &DESTINATION_PIXELS = BURN.destination;
        const GLuint SOURCE = make_texture(width, height, SOURCE_PIXELS);
        const GLuint DESTINATION = make_texture(width, height, DESTINATION_PIXELS);
        const GLuint OUTPUT = output_texture(width, height);
        const GLuint FBO = framebuffer_for(OUTPUT);
        if (!capture_dir.empty()) {
            std::error_code error;
            std::filesystem::create_directories(capture_dir, error);
            CHECK(!error);
        }
        if (profile) {
            constexpr int WARMUP = 12;
            constexpr int ITERATIONS = 120;
            std::cout << "renderer=" << glGetString(GL_RENDERER)
                      << " version=" << glGetString(GL_VERSION)
                      << " method=Renderer::draw_raw extent=" << width << 'x' << height
                      << " frames=" << ITERATIONS << " completed_wall_time=true\n";
            for (int mode = 0; mode != 2; ++mode) {
                for (int variant = 0; variant != 4; ++variant) {
                    Material MATERIAL = default_material(variant);
                    MATERIAL.burn_up = mode != 0;
                    for (int i = 0; i != WARMUP; ++i) {
                        const double PROGRESS = 0.01 + (0.98 * (i + 1.0) / (WARMUP + 1.0));
                        CHECK(draw(renderer,
                            SOURCE,
                            DESTINATION,
                            OUTPUT,
                            FBO,
                            width,
                            height,
                            PROGRESS,
                            Direction::Forward,
                            MATERIAL,
                            mode != 0 ? BURN.planes : WindowPlanes{}));
                    }
                    glFinish();
                    std::array<double, ITERATIONS> samples{};
                    double elapsed_ms = 0;
                    for (int i = 0; i != ITERATIONS; ++i) {
                        const double PROGRESS = 0.01 + (0.98 * (i + 1.0) / (ITERATIONS + 1.0));
                        const auto START = std::chrono::steady_clock::now();
                        CHECK(draw(renderer,
                            SOURCE,
                            DESTINATION,
                            OUTPUT,
                            FBO,
                            width,
                            height,
                            PROGRESS,
                            Direction::Forward,
                            MATERIAL,
                            mode != 0 ? BURN.planes : WindowPlanes{}));
                        glFinish();
                        auto &sample = samples[static_cast<std::size_t>(i)];
                        sample = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - START)
                                     .count();
                        elapsed_ms += sample;
                    }
                    std::ranges::sort(samples);
                    std::printf(
                        "mode=%s variant=%d mean_ms=%.4f p95_ms=%.4f max_ms=%.4f energy=%llu\n",
                        mode != 0 ? "burn_up" : "melt_with_workspace",
                        variant,
                        elapsed_ms / ITERATIONS,
                        samples[(ITERATIONS * 95 / 100) - 1],
                        samples.back(),
                        static_cast<unsigned long long>(
                            image_energy(read_output(FBO, width, height))));
                }
            }
        }
        if (!capture_dir.empty()) {
            constexpr int LAST_FRAME = 100;
            for (int mode = 0; mode != 2; ++mode) {
                for (int variant = 0; variant != 4; ++variant) {
                    Material MATERIAL = default_material(variant);
                    MATERIAL.burn_up = mode != 0;
                    for (int frame = 0; frame <= LAST_FRAME; ++frame) {
                        const double PROGRESS = static_cast<double>(frame) / LAST_FRAME;
                        CHECK(draw(renderer,
                            SOURCE,
                            DESTINATION,
                            OUTPUT,
                            FBO,
                            width,
                            height,
                            PROGRESS,
                            Direction::Forward,
                            MATERIAL,
                            mode != 0 ? BURN.planes : WindowPlanes{}));
                        const Pixels PIXELS = read_output(FBO, width, height);
                        const std::string FRAME_NUMBER = std::to_string(10000 + frame).substr(1);
                        const std::string FILENAME = "variant-" + std::to_string(variant) +
                                                     (mode != 0 ? "-burn-frame-" : "-frame-") +
                                                     FRAME_NUMBER + ".ppm";
                        CHECK(write_ppm(capture_dir / FILENAME, PIXELS, width, height));
                    }
                }
            }
            std::printf("capture=%s extent=%dx%d frames_per_combination=101 variants=4 modes=2\n",
                capture_dir.c_str(),
                width,
                height);
        }
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &OUTPUT);
        glDeleteTextures(1, &DESTINATION);
        glDeleteTextures(1, &SOURCE);
        renderer.release();
    }

    void optional_outputs(int argc, char *const *argv) {
        bool profile = false;
        std::filesystem::path capture_dir;
        for (int i = 1; i < argc; ++i) {
            const std::string_view ARGUMENT(argv[i]);
            if (ARGUMENT == "--profile") {
                profile = true;
            } else if (ARGUMENT == "--capture" && i + 1 < argc) {
                capture_dir = argv[++i];
            } else {
                std::cerr << "usage: lava-renderer-tests [--profile] [--capture DIR]\n";
                CHECK(false);
                return;
            }
        }
        if (!profile && capture_dir.empty()) {
            return;
        }
        if (!capture_dir.empty()) {
            run_optional_mode(640, 360, false, capture_dir);
        }
        if (profile) {
            run_optional_mode(1920, 1080, true, {});
        }
    }
}  // namespace

int main(int argc, char *const *argv) {
    if (argc == 1) {
        run();
    } else {
        optional_outputs(argc, argv);
    }
    return g_failures == 0 ? 0 : 1;
}
