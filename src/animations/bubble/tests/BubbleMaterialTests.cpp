// Headless pixel tests for the same material and embedded shaders used by the plugin.
// EGL_PLATFORM=surfaceless selects Mesa's offscreen platform; no compositor is needed.
#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "animations/bubble/BubbleMaterial.hpp"
#include "Check.hpp"

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return id;
}

struct Fixture {
    CBubbleMaterial material;
    GLuint window = 0, otherWindow = 0, scene = 0, black = 0;

    Fixture() {
        Image content(width * height), alternative(width * height), background(width * height), dark(width * height);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto i = y * width + x;
            // A recognizable, asymmetric client image, distinct from the desktop.
            content[i] = {static_cast<unsigned char>(40 + x / 4), 30, static_cast<unsigned char>(110 + y / 4), 255};
            alternative[i] = {20, 120, 30, 255};
            // Red increases to the right, green upwards, blue marks an off-centre window behind.
            background[i] = {static_cast<unsigned char>(25 + x * 3 / 4),
                static_cast<unsigned char>(25 + y * 3 / 4),
                static_cast<unsigned char>(x > 130 && y > 100 ? 210 : 20), 255};
            dark[i] = {0, 0, 0, 255};
        }
        window = texture(content);
        otherWindow = texture(alternative);
        scene = texture(background);
        black = texture(dark);
        CHECK(material.initialize());
    }
    ~Fixture() {
        material.release();
        const GLuint textures[] = {window, otherWindow, scene, black};
        glDeleteTextures(4, textures);
    }
    Image render(const hyprimate::BubbleFrame &frame, GLuint image, GLuint backdrop,
        hyprimate::Vec2 pos, hyprimate::Vec2 size) {
        SBubbleTarget target{{width, height}, {0, 0}, pos, size};
        SBubbleImages images{};
        images.window = image;
        images.textureOrigin = {0, 0};
        images.textureSize = {width, height};
        images.wallpaper = backdrop;
        // Frame unit coordinates map to the scene at the physical source position.
        images.wallpaperOrigin = {pos.x / width, pos.y / height};
        images.wallpaperX = {size.x / width, 0};
        images.wallpaperY = {0, size.y / height};
        images.wallpaperPixel = {1.0 / width, 1.0 / height};
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        material.drawMembranes(frame, target, images);
        Image result(width * height);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        CHECK(glGetError() == GL_NO_ERROR);
        return result;
    }
};

int diff(const Pixel &a, const Pixel &b, int channel) { return int(a[channel]) - int(b[channel]); }
const Pixel &at(const Image &image, int x, int y) { return image[y * width + x]; }

void testSettled(Fixture &f) {
    hyprimate::BubbleFrame frame{};
    frame.size = {120, 80};
    frame.window.halfSize = {60, 40};
    frame.window.visibility = 1;
    frame.window.transparency = 1;
    const auto result = f.render(frame, f.window, f.scene, {50, 70}, frame.size);
    // Exact native pixels, including a location away from the centre and the outer clear area.
    for (const auto [x, y] : std::array<std::array<int, 2>, 3>{{{55, 75}, {100, 110}, {165, 145}}}) {
        const Pixel expected{static_cast<unsigned char>(40 + x / 4), 30,
            static_cast<unsigned char>(110 + y / 4), 255};
        CHECK(at(result, x, y) == expected);
    }
    CHECK(at(result, 40, 60)[3] == 0);
    // A companion behind the settled client must not show through it.
    auto &satellite = frame.formation[0];
    frame.formationCount = 1;
    satellite.radius = 20;
    satellite.growth = satellite.visibility = 1;
    satellite.thickness = 450;
    CHECK(result == f.render(frame, f.window, f.scene, {50, 70}, frame.size));
}

void testSettling(Fixture &f) {
    hyprimate::BubbleFrame frame{};
    frame.size = {160, 100};
    frame.window.halfSize = {80, 50};
    frame.window.visibility = 1;
    frame.window.thickness = 400;
    frame.window.motion.extent = {76, 48};
    const auto settled = f.render(frame, f.window, f.scene, {48, 78}, frame.size);
    // Crossing from a tiny remaining contraction into the native fast path must
    // restore client pixels continuously, not abruptly swap scene and window.
    for (double round : {.004, .002, .001}) {
        frame.window.motion.round = round;
        const auto settling = f.render(frame, f.window, f.scene, {48, 78}, frame.size);
        for (const auto [x, y] : std::array<std::array<int, 2>, 3>{{{90, 110}, {128, 128}, {170, 145}}})
            for (int channel = 0; channel < 4; ++channel)
                CHECK(std::abs(diff(at(settling, x, y), at(settled, x, y), channel)) <= 2);
    }
}

void testTransparency(Fixture &f) {
    hyprimate::BubbleFrame frame{};
    frame.size = {160, 100};
    frame.window.halfSize = {80, 50};
    frame.window.visibility = 1;
    frame.window.thickness = 400;
    frame.window.motion.round = .5;
    frame.window.motion.extent = {76, 48};
    const hyprimate::Vec2 pos{48, 78};
    frame.window.transparency = 0;
    const auto opaque = f.render(frame, f.window, f.scene, pos, frame.size);
    CHECK(opaque == f.render(frame, f.window, f.black, pos, frame.size));
    frame.window.transparency = 1;
    const auto clear = f.render(frame, f.window, f.scene, pos, frame.size);
    CHECK(clear == f.render(frame, f.otherWindow, f.scene, pos, frame.size));
    frame.window.transparency = .5;
    const auto halfway = f.render(frame, f.window, f.scene, pos, frame.size);
    for (int channel = 0; channel < 3; ++channel) {
        const int average = (int(at(opaque, 128, 128)[channel]) + at(clear, 128, 128)[channel]) / 2;
        CHECK(std::abs(int(at(halfway, 128, 128)[channel]) - average) <= 1);
    }
    CHECK(opaque != clear);
    // This changes the transmitted imagery, not the geometry or film coverage.
    for (std::size_t i = 0; i < opaque.size(); ++i)
        CHECK(opaque[i][3] == clear[i][3]);
}

void testMoving(Fixture &f, int w, int h, double round, hyprimate::Vec2 extent) {
    const hyprimate::Vec2 pos{(width - w) / 2.0, (height - h) / 2.0};
    hyprimate::BubbleFrame frame{};
    frame.size = {double(w), double(h)};
    frame.window.halfSize = {w / 2.0, h / 2.0};
    frame.window.corner = 4;
    frame.window.visibility = 1;
    frame.window.thickness = 400;
    frame.window.motion.round = round;
    frame.window.motion.extent = extent;
    frame.window.motion.drag = {5, -3};
    frame.window.motion.carry = {3, 2};
    const auto desktop = f.render(frame, f.window, f.scene, pos, frame.size);
    const auto dark = f.render(frame, f.window, f.black, pos, frame.size);
    const auto differentWindow = f.render(frame, f.otherWindow, f.scene, pos, frame.size);
    const int cx = width / 2 + 3, cy = height / 2 + 2;
    // A changing desktop behind the same client must be visible in the moving film.
    int desktopResponse = 0, windowResponse = 0;
    for (int c = 0; c < 3; ++c) {
        desktopResponse += std::abs(diff(at(desktop, cx, cy), at(dark, cx, cy), c));
        windowResponse += std::abs(diff(at(desktop, cx, cy), at(differentWindow, cx, cy), c));
    }
    CHECK(desktopResponse > 8);
    CHECK(windowResponse > 8);
    CHECK(desktopResponse > windowResponse);
    // Source orientation: compare the response to the scene, cancelling the unchanged window and film.
    const int dx = int(extent.x * .32), dy = int(extent.y * .32);
    const auto response = [&](int x, int y, int c) { return diff(at(desktop, x, y), at(dark, x, y), c); };
    CHECK(response(cx + dx, cy, 0) > response(cx - dx, cy, 0) + 2);
    CHECK(response(cx, cy + dy, 1) > response(cx, cy - dy, 1) + 2);
    // A blue rectangle represents a second window behind the bubble, not wallpaper alone.
    CHECK(response(cx + dx, cy + dy, 2) > response(cx - dx, cy + dy, 2) + 20);
    // Colour changes cannot alter the silhouette; interior and well outside remain distinct.
    CHECK(at(desktop, cx, cy)[3] > 128);
    // Moving the same film over the screen changes the desktop under its centre.
    const hyprimate::Vec2 shifted{pos.x - 20, pos.y - 20};
    const auto shiftedScene = f.render(frame, f.otherWindow, f.scene, shifted, frame.size);
    const auto shiftedDark = f.render(frame, f.otherWindow, f.black, shifted, frame.size);
    const auto originalScene = f.render(frame, f.otherWindow, f.scene, pos, frame.size);
    const auto originalDark = f.render(frame, f.otherWindow, f.black, pos, frame.size);
    CHECK(diff(at(originalScene, cx, cy), at(originalDark, cx, cy), 0) >
        diff(at(shiftedScene, cx - 20, cy - 20), at(shiftedDark, cx - 20, cy - 20), 0) + 2);
    CHECK(diff(at(originalScene, cx, cy), at(originalDark, cx, cy), 1) >
        diff(at(shiftedScene, cx - 20, cy - 20), at(shiftedDark, cx - 20, cy - 20), 1) + 2);
    CHECK(at(desktop, 2, 2)[3] == 0);
    bool sameSilhouette = true;
    for (std::size_t i = 0; i < desktop.size(); ++i)
        sameSilhouette &= (desktop[i][3] > 8) == (dark[i][3] > 8);
    CHECK(sameSilhouette);
    // The GPU outline must still enclose the CPU motion model's axis-edge points.
    for (int axis = 0; axis < 2; ++axis) for (int sign : {-1, 1}) {
        hyprimate::Vec2 edge{};
        if (axis == 0) edge.x = sign * frame.window.halfSize.x;
        else edge.y = sign * frame.window.halfSize.y;
        const auto mapped = hyprimate::bubbleMotionPoint(frame.window.halfSize, frame.window.motion, edge);
        const int x = int(std::lround(width / 2.0 + mapped.x));
        const int y = int(std::lround(height / 2.0 + mapped.y));
        const int ox = axis == 0 ? sign * 8 : 0;
        const int oy = axis == 1 ? sign * 8 : 0;
        CHECK(at(desktop, x - ox, y - oy)[3] > 128);
        CHECK(at(desktop, x + ox, y + oy)[3] < 8);
    }
}

// A bud swells the drawn outline exactly where the CPU motion model puts it.
void testBud(Fixture &f) {
    const int w = 160, h = 100;
    const hyprimate::Vec2 pos{(width - w) / 2.0, (height - h) / 2.0};
    hyprimate::BubbleFrame frame{};
    frame.size = {double(w), double(h)};
    frame.window.halfSize = {w / 2.0, h / 2.0};
    frame.window.corner = 4;
    frame.window.visibility = 1;
    frame.window.thickness = 400;
    frame.window.motion.round = .3;
    frame.window.motion.extent = {w / 2.0 * .92, h / 2.0 * .92};
    const hyprimate::Vec2 top{20, -h / 2.0};
    auto &bud = frame.window.motion.buds[0];
    bud.foot = hyprimate::bubbleRoundPoint(frame.window.halfSize, frame.window.motion, top);
    bud.width = 12;
    bud.height = 6;
    const auto swollen = f.render(frame, f.window, f.scene, pos, frame.size);
    const auto crest = hyprimate::bubbleMotionPoint(frame.window.halfSize, frame.window.motion, top);
    const int x = int(std::lround(width / 2.0 + crest.x));
    const int y = int(std::lround(height / 2.0 + crest.y));
    CHECK(at(swollen, x, y + 2)[3] > 128);
    CHECK(at(swollen, x, y - 3)[3] < 8);
    // Without it the film ends lower: the swelling is drawn, not just modelled.
    frame.window.motion.buds[0] = {};
    const auto flat = f.render(frame, f.window, f.scene, pos, frame.size);
    CHECK(at(flat, x, y + 2)[3] < 8);
}

// The small-bubble material: a thin film around air, visible by what its walls reflect.
void testCompanionOptics(Fixture &f) {
    hyprimate::BubbleFrame frame{};
    frame.size = {128, 128};
    frame.formationCount = 1;
    auto &bubble = frame.formation[0];
    bubble.growth = bubble.visibility = 1;
    bubble.thickness = 650;
    bubble.seed = .3;
    const hyprimate::Vec2 pos{64, 64};
    for (const double radius : {3.0, 8.0, 24.0, 48.0}) {
        bubble.radius = radius;
        const auto layer = f.render(frame, f.window, f.scene, pos, frame.size);
        // A film does not lens or tint: its layer never samples the desktop or the client.
        CHECK(layer == f.render(frame, f.window, f.black, pos, frame.size));
        CHECK(layer == f.render(frame, f.otherWindow, 0, pos, frame.size));
        double centre = 0, ring = 0, ringMin = 1e9, ringMax = 0;
        int centreCount = 0, ringCount = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double d = std::hypot(x + .5 - 128, y + .5 - 128) / radius;
                const double alpha = at(layer, x, y)[3];
                if (d < .4) centre += alpha, ++centreCount;
                if (d > 1 - 1.5 / radius && d < 1 - .5 / radius) {
                    ring += alpha, ++ringCount;
                    const double light = at(layer, x, y)[0] + at(layer, x, y)[1] + at(layer, x, y)[2];
                    ringMin = std::min(ringMin, light);
                    ringMax = std::max(ringMax, light);
                }
                if (d > 1 + 1.5 / radius) CHECK(alpha == 0);
            }
        }
        // Mostly transparent: the interior transmits most of the light behind it.
        if (centreCount > 0) CHECK(centre / centreCount < 255 * .25);
        // The outline emerges from grazing reflection: stronger than the interior wherever the
        // bubble is large enough to have one...
        CHECK(ringCount > 0 && ring > 0);
        if (radius >= 8) CHECK(ring / ringCount > 1.5 * centre / centreCount);
        // ...but never an opaque stroke, and it varies along the circumference.
        CHECK(ring / ringCount < 255 * .8);
        if (radius >= 24) CHECK(ringMax > ringMin + 12);
    }
    // Resolution-aware: a tiny bubble sliding by subpixel steps keeps a stable total response
    // instead of flickering as its rim and highlight cross pixel centres.
    bubble.radius = 2.5;
    double first = 0, lowest = 1e9, highest = 0;
    for (int i = 0; i < 8; ++i) {
        bubble.centre = {i * .125, i * .0625};
        const auto layer = f.render(frame, f.window, f.scene, pos, frame.size);
        double total = 0;
        for (const auto &p : layer) total += p[3];
        if (i == 0) first = total;
        lowest = std::min(lowest, total);
        highest = std::max(highest, total);
    }
    CHECK(first > 0 && highest < lowest * 1.15);
}

void testFormation(Fixture &f) {
    for (const hyprimate::Vec2 size : {hyprimate::Vec2{64, 180}, {200, 60}, {150, 150}, {180, 126}}) {
        const hyprimate::Vec2 pos{(width - size.x) / 2, (height - size.y) / 2};
        hyprimate::Bubble bubble;
        bubble.begin(hyprimate::Transition::Open, 1, 42, size);
        bool companionsDrawn = false, companionsAfterLanding = false;
        for (int step = 0; step < 100; ++step) {
            bubble.advance(.01);
            if (step % 5 != 4) continue;
            const auto frame = bubble.frame(size);
            const auto drawn = f.render(frame, f.window, f.scene, pos, size);
            auto primary = frame;
            primary.formationCount = 0;
            const auto without = f.render(primary, f.window, f.scene, pos, size);
            companionsDrawn |= drawn != without;
            if (bubble.landed() && drawn != without) companionsAfterLanding = true;
            if (step == 99) CHECK(drawn == without);
            // Companions are clear films: what shows through them is the real desktop behind the
            // layer, never a resampled copy of it or of the client.
            auto satellites = frame;
            satellites.window.visibility = 0;
            const auto scene = f.render(satellites, f.window, f.scene, pos, size);
            CHECK(scene == f.render(satellites, f.otherWindow, f.black, pos, size));
            CHECK(!glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_BLEND));
        }
        CHECK(companionsDrawn);
        CHECK(companionsAfterLanding);
    }
}
} // namespace

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API)) {
        std::fprintf(stderr, "Cannot initialize offscreen EGL (try EGL_PLATFORM=surfaceless)\n");
        return 1;
    }
    const EGLint configAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display, configAttrs, &config, 1, &count) || count != 1) return 1;
    const EGLint surfaceAttrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) {
        std::fprintf(stderr, "Cannot create GLES 3 offscreen context\n");
        return 1;
    }
    {
        Fixture fixture;
        testSettled(fixture);
        testSettling(fixture);
        testTransparency(fixture);
        testFormation(fixture);
        testCompanionOptics(fixture);
        testMoving(fixture, 160, 100, .35, {76, 48}); // pillow, wide
        testMoving(fixture, 100, 160, .75, {46, 74}); // ellipse, tall
        testMoving(fixture, 150, 90, .85, {48, 48}); // disc
        testBud(fixture);
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return g_failures == 0 ? 0 : 1;
}
