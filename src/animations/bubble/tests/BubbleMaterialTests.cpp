// Headless pixel tests for the same material and embedded shaders used by the plugin.
// EGL_PLATFORM=surfaceless selects Mesa's offscreen platform; no compositor is needed.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include "Check.hpp"
#include "animations/bubble/BubbleMaterial.hpp"

namespace {
    constexpr int width = 256, height = 256;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;

    GLuint texture(const Image &pixels) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return id;
    }

    struct Fixture {
        CBubbleMaterial material;
        GLuint window = 0, otherWindow = 0, scene = 0, black = 0;

        Fixture() {
            Image content(width * height);
            Image alternative(width * height);
            Image background(width * height);
            Image dark(width * height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const auto i = (y * width) + x;
                    // A recognizable, asymmetric client image, distinct from the desktop.
                    content[i] = {static_cast<unsigned char>(40 + (x / 4)),
                        30,
                        static_cast<unsigned char>(110 + (y / 4)),
                        255};
                    alternative[i] = {20, 120, 30, 255};
                    // Red increases to the right, green upwards, blue marks an off-centre window
                    // behind.
                    background[i] = {static_cast<unsigned char>(25 + (x * 3 / 4)),
                        static_cast<unsigned char>(25 + (y * 3 / 4)),
                        static_cast<unsigned char>(x > 130 && y > 100 ? 210 : 20),
                        255};
                    dark[i] = {0, 0, 0, 255};
                }
            }
            window = texture(content);
            otherWindow = texture(alternative);
            scene = texture(background);
            black = texture(dark);
            CHECK(material.initialize());
        }
        ~Fixture() {
            material.release();
            const GLuint TEXTURES[] = {window, otherWindow, scene, black};
            glDeleteTextures(4, TEXTURES);
        }
        Image render(const hyprimate::BubbleFrame &frame,
            GLuint image,
            GLuint backdrop,
            hyprimate::Vec2 pos,
            hyprimate::Vec2 size) {
            SBubbleTarget const TARGET{.canvas = {.x = width, .y = height},
                .origin = {.x = 0, .y = 0},
                .sourcePos = pos,
                .sourceSize = size};
            SBubbleImages images{};
            images.window = image;
            images.textureOrigin = {.x = 0, .y = 0};
            images.textureSize = {.x = width, .y = height};
            images.wallpaper = backdrop;
            // Frame unit coordinates map to the scene at the physical source position.
            images.wallpaperOrigin = {.x = pos.x / width, .y = pos.y / height};
            images.wallpaperX = {.x = size.x / width, .y = 0};
            images.wallpaperY = {.x = 0, .y = size.y / height};
            images.wallpaperPixel = {.x = 1.0 / width, .y = 1.0 / height};
            glViewport(0, 0, width, height);
            glDisable(GL_BLEND);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            material.drawMembranes(frame, TARGET, images);
            Image result(width * height);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
            CHECK(glGetError() == GL_NO_ERROR);
            return result;
        }
    };

    int diff(const Pixel &a, const Pixel &b, int channel) {
        return static_cast<int>(a[channel]) - static_cast<int>(b[channel]);
    }
    const Pixel &at(const Image &image, int x, int y) {
        return image[(y * width) + x];
    }

    void testSettled(Fixture &f) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 120, .y = 80};
        frame.window.halfSize = {.x = 60, .y = 40};
        frame.window.visibility = 1;
        frame.window.transparency = 1;
        const auto RESULT = f.render(frame, f.window, f.scene, {.x = 50, .y = 70}, frame.size);
        // Exact native pixels, including a location away from the centre and the outer clear area.
        for (const auto [x, y] :
            std::array<std::array<int, 2>, 3>{{{55, 75}, {100, 110}, {165, 145}}}) {
            const Pixel EXPECTED{static_cast<unsigned char>(40 + (x / 4)),
                30,
                static_cast<unsigned char>(110 + (y / 4)),
                255};
            CHECK(at(RESULT, x, y) == EXPECTED);
        }
        CHECK(at(RESULT, 40, 60)[3] == 0);
    }

    void testSettling(Fixture &f) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 160, .y = 100};
        frame.window.halfSize = {.x = 80, .y = 50};
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.extent = {.x = 76, .y = 48};
        const auto SETTLED = f.render(frame, f.window, f.scene, {.x = 48, .y = 78}, frame.size);
        // Crossing from a tiny remaining contraction into the native fast path must
        // restore client pixels continuously, not abruptly swap scene and window.
        for (double const ROUND : {.004, .002, .001}) {
            frame.window.motion.round = ROUND;
            const auto SETTLING =
                f.render(frame, f.window, f.scene, {.x = 48, .y = 78}, frame.size);
            for (const auto [x, y] :
                std::array<std::array<int, 2>, 3>{{{90, 110}, {128, 128}, {170, 145}}}) {
                for (int channel = 0; channel < 4; ++channel) {
                    CHECK(std::abs(diff(at(SETTLING, x, y), at(SETTLED, x, y), channel)) <= 2);
                }
            }
        }
    }

    void testTransparency(Fixture &f) {
        hyprimate::BubbleFrame frame{};
        frame.size = {.x = 160, .y = 100};
        frame.window.halfSize = {.x = 80, .y = 50};
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.round = .5;
        frame.window.motion.extent = {.x = 76, .y = 48};
        const hyprimate::Vec2 POS{.x = 48, .y = 78};
        frame.window.transparency = 0;
        const auto OPAQUE = f.render(frame, f.window, f.scene, POS, frame.size);
        CHECK(OPAQUE == f.render(frame, f.window, f.black, POS, frame.size));
        frame.window.transparency = 1;
        const auto CLEAR = f.render(frame, f.window, f.scene, POS, frame.size);
        CHECK(CLEAR == f.render(frame, f.otherWindow, f.scene, POS, frame.size));
        frame.window.transparency = .5;
        const auto HALFWAY = f.render(frame, f.window, f.scene, POS, frame.size);
        for (int channel = 0; channel < 3; ++channel) {
            const int AVERAGE =
                (static_cast<int>(at(OPAQUE, 128, 128)[channel]) + at(CLEAR, 128, 128)[channel]) /
                2;
            CHECK(std::abs(int(at(HALFWAY, 128, 128)[channel]) - AVERAGE) <= 1);
        }
        CHECK(OPAQUE != CLEAR);
        // This changes the transmitted imagery, not the geometry or film coverage.
        for (std::size_t i = 0; i < OPAQUE.size(); ++i) {
            CHECK(OPAQUE[i][3] == CLEAR[i][3]);
        }
    }

    void testMoving(Fixture &f, int w, int h, double round, hyprimate::Vec2 extent) {
        const hyprimate::Vec2 POS{.x = (width - w) / 2.0, .y = (height - h) / 2.0};
        hyprimate::BubbleFrame frame{};
        frame.size = {static_cast<double>(w), static_cast<double>(h)};
        frame.window.halfSize = {.x = w / 2.0, .y = h / 2.0};
        frame.window.corner = 4;
        frame.window.visibility = 1;
        frame.window.thickness = 400;
        frame.window.motion.round = round;
        frame.window.motion.extent = extent;
        frame.window.motion.drag = {.x = 5, .y = -3};
        frame.window.motion.carry = {.x = 3, .y = 2};
        const auto DESKTOP = f.render(frame, f.window, f.scene, POS, frame.size);
        const auto DARK = f.render(frame, f.window, f.black, POS, frame.size);
        const auto DIFFERENT_WINDOW = f.render(frame, f.otherWindow, f.scene, POS, frame.size);
        const int cx = width / 2 + 3;
        const int cy = height / 2 + 2;
        // A changing desktop behind the same client must be visible in the moving film.
        int desktopResponse = 0;
        int windowResponse = 0;
        for (int c = 0; c < 3; ++c) {
            desktopResponse += std::abs(diff(at(DESKTOP, cx, cy), at(DARK, cx, cy), c));
            windowResponse += std::abs(diff(at(DESKTOP, cx, cy), at(DIFFERENT_WINDOW, cx, cy), c));
        }
        CHECK(desktopResponse > 8);
        CHECK(windowResponse > 8);
        CHECK(desktopResponse > windowResponse);
        // Source orientation: compare the response to the scene, cancelling the unchanged window
        // and film.
        const int dx = int(extent.x * .32);
        const int dy = int(extent.y * .32);
        const auto RESPONSE = [&](int x, int y, int c) {
            return diff(at(DESKTOP, x, y), at(DARK, x, y), c);
        };
        CHECK(RESPONSE(cx + dx, cy, 0) > RESPONSE(cx - dx, cy, 0) + 2);
        CHECK(RESPONSE(cx, cy + dy, 1) > RESPONSE(cx, cy - dy, 1) + 2);
        // A blue rectangle represents a second window behind the bubble, not wallpaper alone.
        CHECK(RESPONSE(cx + dx, cy + dy, 2) > RESPONSE(cx - dx, cy + dy, 2) + 20);
        // Colour changes cannot alter the silhouette; interior and well outside remain distinct.
        CHECK(at(DESKTOP, cx, cy)[3] > 128);
        // Moving the same film over the screen changes the desktop under its centre.
        const hyprimate::Vec2 SHIFTED{.x = POS.x - 20, .y = POS.y - 20};
        const auto SHIFTED_SCENE = f.render(frame, f.otherWindow, f.scene, SHIFTED, frame.size);
        const auto SHIFTED_DARK = f.render(frame, f.otherWindow, f.black, SHIFTED, frame.size);
        const auto ORIGINAL_SCENE = f.render(frame, f.otherWindow, f.scene, POS, frame.size);
        const auto ORIGINAL_DARK = f.render(frame, f.otherWindow, f.black, POS, frame.size);
        CHECK(diff(at(ORIGINAL_SCENE, cx, cy), at(ORIGINAL_DARK, cx, cy), 0) >
              diff(at(SHIFTED_SCENE, cx - 20, cy - 20), at(SHIFTED_DARK, cx - 20, cy - 20), 0) + 2);
        CHECK(diff(at(ORIGINAL_SCENE, cx, cy), at(ORIGINAL_DARK, cx, cy), 1) >
              diff(at(SHIFTED_SCENE, cx - 20, cy - 20), at(SHIFTED_DARK, cx - 20, cy - 20), 1) + 2);
        CHECK(at(DESKTOP, 2, 2)[3] == 0);
        bool sameSilhouette = true;
        for (std::size_t i = 0; i < DESKTOP.size(); ++i) {
            sameSilhouette &= (DESKTOP[i][3] > 8) == (DARK[i][3] > 8);
        }
        CHECK(sameSilhouette);
        // The GPU outline must still enclose the CPU motion model's axis-edge points.
        for (int axis = 0; axis < 2; ++axis) {
            for (int const SIGN : {-1, 1}) {
                hyprimate::Vec2 edge{};
                if (axis == 0) {
                    edge.x = SIGN * frame.window.halfSize.x;
                } else {
                    edge.y = SIGN * frame.window.halfSize.y;
                }
                const auto MAPPED =
                    hyprimate::bubbleMotionPoint(frame.window.halfSize, frame.window.motion, edge);
                const int x = static_cast<int>(std::lround((width / 2.0) + MAPPED.x));
                const int y = static_cast<int>(std::lround((height / 2.0) + MAPPED.y));
                const int ox = axis == 0 ? SIGN * 8 : 0;
                const int oy = axis == 1 ? SIGN * 8 : 0;
                CHECK(at(DESKTOP, x - ox, y - oy)[3] > 128);
                CHECK(at(DESKTOP, x + ox, y + oy)[3] < 8);
            }
        }
    }
}  // namespace

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || (eglInitialize(display, &major, &minor) == 0u) ||
        (eglBindAPI(EGL_OPENGL_ES_API) == 0u)) {
        std::fprintf(stderr, "Cannot initialize offscreen EGL (try EGL_PLATFORM=surfaceless)\n");
        return 1;
    }
    const EGLint CONFIG_ATTRS[] = {EGL_SURFACE_TYPE,
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
        EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if ((eglChooseConfig(display, CONFIG_ATTRS, &config, 1, &count) == 0u) || count != 1) {
        return 1;
    }
    const EGLint SURFACE_ATTRS[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    const EGLint CONTEXT_ATTRS[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, SURFACE_ATTRS);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, CONTEXT_ATTRS);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        (eglMakeCurrent(display, surface, surface, context) == 0u)) {
        std::fprintf(stderr, "Cannot create GLES 3 offscreen context\n");
        return 1;
    }
    {
        Fixture fixture;
        testSettled(fixture);
        testSettling(fixture);
        testTransparency(fixture);
        testMoving(fixture, 160, 100, .35, {.x = 76, .y = 48});  // pillow, wide
        testMoving(fixture, 100, 160, .75, {.x = 46, .y = 74});  // ellipse, tall
        testMoving(fixture, 150, 90, .85, {.x = 48, .y = 48});  // disc
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return g_failures == 0 ? 0 : 1;
}
