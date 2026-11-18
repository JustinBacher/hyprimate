// Headless pixel tests for the same material and embedded shaders used by the plugin.
// EGL_PLATFORM=surfaceless selects Mesa's offscreen platform; no compositor is needed.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <GLES3/gl32.h>

#include "Check.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/shared/Core.hpp"

namespace {
    constexpr int WIDTH = 256, HEIGHT = 256;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;

    struct TexturePair {
        GLuint image;
        GLuint backdrop;
    };

    GLuint texture(const Image &pixels) {
        GLuint texture_id = 0;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return texture_id;
    }

    struct Fixture {
        CBubbleMaterial material;
        GLuint window = 0, other_window = 0, scene = 0, black = 0;

        Fixture() {
            Image content(static_cast<std::size_t>(WIDTH) * HEIGHT);
            Image alternative(static_cast<std::size_t>(WIDTH) * HEIGHT);
            Image background(static_cast<std::size_t>(WIDTH) * HEIGHT);
            Image dark(static_cast<std::size_t>(WIDTH) * HEIGHT);
            auto content_pixel = content.begin();
            auto alternative_pixel = alternative.begin();
            auto background_pixel = background.begin();
            auto dark_pixel = dark.begin();
            for (int row = 0; row < HEIGHT; ++row) {
                for (int col = 0; col < WIDTH; ++col) {
                    // A recognizable, asymmetric client image, distinct from the desktop.
                    *content_pixel++ = {
                        static_cast<unsigned char>(40 + (col / 4)),
                        30,
                        static_cast<unsigned char>(110 + (row / 4)),
                        255,
                    };
                    *alternative_pixel++ = {20, 120, 30, 255};
                    // Red increases to the right, green upwards, blue marks an off-centre window
                    // behind.
                    *background_pixel++ = {
                        static_cast<unsigned char>(25 + (col * 3 / 4)),
                        static_cast<unsigned char>(25 + (row * 3 / 4)),
                        static_cast<unsigned char>(col > 130 && row > 100 ? 210 : 20),
                        255,
                    };
                    *dark_pixel++ = {0, 0, 0, 255};
                }
            }
            window = texture(content);
            other_window = texture(alternative);
            scene = texture(background);
            black = texture(dark);
            CHECK(material.initialize());
        }
        Fixture(const Fixture &) = delete;
        Fixture &operator=(const Fixture &) = delete;
        Fixture(Fixture &&) = delete;
        Fixture &operator=(Fixture &&) = delete;
        ~Fixture() {
            material.release();
            const std::array<GLuint, 4> TEXTURES{window, other_window, scene, black};
            glDeleteTextures(4, TEXTURES.data());
        }
        Image render(const hyprimate::BubbleFrame &frame,
            TexturePair textures,
            hyprimate::Vec2 pos,
            hyprimate::Vec2 size) {
            SBubbleTarget const TARGET{
                .canvas = {.x = WIDTH, .y = HEIGHT},
                .origin = {.x = 0, .y = 0},
                .source_pos = pos,
                .source_size = size,
            };
            SBubbleImages images{};
            images.window = textures.image;
            images.texture_origin = {.x = 0, .y = 0};
            images.texture_size = {.x = WIDTH, .y = HEIGHT};
            images.wallpaper = textures.backdrop;
            // Frame unit coordinates map to the scene at the physical source position.
            images.wallpaper_origin = {.x = pos.x / WIDTH, .y = pos.y / HEIGHT};
            images.wallpaper_x = {.x = size.x / WIDTH, .y = 0};
            images.wallpaper_y = {.x = 0, .y = size.y / HEIGHT};
            images.wallpaper_pixel = {.x = 1.0 / WIDTH, .y = 1.0 / HEIGHT};
            glViewport(0, 0, WIDTH, HEIGHT);
            glDisable(GL_BLEND);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            material.draw_membranes(frame, TARGET, images);
            Image result(static_cast<std::size_t>(WIDTH) * HEIGHT);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
            CHECK(glGetError() == GL_NO_ERROR);
            return result;
        }
    };

    int diff(const Pixel &first, const Pixel &second, int channel) {
        return static_cast<int>(first[channel]) - static_cast<int>(second[channel]);
    }
    const Pixel &at(const Image &image, int column, int row) {
        return image[(row * WIDTH) + column];
    }

    void test_settled(Fixture &fixture) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 120, .y = 80};
        frame.window.half_size = {.x = 60, .y = 40};
        frame.window.visibility = 1;
        frame.window.transparency = 1;
        const auto RESULT = fixture.render(frame,
            {.image = fixture.window, .backdrop = fixture.scene},
            {.x = 50, .y = 70},
            frame.size);
        // Exact native pixels, including a location away from the centre and the outer clear area.
        for (const auto [PIXEL_X, PIXEL_Y] :
            std::array<std::array<int, 2>, 3>{{{55, 75}, {100, 110}, {165, 145}}}) {
            const Pixel EXPECTED{
                static_cast<unsigned char>(40 + (PIXEL_X / 4)),
                30,
                static_cast<unsigned char>(110 + (PIXEL_Y / 4)),
                255,
            };
            CHECK(at(RESULT, PIXEL_X, PIXEL_Y) == EXPECTED);
        }
        CHECK(std::get<3>(at(RESULT, 40, 60)) == 0);
        // A companion behind the settled client must not show through it.
        auto &satellite = std::get<0>(frame.formation);
        frame.formation_count = 1;
        satellite.radius = 20;
        satellite.growth = satellite.visibility = 1;
        satellite.thickness = 450;
        CHECK(RESULT == fixture.render(frame,
            {.image = fixture.window, .backdrop = fixture.scene},
            {.x = 50, .y = 70}, frame.size));
    }

    void test_settling(Fixture &fixture) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 160, .y = 100};
        frame.window.half_size = {.x = 80, .y = 50};
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.extent = {.x = 76, .y = 48};
        const auto SETTLED = fixture.render(frame,
            {.image = fixture.window, .backdrop = fixture.scene},
            {.x = 48, .y = 78},
            frame.size);
        // Crossing from a tiny remaining contraction into the native fast path must
        // restore client pixels continuously, not abruptly swap scene and window.
        for (double const ROUND : {.004, .002, .001}) {
            frame.window.motion.round = ROUND;
            const auto SETTLING = fixture.render(frame,
                {.image = fixture.window, .backdrop = fixture.scene},
                {.x = 48, .y = 78},
                frame.size);
            for (const auto [PIXEL_X, PIXEL_Y] :
                std::array<std::array<int, 2>, 3>{{{90, 110}, {128, 128}, {170, 145}}}) {
                for (int channel = 0; channel < 4; ++channel) {
                    CHECK(std::abs(diff(at(SETTLING, PIXEL_X, PIXEL_Y),
                              at(SETTLED, PIXEL_X, PIXEL_Y),
                              channel)) <= 2);
                }
            }
        }
    }

    void test_transparency(Fixture &fixture) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 160, .y = 100};
        frame.window.half_size = {.x = 80, .y = 50};
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.round = .5;
        frame.window.motion.extent = {.x = 76, .y = 48};
        const hyprimate::Vec2 POS{.x = 48, .y = 78};
        frame.window.transparency = 0;
        const auto OPAQUE = fixture.render(
            frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        CHECK(OPAQUE ==
              fixture.render(frame, (TexturePair{fixture.window, fixture.black}), POS, frame.size));
        frame.window.transparency = 1;
        const auto CLEAR = fixture.render(
            frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        CHECK(CLEAR ==
              fixture.render(
                  frame, (TexturePair{fixture.other_window, fixture.scene}), POS, frame.size));
        frame.window.transparency = .5;
        const auto HALFWAY = fixture.render(
            frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        const auto *opaque_channel = at(OPAQUE, 128, 128).begin();
        const auto *clear_channel = at(CLEAR, 128, 128).begin();
        const auto *halfway_channel = at(HALFWAY, 128, 128).begin();
        for (int channel = 0; channel < 3; ++channel, ++halfway_channel) {
            const int AVERAGE = (static_cast<int>(*opaque_channel++) + *clear_channel++) / 2;
            CHECK(std::abs(int(*halfway_channel) - AVERAGE) <= 1);
        }
        CHECK(OPAQUE != CLEAR);
        // This changes the transmitted imagery, not the geometry or film coverage.
        auto clear_pixel = CLEAR.begin();
        for (const auto &pixel : OPAQUE) {
            CHECK(std::get<3>(pixel) == std::get<3>(*clear_pixel));
            ++clear_pixel;
        }
    }

    struct WindowDimensions {
        int width;
        int height;
    };

    void test_moving(
        Fixture &fixture, WindowDimensions dimensions, double round, hyprimate::Vec2 extent) {
        const hyprimate::Vec2 POS{
            .x = (WIDTH - dimensions.width) / 2.0,
            .y = (HEIGHT - dimensions.height) / 2.0,
        };
        hyprimate::BubbleFrame frame{};
        frame.size = {
            .x = static_cast<double>(dimensions.width),
            .y = static_cast<double>(dimensions.height),
        };
        frame.window.half_size = {.x = dimensions.width / 2.0, .y = dimensions.height / 2.0};
        frame.window.corner = 4;
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.round = round;
        frame.window.motion.extent = extent;
        frame.window.motion.drag = {.x = 5, .y = -3};
        frame.window.motion.carry = {.x = 3, .y = 2};
        const auto DESKTOP = fixture.render(
            frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        const auto DARK = fixture.render(
            frame, {.image = fixture.window, .backdrop = fixture.black}, POS, frame.size);
        const auto DIFFERENT_WINDOW = fixture.render(
            frame, {.image = fixture.other_window, .backdrop = fixture.scene}, POS, frame.size);
        const int CENTER_X = (WIDTH / 2) + 3;
        const int CENTER_Y = (HEIGHT / 2) + 2;
        // A changing desktop behind the same client must be visible in the moving film.
        int desktop_response = 0;
        int window_response = 0;
        for (int channel = 0; channel < 3; ++channel) {
            desktop_response += std::abs(
                diff(at(DESKTOP, CENTER_X, CENTER_Y), at(DARK, CENTER_X, CENTER_Y), channel));
            window_response += std::abs(diff(at(DESKTOP, CENTER_X, CENTER_Y),
                at(DIFFERENT_WINDOW, CENTER_X, CENTER_Y),
                channel));
        }
        CHECK(desktop_response > 8);
        CHECK(window_response > 8);
        CHECK(desktop_response > window_response);
        // Source orientation: compare the response to the scene, cancelling the unchanged window
        // and film.
        const int SAMPLE_OFFSET_X = static_cast<int>(extent.x * .32);
        const int SAMPLE_OFFSET_Y = static_cast<int>(extent.y * .32);
        const auto RESPONSE = [&](int column, int row, int channel) {
            return diff(at(DESKTOP, column, row), at(DARK, column, row), channel);
        };
        CHECK(RESPONSE(CENTER_X + SAMPLE_OFFSET_X, CENTER_Y, 0) >
              RESPONSE(CENTER_X - SAMPLE_OFFSET_X, CENTER_Y, 0) + 2);
        CHECK(RESPONSE(CENTER_X, CENTER_Y + SAMPLE_OFFSET_Y, 1) >
              RESPONSE(CENTER_X, CENTER_Y - SAMPLE_OFFSET_Y, 1) + 2);
        // A blue rectangle represents a second window behind the bubble, not wallpaper alone.
        CHECK(RESPONSE(CENTER_X + SAMPLE_OFFSET_X, CENTER_Y + SAMPLE_OFFSET_Y, 2) >
              RESPONSE(CENTER_X - SAMPLE_OFFSET_X, CENTER_Y + SAMPLE_OFFSET_Y, 2) + 20);
        // Colour changes cannot alter the silhouette; interior and well outside remain distinct.
        CHECK(std::get<3>(at(DESKTOP, CENTER_X, CENTER_Y)) > 128);
        // Moving the same film over the screen changes the desktop under its centre.
        const hyprimate::Vec2 SHIFTED{.x = POS.x - 20, .y = POS.y - 20};
        const auto SHIFTED_SCENE = fixture.render(
            frame, {.image = fixture.other_window, .backdrop = fixture.scene}, SHIFTED, frame.size);
        const auto SHIFTED_DARK = fixture.render(
            frame, {.image = fixture.other_window, .backdrop = fixture.black}, SHIFTED, frame.size);
        const auto ORIGINAL_SCENE = fixture.render(
            frame, {.image = fixture.other_window, .backdrop = fixture.scene}, POS, frame.size);
        const auto ORIGINAL_DARK = fixture.render(
            frame, {.image = fixture.other_window, .backdrop = fixture.black}, POS, frame.size);
        CHECK(
            diff(at(ORIGINAL_SCENE, CENTER_X, CENTER_Y), at(ORIGINAL_DARK, CENTER_X, CENTER_Y), 0) >
            diff(at(SHIFTED_SCENE, CENTER_X - 20, CENTER_Y - 20),
                at(SHIFTED_DARK, CENTER_X - 20, CENTER_Y - 20),
                0) +
                2);
        CHECK(
            diff(at(ORIGINAL_SCENE, CENTER_X, CENTER_Y), at(ORIGINAL_DARK, CENTER_X, CENTER_Y), 1) >
            diff(at(SHIFTED_SCENE, CENTER_X - 20, CENTER_Y - 20),
                at(SHIFTED_DARK, CENTER_X - 20, CENTER_Y - 20),
                1) +
                2);
        CHECK(std::get<3>(at(DESKTOP, 2, 2)) == 0);
        bool same_silhouette = true;
        auto dark_pixel = DARK.begin();
        for (const auto &pixel : DESKTOP) {
            same_silhouette &= (std::get<3>(pixel) > 8) == (std::get<3>(*dark_pixel++) > 8);
        }
        CHECK(same_silhouette);
        // The GPU outline must still enclose the CPU motion model's axis-edge points.
        for (int axis = 0; axis < 2; ++axis) {
            for (int const SIGN : {-1, 1}) {
                hyprimate::Vec2 edge{};
                if (axis == 0) {
                    edge.x = SIGN * frame.window.half_size.x;
                } else {
                    edge.y = SIGN * frame.window.half_size.y;
                }
                const auto MAPPED = hyprimate::bubble_motion_point(
                    frame.window.half_size, frame.window.motion, edge);
                const int PIXEL_X = static_cast<int>(std::lround((WIDTH / 2.0) + MAPPED.x));
                const int PIXEL_Y = static_cast<int>(std::lround((HEIGHT / 2.0) + MAPPED.y));
                const int OFFSET_X = axis == 0 ? SIGN * 8 : 0;
                const int OFFSET_Y = axis == 1 ? SIGN * 8 : 0;
                CHECK(std::get<3>(at(DESKTOP, PIXEL_X - OFFSET_X, PIXEL_Y - OFFSET_Y)) > 128);
                CHECK(std::get<3>(at(DESKTOP, PIXEL_X + OFFSET_X, PIXEL_Y + OFFSET_Y)) < 8);
            }
        }
    }
    // A bud swells the drawn outline exactly where the CPU motion model puts it.
    void test_bud(Fixture &fixture) {
        constexpr int W = 160, H = 100;
        const hyprimate::Vec2 POS{.x = (WIDTH - W) / 2.0, .y = (HEIGHT - H) / 2.0};
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = W, .y = H};
        frame.window.half_size = {.x = W / 2.0, .y = H / 2.0};
        frame.window.corner = 4;
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.round = .3;
        frame.window.motion.extent = {.x = W / 2.0 * .92, .y = H / 2.0 * .92};
        const hyprimate::Vec2 TOP{.x = 20, .y = -H / 2.0};
        auto &bud = std::get<0>(frame.window.motion.buds);
        bud.foot = hyprimate::bubble_round_point(frame.window.half_size, frame.window.motion, TOP);
        bud.width = 12;
        bud.height = 6;
        const auto SWOLLEN = fixture.render(frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        const auto CREST = hyprimate::bubble_motion_point(frame.window.half_size, frame.window.motion, TOP);
        const int X = static_cast<int>(std::lround(WIDTH / 2.0 + CREST.x));
        const int Y = static_cast<int>(std::lround(HEIGHT / 2.0 + CREST.y));
        CHECK(std::get<3>(at(SWOLLEN, X, Y + 2)) > 128);
        CHECK(std::get<3>(at(SWOLLEN, X, Y - 3)) < 8);
        bud = {};
        const auto FLAT = fixture.render(frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
        CHECK(std::get<3>(at(FLAT, X, Y + 2)) < 8);
    }

    void test_companion_optics(Fixture &fixture) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 128, .y = 128};
        frame.formation_count = 1;
        auto &bubble = std::get<0>(frame.formation);
        bubble.growth = bubble.visibility = 1;
        bubble.thickness = 650;
        bubble.seed = .3;
        const hyprimate::Vec2 POS{.x = 64, .y = 64};
        for (const double RADIUS : {3.0, 8.0, 24.0, 48.0}) {
            bubble.radius = RADIUS;
            const auto LAYER = fixture.render(frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
            CHECK(LAYER == fixture.render(frame, {.image = fixture.window, .backdrop = fixture.black}, POS, frame.size));
            CHECK(LAYER == fixture.render(frame, {.image = fixture.other_window, .backdrop = 0}, POS, frame.size));
            double centre = 0, ring = 0, ring_min = 1e9, ring_max = 0;
            int centre_count = 0, ring_count = 0;
            for (int y = 0; y < HEIGHT; ++y) {
                for (int x = 0; x < WIDTH; ++x) {
                    const double DISTANCE = std::hypot(x + .5 - 128, y + .5 - 128) / RADIUS;
                    const double ALPHA = std::get<3>(at(LAYER, x, y));
                    if (DISTANCE < .4) {
                        centre += ALPHA;
                        ++centre_count;
                    }
                    if (DISTANCE > 1 - 1.5 / RADIUS && DISTANCE < 1 - .5 / RADIUS) {
                        ring += ALPHA;
                        ++ring_count;
                        const double LIGHT = std::get<0>(at(LAYER, x, y)) + std::get<1>(at(LAYER, x, y)) + std::get<2>(at(LAYER, x, y));
                        ring_min = std::min(ring_min, LIGHT);
                        ring_max = std::max(ring_max, LIGHT);
                    }
                    if (DISTANCE > 1 + 1.5 / RADIUS) {
                        CHECK(ALPHA == 0);
                    }
                }
            }
            if (centre_count > 0) {
                CHECK(centre / centre_count < 255 * .25);
            }
            CHECK(ring_count > 0 && ring > 0);
            if (RADIUS >= 8) {
                CHECK(ring / ring_count > 1.5 * centre / centre_count);
            }
            CHECK(ring / ring_count < 255 * .8);
            if (RADIUS >= 24) {
                CHECK(ring_max > ring_min + 12);
            }
        }
        // Subpixel motion of a tiny bubble should not flicker.
        bubble.radius = 2.5;
        double first = 0, lowest = 1e9, highest = 0;
        for (int i = 0; i < 8; ++i) {
            bubble.centre = {.x = i * .125, .y = i * .0625};
            const auto LAYER = fixture.render(frame, {.image = fixture.window, .backdrop = fixture.scene}, POS, frame.size);
            double total = 0;
            for (const auto &pixel : LAYER) {
                total += std::get<3>(pixel);
            }
            if (i == 0) {
                first = total;
            }
            lowest = std::min(lowest, total);
            highest = std::max(highest, total);
        }
        CHECK(first > 0 && highest < lowest * 1.15);
    }

    void test_formation(Fixture &fixture) {
        for (const hyprimate::Vec2 SIZE : {hyprimate::Vec2{.x = 64, .y = 180},
                 hyprimate::Vec2{.x = 200, .y = 60}, hyprimate::Vec2{.x = 150, .y = 150},
                 hyprimate::Vec2{.x = 180, .y = 126}}) {
            const hyprimate::Vec2 POS{.x = (WIDTH - SIZE.x) / 2, .y = (HEIGHT - SIZE.y) / 2};
            hyprimate::Bubble bubble;
            bubble.begin(hyprimate::Transition::Open, hyprimate::BubbleDuration{1}, 42, SIZE);
            bool companions_drawn = false, companions_after_landing = false;
            for (int step = 0; step < 100; ++step) {
                bubble.advance(.01);
                if (step % 5 != 4) {
                    continue;
                }
                const auto FRAME = bubble.frame(SIZE);
                const auto DRAWN = fixture.render(FRAME, {.image = fixture.window, .backdrop = fixture.scene}, POS, SIZE);
                auto primary = FRAME;
                primary.formation_count = 0;
                const auto WITHOUT = fixture.render(primary, {.image = fixture.window, .backdrop = fixture.scene}, POS, SIZE);
                companions_drawn |= DRAWN != WITHOUT;
                if (bubble.landed() && DRAWN != WITHOUT) {
                    companions_after_landing = true;
                }
                if (step == 99) {
                    CHECK(DRAWN == WITHOUT);
                }
                auto satellites = FRAME;
                satellites.window.visibility = 0;
                const auto SCENE = fixture.render(satellites, {.image = fixture.window, .backdrop = fixture.scene}, POS, SIZE);
                CHECK(SCENE == fixture.render(satellites, {.image = fixture.other_window, .backdrop = fixture.black}, POS, SIZE));
                CHECK(!glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_BLEND));
            }
            CHECK(companions_drawn);
            CHECK(companions_after_landing);
        }
    }
}  // namespace

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || (eglInitialize(display, &major, &minor) == 0U) ||
        (eglBindAPI(EGL_OPENGL_ES_API) == 0U)) {
        (void)std::fprintf(stderr, "Cannot initialize offscreen EGL (try EGL_PLATFORM=surfaceless)\n");
        return 1;
    }
    const std::array<EGLint, 13> CONFIG_ATTRS{
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_NONE,
    };
    EGLConfig config{};
    EGLint count = 0;
    if ((eglChooseConfig(display, CONFIG_ATTRS.data(), &config, 1, &count) == 0U) || count != 1) {
        return 1;
    }
    const std::array<EGLint, 5> SURFACE_ATTRS{EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
    const std::array<EGLint, 3> CONTEXT_ATTRS{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, SURFACE_ATTRS.data());
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, CONTEXT_ATTRS.data());
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        (eglMakeCurrent(display, surface, surface, context) == 0U)) {
        (void)std::fprintf(stderr, "Cannot create GLES 3 offscreen context\n");
        return 1;
    }
    {
        Fixture fixture;
        test_settled(fixture);
        test_settling(fixture);
        test_transparency(fixture);
        test_formation(fixture);
        test_companion_optics(fixture);
        test_moving(
            fixture, {.width = 160, .height = 100}, .35, {.x = 76, .y = 48});
        test_moving(
            fixture, {.width = 100, .height = 160}, .75, {.x = 46, .y = 74});
        test_moving(fixture, {.width = 150, .height = 90}, .85, {.x = 48, .y = 48});
        test_bud(fixture);
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return g_failures == 0 ? 0 : 1;
}
