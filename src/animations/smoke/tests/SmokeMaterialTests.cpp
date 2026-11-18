// Headless tests and optional visual capture of the actual compositor fluid and shaders.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include "Check.hpp"
#include "animations/smoke/SmokeMaterial.hpp"

namespace {
    constexpr int W = 512, H = 256, TW = 160, TH = 120;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;

    void save(const std::filesystem::path &path, const Image &image) {
        FILE *file = std::fopen(path.string().c_str(), "wb");
        if (file == nullptr) {
            (void)std::fprintf(stderr, "Cannot write capture %s\n", path.string().c_str());
            ++g_failures;
            return;
        }
        (void)std::fprintf(file, "P6\n%d %d\n255\n", W, H);
        for (const auto &p : image) {
            // Composite premultiplied pixels over a neutral review background.
            const int BACK = (255 - int(p[3])) * 60 / 255;
            const std::array<unsigned char, 3> RGB{static_cast<unsigned char>(std::min(255, p[0] + BACK)),
                static_cast<unsigned char>(std::min(255, p[1] + BACK)),
                static_cast<unsigned char>(std::min(255, p[2] + BACK))};
            (void)std::fwrite(RGB.data(), 1, 3, file);
        }
        (void)std::fclose(file);
    }

    struct Harness {
        CSmokeMaterial material;
        GLuint texture = 0;
        SSmokeTarget target{.canvas = {W, H}, .origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        hyprimate::SmokeFrame frame{};

        void upload(const Image &pixels) const {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TW, TH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        [[nodiscard]] SSmokeImages images() const {
            return {.window = texture, .texture_origin = target.source_pos, .texture_size = {TW, TH}};
        }
        Image render(CSmokeField &field) {
            glViewport(0, 0, W, H);
            glDisable(GL_BLEND);
            CHECK(material.simulate(field, frame, target, images()));
            material.draw(field, frame, target, images());
            Image result(static_cast<std::size_t>(W) * H);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
            return result;
        }
        // Unravels the resting window into smoke over half a second.
        void convert(CSmokeField &field) {
            frame.presence = 1;
            frame.fade = 1;
            for (int i = 0; i <= 30; ++i) {
                frame.time += 1.0 / 60;
                frame.smoke = std::min(1.0, i / 20.0);
                render(field);
            }
        }
    };

    Pixel at(const Image &image, int x, int y) {
        return image[static_cast<std::size_t>(y * W + x)];
    }
    int alpha_sum(const Image &image, int x0, int x1) {
        int sum = 0;
        for (int y = 20; y < H - 20; ++y) {
            for (int x = x0; x < x1; ++x) {
                sum += at(image, x, y)[3];
            }
        }
        return sum;
    }
}  // namespace

int main(int argc, char **argv) {
    const char *capture = std::getenv("SMOKE_CAPTURE_DIR");
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--capture-dir") == 0) {
            capture = argv[++i];
        }
    }
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || (eglInitialize(display, &major, &minor) == 0U) ||
        (eglBindAPI(EGL_OPENGL_ES_API) == 0U)) {
        (void)std::fprintf(stderr, "Cannot initialize EGL; try EGL_PLATFORM=surfaceless\n");
        return 1;
    }
    const std::array<EGLint, 13> attrs{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if ((eglChooseConfig(display, attrs.data(), &config, 1, &count) == 0U) || count != 1) {
        return 1;
    }
    // Large pbuffer permits realistic full-HD timing without changing the shader.
    const std::array<EGLint, 5> surface_attrs{EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE};
    const std::array<EGLint, 3> context_attrs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs.data());
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs.data());
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        (eglMakeCurrent(display, surface, surface, context) == 0U)) {
        return 1;
    }
    {
        Image pixels(static_cast<std::size_t>(TW) * TH);
        for (int y = 0; y < TH; ++y) {
            for (int x = 0; x < TW; ++x) {
                pixels[static_cast<std::size_t>(y * TW + x)] = {static_cast<unsigned char>(30 + x),
                    static_cast<unsigned char>(20 + y), static_cast<unsigned char>(x < 80 ? 180 : 35), 255};
            }
        }
        Harness h;
        CHECK(h.material.initialize());
        glGenTextures(1, &h.texture);
        glBindTexture(GL_TEXTURE_2D, h.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        h.upload(pixels);
        h.frame.size = {TW, TH};
        h.frame.seed = 0.37;

        // At rest the window is drawn exactly and nothing surrounds it.
        {
            CSmokeField field;
            const Image SETTLED = h.render(field);
            CHECK(at(SETTLED, 100, 100) == pixels[static_cast<std::size_t>(32 * TW + 60)]);
            CHECK(at(SETTLED, 10, 10)[3] == 0 && at(SETTLED, 300, 128)[3] == 0);
            field.release();
        }

        // Unravelling turns the window into smoke coloured by its own content.
        Image smoke;
        {
            CSmokeField field;
            h.convert(field);
            smoke = h.render(field);
            CHECK(alpha_sum(smoke, 40, 200) > 0);
            for (auto &pixel : pixels) {
                pixel = {0, 180, 0, 255};
            }
            h.upload(pixels);
            CSmokeField green_field;
            h.frame.time = 0;
            h.frame.smoke = 0;
            h.convert(green_field);
            const Image GREEN = h.render(green_field);
            int green = 0, red = 0;
            for (const auto &pixel : GREEN) {
                green += pixel[1];
                red += pixel[0];
            }
            CHECK(green > 4 * red && green > 0);
            // A fully transparent client cannot produce visible smoke.
            for (auto &pixel : pixels) {
                pixel = {0, 0, 0, 0};
            }
            h.upload(pixels);
            CSmokeField clear_field;
            h.frame.time = 0;
            h.frame.smoke = 0;
            h.convert(clear_field);
            CHECK(alpha_sum(h.render(clear_field), 0, W) == 0);
            // Smoke whose residual fade has ended is invisible.
            h.frame.fade = 0;
            h.frame.smoke = 1;
            h.frame.time += 1.0 / 60;
            CHECK(alpha_sum(h.render(field), 0, W) == 0);
            field.release();
            green_field.release();
            clear_field.release();
        }

        // The smoke stays anchored to the world: a moving window leaves its wake behind it.
        {
            for (int y = 0; y < TH; ++y) {
                for (int x = 0; x < TW; ++x) {
                    pixels[static_cast<std::size_t>(y * TW + x)] = {200, 120, 60, 255};
                }
            }
            h.upload(pixels);
            CSmokeField field;
            h.frame = {};
            h.frame.size = {TW, TH};
            h.frame.seed = 0.37;
            h.target.source_pos = {40, 68};
            h.convert(field);
            for (int i = 0; i < 20; ++i) {
                h.frame.time += 1.0 / 60;
                h.frame.velocity = {480, 0};
                h.frame.position.x += 8;
                h.target.source_pos.x += 8;
                h.render(field);
            }
            const Image MOVED = h.render(field);
            const int RIGHT = static_cast<int>(h.target.source_pos.x) + TW;
            const int LEFT = static_cast<int>(h.target.source_pos.x);
            CHECK(alpha_sum(MOVED, LEFT - 70, LEFT) > 2 * alpha_sum(MOVED, RIGHT, RIGHT + 70));
            field.release();
        }

        // Resizing must transport existing smoke, not erase and refill it. With the
        // source disabled, disappearing density cannot be hidden by fresh emission.
        for (const bool CENTRED : {false, true}) {
            CSmokeField field;
            h.frame = {};
            h.frame.size = {TW, TH};
            h.frame.seed = .37;
            h.target.source_pos = {176, 68};
            h.target.source_size = {TW, TH};
            h.convert(field);
            h.frame.presence = 0;
            int previous = alpha_sum(h.render(field), 0, W);
            CHECK(previous > 10000);
            for (int i = 0; i < 48; ++i) {
                const double DX = i < 24 ? -2.0 : 2.0;
                const double DY = i < 24 ? 2.0 : -2.0;
                h.frame.size.x += DX;
                h.frame.size.y += DY;
                h.target.source_size = h.frame.size;
                if (CENTRED) {
                    h.target.source_pos.x -= DX * .5;
                    h.target.source_pos.y -= DY * .5;
                } else {
                    h.frame.position.x += DX * .5;
                    h.frame.position.y += DY * .5;
                }
                h.frame.time += 1.0 / 60;
                const Image RESIZED = h.render(field);
                const int CURRENT = alpha_sum(RESIZED, 0, W);
                CHECK(CURRENT > previous * .65);
                // Rendering another monitor at the same time must not step again.
                CHECK(h.render(field) == RESIZED);
                previous = CURRENT;
                if (capture != nullptr && i % 6 == 0) {
                    std::filesystem::create_directories(capture);
                    std::string name = CENTRED ? "resize-centred-" : "resize-corner-";
                    name.append(std::to_string(i / 6)).append(".ppm");
                    save(std::filesystem::path(capture) / name, RESIZED);
                }
            }
            CHECK(previous > 1000);
            field.release();
        }

        if (capture != nullptr) {
            std::filesystem::create_directories(capture);
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(7);
            h.target.source_pos = {60, 68};
            const hyprimate::Vec2 SIZE{TW, TH};
            driver.begin(hyprimate::Transition::Open, 1.1, SIZE);
            for (int i = 0; i < 150; ++i) {
                if (i >= 70 && i < 100) {
                    driver.drive({6, 0});
                    h.target.source_pos.x += 6;
                }
                if (i == 110) {
                    driver.begin(hyprimate::Transition::Close, 0.85, SIZE);
                }
                driver.advance(1.0 / 30, SIZE);
                h.frame = driver.frame(SIZE);
                const Image IMAGE = h.render(field);
                if (i % 10 == 0) {
                    save(std::filesystem::path(capture) / ("frame-" + std::to_string(i / 10) + ".ppm"),
                        IMAGE);
                }
            }
            field.release();
        }

        // Full-HD cost of one simulation step plus the composite.
        CSmokeField field;
        h.frame = {};
        h.frame.size = {1280, 720};
        h.frame.presence = h.frame.fade = 1;
        h.target = {.canvas = {1920, 1080}, .origin = {0, 0}, .source_pos = {320, 180}, .source_size = {1280, 720}};
        glViewport(0, 0, 1920, 1080);
        h.material.simulate(field, h.frame, h.target, h.images());
        glFinish();
        const auto START = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) {
            h.frame.time += .016;
            h.frame.smoke = 0.2 * (i + 1);
            h.material.simulate(field, h.frame, h.target, h.images());
            h.material.draw(field, h.frame, h.target, h.images());
        }
        glFinish();
        const auto ELAPSED =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - START).count() / 5.0;
        (void)std::fprintf(stderr, "Smoke 1920x1080 fluid step + composite: %.2f ms/frame (5 frames)\n", ELAPSED);
        CHECK(glGetError() == GL_NO_ERROR);
        field.release();
        h.material.release();
        glDeleteTextures(1, &h.texture);
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return g_failures == 0 ? 0 : 1;
}
