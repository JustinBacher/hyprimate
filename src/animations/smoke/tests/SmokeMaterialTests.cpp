// Headless tests and optional visual capture of the actual compositor fluid and shaders.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <GLES3/gl32.h>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "animations/smoke/SmokeMaterial.hpp"
#include "smoke_shaders_generated.hpp"

namespace {
    constexpr int W = 512, H = 256, TW = 160, TH = 120;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;

    void save(const std::filesystem::path &path, const Image &image, int width = W, int height = H) {
        FILE *file = std::fopen(path.string().c_str(), "wb");
        if (file == nullptr) {
            (void)std::fprintf(stderr, "Cannot write capture %s\n", path.string().c_str());
            ++g_failures;
            return;
        }
        (void)std::fprintf(file, "P6\n%d %d\n255\n", width, height);
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
        hyprimate::Vec2 texture_size{TW, TH};
        SSmokeTarget target{.canvas = {W, H}, .origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        hyprimate::SmokeFrame frame{};

        void upload(const Image &pixels, int width = TW, int height = TH) {
            texture_size = {static_cast<double>(width), static_cast<double>(height)};
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        [[nodiscard]] SSmokeImages images() const {
            return {.window = texture, .texture_origin = target.source_pos, .texture_size = texture_size};
        }
        Image render(CSmokeField &field) {
            const auto WIDTH = static_cast<GLsizei>(target.canvas.x);
            const auto HEIGHT = static_cast<GLsizei>(target.canvas.y);
            glViewport(0, 0, WIDTH, HEIGHT);
            glDisable(GL_BLEND);
            CHECK(material.simulate(field, frame, target, images()));
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, WIDTH, HEIGHT);
            material.draw(field, frame, target, images());
            Image result(static_cast<std::size_t>(WIDTH) * HEIGHT);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
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
    gl_harness::CContext context;
    if (!context.valid()) return 1;
    {
        Image pixels(static_cast<std::size_t>(TW) * TH);
        for (int y = 0; y < TH; ++y) {
            for (int x = 0; x < TW; ++x) {
                pixels[static_cast<std::size_t>(y * TW + x)] = {static_cast<unsigned char>(30 + x),
                    static_cast<unsigned char>(20 + y), static_cast<unsigned char>(x < 80 ? 180 : 35), 255};
            }
        }
        Harness h;
        SSmokePrograms programs;
        const std::array<const char *, SSmokePrograms::ProgramCount> sources{smoke_shader::VELOCITY.data(),
            smoke_shader::DIVERGENCE.data(), smoke_shader::PRESSURE.data(), smoke_shader::PROJECT.data(),
            smoke_shader::ADVECT.data(), smoke_shader::DYE.data(), smoke_shader::COMPOSITE.data()};
        for (std::size_t i = 0; i < sources.size(); ++i) {
            programs.ids[i] = gl_harness::build_program(smoke_shader::VERTEX.data(), sources[i]);
            CHECK(programs.ids[i] != 0);
        }
        CHECK(h.material.initialize(programs));
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

        // Wake smoke lives briefly: passing back and forth over the same area does not pile it
        // up, and once the window stops the trail away from it clears promptly.
        {
            constexpr int CW = 1024, CH = 512, CLEARANCE = 40;
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(5);
            const hyprimate::Vec2 SIZE{TW, TH};
            h.target = {.canvas = {CW, CH}, .origin = {0, 0}, .source_pos = {260, 196}, .source_size = SIZE};
            const auto AWAY = [&](const Image &image) {
                const int X0 = static_cast<int>(h.target.source_pos.x) - CLEARANCE;
                const int Y0 = static_cast<int>(h.target.source_pos.y) - CLEARANCE;
                long sum = 0;
                for (int y = 0; y < CH; ++y) {
                    for (int x = 0; x < CW; ++x) {
                        if (x < X0 || x >= X0 + TW + 2 * CLEARANCE || y < Y0 || y >= Y0 + TH + 2 * CLEARANCE) {
                            sum += image[static_cast<std::size_t>(y * CW + x)][3];
                        }
                    }
                }
                return sum;
            };
            const auto STEP = [&](double dx) {
                if (dx != 0) {
                    driver.drive({dx, 0});
                    h.target.source_pos.x += dx;
                }
                driver.advance(1.0 / 60, SIZE);
                h.frame = driver.frame(SIZE);
                return h.render(field);
            };
            std::array<long, 6> passes{};
            for (int pass = 0; pass < 6; ++pass) {
                for (int i = 0; i < 30; ++i) {
                    passes[pass] = std::max(passes[pass], AWAY(STEP(pass % 2 == 0 ? 12.0 : -12.0)));
                }
            }
            CHECK(passes[1] > 100000 && passes[5] < passes[1]);
            const int CLEAR = static_cast<int>(std::ceil(60.0 * 1.1 * hyprimate::smoke_life(hyprimate::SmokeParams{}.trail)));
            long rest = 0;
            for (int i = 0; i < CLEAR; ++i) {
                rest = AWAY(STEP(0));
            }
            CHECK(rest < passes[1] / 100);
            field.release();
            h.target = {.canvas = {W, H}, .origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        }

        // A display frame between fluid substeps draws the stepped smoke where the world has
        // it: moving the window without stepping must not carry detached smoke along with it.
        {
            CSmokeField field;
            h.frame = {};
            h.frame.size = {TW, TH};
            h.frame.seed = .37;
            h.target.source_pos = {176, 68};
            h.convert(field);
            for (int i = 0; i < 12; ++i) {
                h.frame.time += 1.0 / 60;
                h.frame.velocity = {360, 0};
                h.frame.position.x += 6;
                h.target.source_pos.x += 6;
                h.render(field);
            }
            const Image STEPPED = h.render(field);
            h.frame.position.x += 5;
            h.target.source_pos.x += 5;
            const Image BETWEEN = h.render(field);
            const int LEFT = static_cast<int>(h.target.source_pos.x) - 40;
            const int RIGHT = static_cast<int>(h.target.source_pos.x) + TW + 40;
            int smoke = 0, moved = 0;
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (x >= LEFT && x < RIGHT) {
                        continue;
                    }
                    const int A = at(STEPPED, x, y)[3], B = at(BETWEEN, x, y)[3];
                    smoke += A > 8 ? 1 : 0;
                    moved += std::abs(A - B) > 3 ? 1 : 0;
                }
            }
            CHECK(smoke > 500 && moved * 50 < smoke);
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

        // Wake capture of a full-size window: a fast move, a sharp change of direction, then a
        // stop while the trail dissipates.
        if (capture != nullptr) {
            constexpr int CW = 1920, CH = 1080, WW = 640, WH = 400;
            std::filesystem::create_directories(capture);
            Image content(static_cast<std::size_t>(WW) * WH);
            for (int y = 0; y < WH; ++y) {
                for (int x = 0; x < WW; ++x) {
                    content[static_cast<std::size_t>(y * WW + x)] = {static_cast<unsigned char>(40 + x * 200 / WW),
                        static_cast<unsigned char>(60 + y * 150 / WH), static_cast<unsigned char>(x < WW / 2 ? 200 : 50), 255};
                }
            }
            h.upload(content, WW, WH);
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(11);
            const hyprimate::Vec2 SIZE{WW, WH};
            h.target = {.canvas = {CW, CH}, .origin = {0, 0}, .source_pos = {120, 560}, .source_size = SIZE};
            for (int i = 0; i < 168; ++i) {
                const hyprimate::Vec2 STEP = i < 36 ? hyprimate::Vec2{24, -4}
                    : i < 60                       ? hyprimate::Vec2{-6, -16}
                                                   : hyprimate::Vec2{};
                if (STEP.x != 0 || STEP.y != 0) {
                    driver.drive(STEP);
                    h.target.source_pos.x += STEP.x;
                    h.target.source_pos.y += STEP.y;
                }
                driver.advance(1.0 / 60, SIZE);
                h.frame = driver.frame(SIZE);
                const Image IMAGE = h.render(field);
                if (i % 3 == 0) {
                    char name[32];
                    (void)std::snprintf(name, sizeof name, "trail-%03d.ppm", i);
                    save(std::filesystem::path(capture) / name, IMAGE, CW, CH);
                }
            }
            field.release();
            // Erratic dragging on a 144 Hz display: frames fall between fluid substeps, and the
            // direction flips faster than the wake can settle behind the window.
            hyprimate::Smoke erratic;
            erratic.seed(13);
            h.target.source_pos = {200, 340};
            for (int i = 0; i < 380; ++i) {
                // Wide sweeps of about a third of a second, each with its own speed and wobble.
                const int SEGMENT = i / 50;
                const double SPEED = (SEGMENT % 2 == 0 ? 1.0 : -1.0) * (2300.0 + ((SEGMENT * 37) % 7) * 120.0) *
                    (0.75 + 0.25 * std::sin(i * 0.9));
                const hyprimate::Vec2 STEP = i < 250
                    ? hyprimate::Vec2{SPEED / 144.0, ((SEGMENT % 3) - 1) * 220.0 / 144.0 + 3.0 * std::sin(i * 1.3)}
                    : hyprimate::Vec2{};
                if (STEP.x != 0 || STEP.y != 0) {
                    erratic.drive(STEP);
                    h.target.source_pos.x += STEP.x;
                    h.target.source_pos.y += STEP.y;
                }
                erratic.advance(1.0 / 144, SIZE);
                h.frame = erratic.frame(SIZE);
                const Image IMAGE = h.render(field);
                if (i % 6 == 0) {
                    char name[32];
                    (void)std::snprintf(name, sizeof name, "erratic-%03d.ppm", i);
                    save(std::filesystem::path(capture) / name, IMAGE, CW, CH);
                }
            }
            field.release();
            h.upload(pixels);
            h.target = {.canvas = {W, H}, .origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
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
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 1920, 1080);
            h.material.draw(field, h.frame, h.target, h.images());
        }
        glFinish();
        const auto ELAPSED =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - START).count() / 5.0;
        (void)std::fprintf(stderr, "Smoke 1920x1080 fluid step + composite: %.2f ms/frame (5 frames)\n", ELAPSED);
        CHECK(glGetError() == GL_NO_ERROR);
        field.release();
        h.material.release();
        for (GLuint program : programs.ids) glDeleteProgram(program);
        glDeleteTextures(1, &h.texture);
    }
    return g_failures == 0 ? 0 : 1;
}
