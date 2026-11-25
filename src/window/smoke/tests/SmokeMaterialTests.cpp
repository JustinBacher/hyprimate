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
#include <type_traits>
#include <vector>

#include <GLES3/gl32.h>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "smoke_shaders_generated.hpp"
#include "window/smoke/SmokeMaterial.hpp"

namespace {
    struct TrackedName {
        GLuint name;
        unsigned deletions = 0;
    };

    struct AllocationTrace;
    thread_local AllocationTrace *active_trace = nullptr;

    // These wrappers are linked only into this executable and are inert outside the scope.
    struct AllocationTrace {
        explicit AllocationTrace(bool reject_pair)
            : previous(active_trace), reject_pair(reject_pair) {
            active_trace = this;
        }
        ~AllocationTrace() {
            active_trace = previous;
        }
        AllocationTrace(const AllocationTrace &) = delete;
        AllocationTrace &operator=(const AllocationTrace &) = delete;
        AllocationTrace(AllocationTrace &&) = delete;
        AllocationTrace &operator=(AllocationTrace &&) = delete;

        static void generated(std::vector<TrackedName> &names, GLsizei count, const GLuint *ids) {
            for (GLsizei i = 0; i < count; ++i) names.push_back({ids[i]});
        }
        static void deleted(std::vector<TrackedName> &names, GLsizei count, const GLuint *ids) {
            for (GLsizei i = 0; i < count; ++i) {
                for (auto &name : names) {
                    if (name.name == ids[i]) ++name.deletions;
                }
            }
        }

        AllocationTrace *previous;
        bool reject_pair;
        bool rejected_pair = false;
        std::vector<TrackedName> textures;
        std::vector<TrackedName> framebuffers;
    };
}  // namespace

extern "C" {
void __real_glGenTextures(GLsizei, GLuint *);
void __real_glDeleteTextures(GLsizei, const GLuint *);
void __real_glGenFramebuffers(GLsizei, GLuint *);
void __real_glDeleteFramebuffers(GLsizei, const GLuint *);
void __real_glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);

void __wrap_glGenTextures(GLsizei count, GLuint *ids) {
    __real_glGenTextures(count, ids);
    if (active_trace != nullptr) AllocationTrace::generated(active_trace->textures, count, ids);
}
void __wrap_glDeleteTextures(GLsizei count, const GLuint *ids) {
    if (active_trace != nullptr) AllocationTrace::deleted(active_trace->textures, count, ids);
    __real_glDeleteTextures(count, ids);
}
void __wrap_glGenFramebuffers(GLsizei count, GLuint *ids) {
    __real_glGenFramebuffers(count, ids);
    if (active_trace != nullptr) AllocationTrace::generated(active_trace->framebuffers, count, ids);
}
void __wrap_glDeleteFramebuffers(GLsizei count, const GLuint *ids) {
    if (active_trace != nullptr) AllocationTrace::deleted(active_trace->framebuffers, count, ids);
    __real_glDeleteFramebuffers(count, ids);
}
void __wrap_glFramebufferTexture2D(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (active_trace != nullptr && active_trace->reject_pair && !active_trace->rejected_pair &&
        attachment == GL_COLOR_ATTACHMENT1 && texture != 0 && !active_trace->textures.empty()) {
        active_trace->rejected_pair = true;
        // Use another allocated texture so the attachment query remains valid, but mismatches.
        __real_glFramebufferTexture2D(
            target, attachment, textarget, active_trace->textures.front().name, level);
        return;
    }
    __real_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}
}  // extern "C"

namespace {
    static_assert(!std::is_copy_constructible_v<CSmokeMaterial>);
    static_assert(!std::is_copy_assignable_v<CSmokeMaterial>);
    static_assert(!std::is_move_constructible_v<CSmokeMaterial>);
    static_assert(!std::is_move_assignable_v<CSmokeMaterial>);
    static_assert(!std::is_copy_constructible_v<CSmokeField>);
    static_assert(!std::is_copy_assignable_v<CSmokeField>);
    static_assert(!std::is_move_constructible_v<CSmokeField>);
    static_assert(!std::is_move_assignable_v<CSmokeField>);

    constexpr int W = 512, H = 256, TW = 160, TH = 120;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;

    void require(bool passed) {
        CHECK(passed);
        if (!passed) std::exit(EXIT_FAILURE);
    }

    void save(
        const std::filesystem::path &path, const Image &image, int width = W, int height = H) {
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
            const std::array<unsigned char, 3> RGB{
                static_cast<unsigned char>(std::min(255, p[0] + BACK)),
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
        hyprimate::Vec2 canvas{W, H};
        SSmokeTarget target{.origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        hyprimate::SmokeFrame frame{};

        void upload(const Image &pixels, int width = TW, int height = TH) {
            texture_size = {static_cast<double>(width), static_cast<double>(height)};
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                width,
                height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());
        }
        [[nodiscard]] SSmokeImages images() const {
            return {.window = texture,
                .texture_origin = target.source_pos,
                .texture_size = texture_size};
        }
        Image render(CSmokeField &field) {
            const auto WIDTH = static_cast<GLsizei>(canvas.x);
            const auto HEIGHT = static_cast<GLsizei>(canvas.y);
            glViewport(0, 0, WIDTH, HEIGHT);
            glDisable(GL_BLEND);
            require(material.simulate(field, frame, target, images()));
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

    void test_smoke_tint(Harness &harness) {
        harness.upload(Image(TW * TH, Pixel{255, 0, 0, 255}));
        harness.frame = {};
        harness.frame.size = {TW, TH};
        CSmokeField field;
        const Image REST = harness.render(field);
        harness.frame.color = 0xFF0000FF;
        CHECK(harness.render(field) == REST);

        harness.frame.color = 0;
        harness.convert(field);
        const Image UNTINTED = harness.render(field);
        harness.frame.color = 0x000000FF;
        CHECK(harness.render(field) == UNTINTED);
        harness.frame.color = 0x800000FF;
        const Image HALF_TINT = harness.render(field);
        harness.frame.color = 0xFF0000FF;
        const Image TINTED = harness.render(field);

        // Compare only smoke outside the window, including low-alpha wisps.
        int untinted_blue = 0, half_blue = 0, tinted_blue = 0, tinted_red = 0;
        bool same_opacity = true;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (x >= 40 && x < 40 + TW && y >= 68 && y < 68 + TH)
                    continue;
                const auto base = at(UNTINTED, x, y);
                const auto half = at(HALF_TINT, x, y);
                const auto full = at(TINTED, x, y);
                same_opacity &= base[3] == half[3] && base[3] == full[3];
                untinted_blue += base[2];
                half_blue += half[2];
                tinted_blue += full[2];
                tinted_red += full[0];
            }
        }
        CHECK(same_opacity);
        CHECK(untinted_blue < half_blue && half_blue < tinted_blue);
        CHECK(tinted_red > 0 && tinted_red < tinted_blue);
        field.release();
        harness.frame = {};
    }

    void test_stationary_close_keeps_solid_content_in_place(Harness &harness) {
        const hyprimate::Vec2 SIZE{TW, TH};
        for (const auto ROUTE : {hyprimate::SmokeRoute{},
                 hyprimate::SmokeRoute{.edge = true, .direction = {0, 1}, .reach = 160}}) {
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(7);
            harness.frame = driver.frame(SIZE);
            const Image REST = harness.render(field);
            driver.begin(hyprimate::Transition::Close, 1.6, SIZE, ROUTE);
            for (int step = 1; step <= 8; ++step) {
                driver.advance(1.0 / 60, SIZE);
                harness.frame = driver.frame(SIZE);
                const Image CLOSE = harness.render(field);
                // The still-solid interior must not shift or scale as the smoke head forms.
                for (const int y : {108, 128, 148}) {
                    const auto EXPECTED = at(REST, 145, y);
                    const auto ACTUAL = at(CLOSE, 145, y);
                    CHECK(std::abs(int(ACTUAL[0]) - int(EXPECTED[0])) <= 2 &&
                          std::abs(int(ACTUAL[1]) - int(EXPECTED[1])) <= 2 && ACTUAL[3] >= 253);
                }
            }
            field.release();
        }
    }

    void test_smoke_forms_the_window(Harness &harness, const char *capture, int frames, bool edge) {
        const hyprimate::Vec2 SIZE{TW, TH};
        const int LEFT = static_cast<int>(harness.target.source_pos.x);
        const int TOP = static_cast<int>(harness.target.source_pos.y);
        constexpr int AREA = (TW - 6) * (TH - 6);
        const auto interior = [&](int x, int y) {
            return x >= LEFT + 3 && x < LEFT + TW - 3 && y >= TOP + 3 && y < TOP + TH - 3;
        };
        CSmokeField field;
        hyprimate::Smoke driver;
        driver.seed(7);
        harness.frame = driver.frame(SIZE);
        const Image REST = harness.render(field);
        driver.begin(hyprimate::Transition::Open,
            frames / 60.0,
            SIZE,
            edge ? hyprimate::SmokeRoute{.edge = true, .direction = {1, 0}, .reach = 240}
                 : hyprimate::SmokeRoute{});
        std::filesystem::path destination;
        std::string prefix;
        if (capture != nullptr) {
            destination = capture;
            std::filesystem::create_directories(destination);
            prefix = std::to_string(frames) + (edge ? "-edge-" : "-inside-");
        }
        int previous_opacity = 0, largest_gain = 0, early_smoke = 0;
        for (int i = 0; i <= frames + 30; ++i) {
            driver.advance(i == 0 ? 0.0 : 1.0 / 60.0, SIZE);
            harness.frame = driver.frame(SIZE);
            const Image IMAGE = harness.render(field);
            if (capture != nullptr) {
                save(destination / (prefix + std::to_string(i) + ".ppm"), IMAGE);
            }
            int solid = 0, smoke = 0, exact = 0, opacity = 0;
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const Pixel PIXEL = at(IMAGE, x, y);
                    if (interior(x, y)) {
                        solid += PIXEL[3] >= 245;
                        opacity += PIXEL[3];
                        exact += PIXEL == at(REST, x, y);
                    }
                    smoke += PIXEL[3] >= 24 && PIXEL[3] <= 180;
                }
            }
            if (i <= frames / 2) {
                early_smoke = std::max(early_smoke, smoke);
                CHECK(solid < AREA / 4);
            }
            if (i == 2 * frames / 3) {
                // The forming window cannot reveal itself on its own clock.
                CSmokeField empty;
                const Image UNSEEDED = harness.render(empty);
                int unsupported = 0;
                for (int y = TOP + 3; y < TOP + TH - 3; ++y) {
                    for (int x = LEFT + 3; x < LEFT + TW - 3; ++x) {
                        unsupported += at(UNSEEDED, x, y)[3] >= 192;
                    }
                }
                if (capture != nullptr) {
                    std::printf("%s unsupported solid %d/%d; smoke %d\n",
                        prefix.c_str(),
                        unsupported,
                        AREA,
                        smoke);
                }
                CHECK(unsupported < AREA / 12);
                CHECK(smoke > AREA / 12);
                empty.release();
            }
            if (i <= frames) {
                largest_gain = std::max(largest_gain, opacity - previous_opacity);
            }
            if (i == frames || i == frames + 30) {
                CHECK(exact > 99 * AREA / 100);
            }
            previous_opacity = opacity;
        }
        if (capture != nullptr) {
            std::printf("%s early smoke %d; largest coverage gain %.1f%%\n",
                prefix.c_str(),
                early_smoke,
                100.0 * largest_gain / (255 * AREA));
        }
        CHECK(early_smoke > AREA / 12);
        CHECK(largest_gain < 255 * AREA / 6);
        field.release();
    }

    void test_initialization_failures(CSmokeMaterial &material, const SSmokePrograms &programs) {
        auto incomplete = programs;
        incomplete.ids[SSmokePrograms::Pressure] = 0;
        CHECK(!material.initialize(incomplete));
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::texture_3d);
            CHECK(!material.initialize(programs));
            CHECK(!material.initialize(incomplete));
        }
        {
            gl_harness::FailAllocation fail(gl_harness::Allocation::texture_2d);
            CHECK(!material.initialize(programs));
            CSmokeField field;
            CHECK(!material.simulate(field, {}, {}, {}));
            CHECK(field.framebuffer() == 0);
        }
        require(material.initialize(programs));
        require(material.initialize(programs));
        CHECK(!material.initialize(incomplete));
        auto different = programs;
        std::swap(different.ids[SSmokePrograms::Pressure], different.ids[SSmokePrograms::Dye]);
        CHECK(!material.initialize(different));
        require(material.initialize(programs));
        material.release();
        require(material.initialize(programs));
        material.release();
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void test_field_failures(Harness &harness) {
        CSmokeField survivor;
        harness.frame = {};
        harness.frame.size = {TW, TH};
        harness.frame.seed = .37;
        harness.convert(survivor);
        const GLuint SURVIVOR = survivor.framebuffer();
        require(SURVIVOR != 0);
        // Fail early, halfway through, and at the last texture, then reject a paired
        // attachment after all textures are allocated. The survivor retains its fluid.
        for (const unsigned OCCURRENCE : {1U, 6U, 11U, 0U}) {
            CSmokeField pending;
            Image before;
            {
                gl_harness::FailAllocation fail(gl_harness::Allocation::texture_2d, OCCURRENCE);
                harness.frame.size.x += 32;
                harness.frame.time += 1.0 / 60;
                before = harness.render(survivor);
                CHECK(survivor.framebuffer() == SURVIVOR);
                {
                    const bool PAIR_FAILURE = OCCURRENCE == 0;
                    AllocationTrace trace{PAIR_FAILURE};
                    CHECK(!harness.material.simulate(
                        pending, harness.frame, harness.target, harness.images()));
                    CHECK(trace.rejected_pair == PAIR_FAILURE);
                    CHECK(trace.textures.size() == 11);
                    CHECK(trace.framebuffers.size() == 1);
                    for (const auto &texture : trace.textures) {
                        CHECK(texture.name != 0);
                        CHECK(texture.deletions == 1);
                        CHECK(glIsTexture(texture.name) == GL_FALSE);
                    }
                    for (const auto &framebuffer : trace.framebuffers) {
                        CHECK(framebuffer.name != 0);
                        CHECK(framebuffer.deletions == 1);
                        CHECK(glIsFramebuffer(framebuffer.name) == GL_FALSE);
                    }
                    CHECK(pending.framebuffer() == 0);
                }
                CHECK(harness.render(survivor) == before);
            }
            // The same failed field must be usable without release or reconstruction.
            require(harness.material.simulate(
                pending, harness.frame, harness.target, harness.images()));
            const GLuint RETRY = pending.framebuffer();
            require(RETRY != 0 && RETRY != SURVIVOR);
            CHECK(glIsFramebuffer(RETRY) == GL_TRUE);
            CHECK(harness.render(survivor) == before);
            pending.release();
            CHECK(pending.framebuffer() == 0);
            CHECK(glIsFramebuffer(RETRY) == GL_FALSE);
            CHECK(glIsFramebuffer(SURVIVOR) == GL_TRUE);
            CHECK(harness.render(survivor) == before);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        CHECK(alpha_sum(harness.render(survivor), 0, W) > 0);
        survivor.release();
        CHECK(survivor.framebuffer() == 0);
        CHECK(glIsFramebuffer(SURVIVOR) == GL_FALSE);
        // A released field can be primed at an earlier frame time, like a new field.
        harness.frame = {};
        harness.frame.size = {TW, TH};
        harness.frame.seed = .37;
        harness.frame.presence = harness.frame.fade = 1;
        harness.frame.smoke = .5;
        const Image REUSED = harness.render(survivor);
        require(survivor.framebuffer() != 0);
        CSmokeField fresh;
        CHECK(harness.render(fresh) == REUSED);
        harness.frame.time += 1.0 / 60;
        harness.frame.smoke = .8;
        CHECK(harness.render(fresh) == harness.render(survivor));
        fresh.release();
        survivor.release();
        CHECK(glGetError() == GL_NO_ERROR);
        harness.frame = {};
        harness.frame.size = {TW, TH};
        harness.frame.seed = .37;
    }

    void test_wake(Harness &harness) {
        // Wake smoke lives briefly: passing back and forth over the same area does not pile it
        // up, and once the window stops the trail away from it clears promptly.
        {
            constexpr int CW = 1024, CH = 512, CLEARANCE = 40;
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(5);
            const hyprimate::Vec2 SIZE{TW, TH};
            harness.canvas = {CW, CH};
            harness.target = {.origin = {0, 0}, .source_pos = {260, 196}, .source_size = SIZE};
            const auto AWAY = [&](const Image &image) {
                const int X0 = static_cast<int>(harness.target.source_pos.x) - CLEARANCE;
                const int Y0 = static_cast<int>(harness.target.source_pos.y) - CLEARANCE;
                long sum = 0;
                for (int y = 0; y < CH; ++y) {
                    for (int x = 0; x < CW; ++x) {
                        if (x < X0 || x >= X0 + TW + 2 * CLEARANCE || y < Y0 ||
                            y >= Y0 + TH + 2 * CLEARANCE) {
                            sum += image[static_cast<std::size_t>(y * CW + x)][3];
                        }
                    }
                }
                return sum;
            };
            const auto STEP = [&](double dx) {
                if (dx != 0) {
                    driver.drive({dx, 0});
                    harness.target.source_pos.x += dx;
                }
                driver.advance(1.0 / 60, SIZE);
                harness.frame = driver.frame(SIZE);
                return harness.render(field);
            };
            std::array<long, 6> passes{};
            for (int pass = 0; pass < 6; ++pass) {
                for (int i = 0; i < 30; ++i) {
                    passes[pass] = std::max(passes[pass], AWAY(STEP(pass % 2 == 0 ? 12.0 : -12.0)));
                }
            }
            CHECK(passes[1] > 100000 && passes[5] < passes[1]);
            const int CLEAR = static_cast<int>(std::ceil(60.0 * 1.1 * harness.frame.life));
            long rest = 0;
            for (int i = 0; i < CLEAR; ++i) {
                rest = AWAY(STEP(0));
            }
            CHECK(rest < passes[1] / 100);
            field.release();
            harness.canvas = {W, H};
            harness.target = {.origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        }
    }

    void test_wake_controls(Harness &harness) {
        struct Wake {
            int distant_opacity;
            int lingering_opacity;
            double width_squared;
        };
        const auto run = [&](hyprimate::SmokeParams params) {
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(5);
            harness.target.source_pos = {40, 68};
            const auto step = [&](double dx) {
                driver.drive({dx, 0});
                driver.advance(1.0 / 120, {TW, TH}, params, true);
                harness.target.source_pos.x += dx;
                harness.frame = driver.frame({TW, TH});
                return harness.render(field);
            };
            for (int i = 0; i < 47; ++i)
                step(6);
            const Image MOVING = step(6);
            const int LEFT = static_cast<int>(harness.target.source_pos.x);
            const int DISTANT = alpha_sum(MOVING, 20, LEFT - 65);
            // Measure lateral width across the near wake, excluding the main body.
            double weight = 0, moment = 0, second_moment = 0;
            for (int y = 20; y < H - 20; ++y) {
                for (int x = LEFT - 35; x < LEFT + 24; ++x) {
                    const double opacity = at(MOVING, x, y)[3];
                    weight += opacity;
                    moment += y * opacity;
                    second_moment += y * y * opacity;
                }
            }
            const double MEAN = weight > 0 ? moment / weight : 0;
            const double WIDTH_SQUARED = weight > 0 ? second_moment / weight - MEAN * MEAN : 0;
            for (int i = 0; i < 35; ++i)
                step(0);
            const int LINGERING = alpha_sum(step(0), 20, LEFT - 65);
            field.release();
            return Wake{DISTANT, LINGERING, WIDTH_SQUARED};
        };
        const auto baseline = run({});
        const auto short_reach = run({.trail_length = 35});
        const auto brief = run({.trail_lifetime = 0.1});
        const auto narrow = run({.trail_thickness = 0.25});
        const auto broad = run({.trail_thickness = 2.5});
        CHECK(baseline.distant_opacity > 10000);
        CHECK(short_reach.distant_opacity < 0.1 * baseline.distant_opacity);
        CHECK(baseline.lingering_opacity > 3000);
        CHECK(brief.lingering_opacity < 0.1 * baseline.lingering_opacity);
        CHECK(narrow.width_squared > 0 && broad.width_squared > narrow.width_squared);
        harness.target.source_pos = {40, 68};
        harness.frame = {};
    }

    void test_dragged_core_tracks_between_steps(Harness &harness) {
        CSmokeField field;
        const auto origin = harness.target.source_pos;
        harness.target.source_pos = {120, 68};
        harness.frame = {};
        harness.frame.size = {TW, TH};
        harness.frame.mass = 1;
        harness.frame.drag_follow = 1;
        harness.convert(field);
        const auto core_opacity = [&](const Image &image, hyprimate::Vec2 position) {
            const int X = static_cast<int>(position.x) + TW / 2;
            const int Y = static_cast<int>(position.y) + TH / 2;
            int sum = 0;
            for (int y = Y - 4; y <= Y + 4; ++y) {
                for (int x = X - 4; x <= X + 4; ++x)
                    sum += at(image, x, y)[3];
            }
            return sum;
        };
        const auto start = harness.target.source_pos;
        const int BEFORE = core_opacity(harness.render(field), start);
        CHECK(BEFORE > 5000);
        harness.frame.position.x += 55;
        harness.target.source_pos.x += 55;
        const Image BETWEEN = harness.render(field);
        CHECK(core_opacity(BETWEEN, harness.target.source_pos) > 0.85 * BEFORE);
        CHECK(core_opacity(BETWEEN, start) < 0.30 * BEFORE);
        harness.frame.time += 1.0 / 120;
        const Image STEPPED = harness.render(field);
        CHECK(core_opacity(STEPPED, harness.target.source_pos) > 0.75 * BEFORE);
        CHECK(core_opacity(STEPPED, start) < 0.40 * BEFORE);

        // A perpendicular change of direction follows the next observed drag sample.
        harness.frame.position.y += 45;
        harness.target.source_pos.y += 45;
        harness.frame.time += 1.0 / 120;
        CHECK(core_opacity(harness.render(field), harness.target.source_pos) > 0.65 * BEFORE);
        field.release();
        harness.target.source_pos = origin;
        harness.frame = {};
    }

    void test_substep(Harness &harness) {
        // A display frame between fluid substeps draws the stepped smoke where the world has
        // it: moving the window without stepping must not carry detached smoke along with it.
        {
            CSmokeField field;
            harness.frame = {};
            harness.frame.size = {TW, TH};
            harness.frame.seed = .37;
            harness.target.source_pos = {176, 68};
            harness.convert(field);
            for (int i = 0; i < 12; ++i) {
                harness.frame.time += 1.0 / 60;
                harness.frame.velocity = {360, 0};
                harness.frame.position.x += 6;
                harness.target.source_pos.x += 6;
                harness.render(field);
            }
            const Image STEPPED = harness.render(field);
            harness.frame.drag_follow = 1;
            harness.frame.mass = 1;
            harness.frame.position.x += 5;
            harness.target.source_pos.x += 5;
            const Image BETWEEN = harness.render(field);
            const int LEFT = static_cast<int>(harness.target.source_pos.x) - 40;
            const int RIGHT = static_cast<int>(harness.target.source_pos.x) + TW + 40;
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
    }

    void capture_transition(Harness &harness, const char *capture) {
        if (capture != nullptr) {
            std::filesystem::create_directories(capture);
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(7);
            harness.target.source_pos = {60, 68};
            const hyprimate::Vec2 SIZE{TW, TH};
            driver.begin(hyprimate::Transition::Open, 1.1, SIZE);
            for (int i = 0; i < 150; ++i) {
                if (i >= 70 && i < 100) {
                    driver.drive({6, 0});
                    harness.target.source_pos.x += 6;
                }
                if (i == 110) {
                    driver.begin(hyprimate::Transition::Close, 0.85, SIZE);
                }
                driver.advance(1.0 / 30, SIZE);
                harness.frame = driver.frame(SIZE);
                const Image IMAGE = harness.render(field);
                if (i % 10 == 0) {
                    save(std::filesystem::path(capture) /
                             ("frame-" + std::to_string(i / 10) + ".ppm"),
                        IMAGE);
                }
            }
            field.release();
        }
    }

    void capture_erratic(Harness &harness,
        CSmokeField &field,
        const char *capture,
        hyprimate::Vec2 size,
        int width,
        int height) {
        // Erratic dragging on a 144 Hz display: frames fall between fluid substeps, and the
        // direction flips faster than the wake can settle behind the window.
        hyprimate::Smoke erratic;
        erratic.seed(13);
        harness.target.source_pos = {200, 340};
        for (int i = 0; i < 380; ++i) {
            // Wide sweeps of about a third of a second, each with its own speed and wobble.
            const int SEGMENT = i / 50;
            const double SPEED = (SEGMENT % 2 == 0 ? 1.0 : -1.0) *
                                 (2300.0 + ((SEGMENT * 37) % 7) * 120.0) *
                                 (0.75 + 0.25 * std::sin(i * 0.9));
            const hyprimate::Vec2 STEP =
                i < 250 ? hyprimate::Vec2{SPEED / 144.0,
                              ((SEGMENT % 3) - 1) * 220.0 / 144.0 + 3.0 * std::sin(i * 1.3)}
                        : hyprimate::Vec2{};
            if (STEP.x != 0 || STEP.y != 0) {
                erratic.drive(STEP);
                harness.target.source_pos.x += STEP.x;
                harness.target.source_pos.y += STEP.y;
            }
            erratic.advance(1.0 / 144, size);
            harness.frame = erratic.frame(size);
            const Image IMAGE = harness.render(field);
            if (i % 6 == 0) {
                char name[32];
                (void)std::snprintf(name, sizeof name, "erratic-%03d.ppm", i);
                save(std::filesystem::path(capture) / name, IMAGE, width, height);
            }
        }
        field.release();
    }

    void test_resize(Harness &harness, const char *capture) {
        // Resizing must transport existing smoke, not erase and refill it. With the
        // source disabled, disappearing density cannot be hidden by fresh emission.
        for (const bool CENTRED : {false, true}) {
            CSmokeField field;
            harness.frame = {};
            harness.frame.size = {TW, TH};
            harness.frame.seed = .37;
            harness.target.source_pos = {176, 68};
            harness.target.source_size = {TW, TH};
            harness.convert(field);
            harness.frame.presence = 0;
            int previous = alpha_sum(harness.render(field), 0, W);
            CHECK(previous > 10000);
            for (int i = 0; i < 48; ++i) {
                const double DX = i < 24 ? -2.0 : 2.0;
                const double DY = i < 24 ? 2.0 : -2.0;
                harness.frame.size.x += DX;
                harness.frame.size.y += DY;
                harness.target.source_size = harness.frame.size;
                if (CENTRED) {
                    harness.target.source_pos.x -= DX * .5;
                    harness.target.source_pos.y -= DY * .5;
                } else {
                    harness.frame.position.x += DX * .5;
                    harness.frame.position.y += DY * .5;
                }
                harness.frame.time += 1.0 / 60;
                const Image RESIZED = harness.render(field);
                const int CURRENT = alpha_sum(RESIZED, 0, W);
                CHECK(CURRENT > previous * .65);
                // Rendering another monitor at the same time must not step again.
                CHECK(harness.render(field) == RESIZED);
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
    }

    void capture_wake(Harness &harness, const Image &pixels, const char *capture) {
        // Wake capture of a full-size window: a fast move, a sharp change of direction, then a
        // stop while the trail dissipates.
        if (capture != nullptr) {
            constexpr int CW = 1920, CH = 1080, WW = 640, WH = 400;
            std::filesystem::create_directories(capture);
            Image content(static_cast<std::size_t>(WW) * WH);
            for (int y = 0; y < WH; ++y) {
                for (int x = 0; x < WW; ++x) {
                    content[static_cast<std::size_t>(y * WW + x)] = {
                        static_cast<unsigned char>(40 + x * 200 / WW),
                        static_cast<unsigned char>(60 + y * 150 / WH),
                        static_cast<unsigned char>(x < WW / 2 ? 200 : 50),
                        255};
                }
            }
            harness.upload(content, WW, WH);
            CSmokeField field;
            hyprimate::Smoke driver;
            driver.seed(11);
            const hyprimate::Vec2 SIZE{WW, WH};
            harness.canvas = {CW, CH};
            harness.target = {.origin = {0, 0}, .source_pos = {120, 560}, .source_size = SIZE};
            for (int i = 0; i < 168; ++i) {
                const hyprimate::Vec2 STEP = i < 36   ? hyprimate::Vec2{24, -4}
                                             : i < 60 ? hyprimate::Vec2{-6, -16}
                                                      : hyprimate::Vec2{};
                if (STEP.x != 0 || STEP.y != 0) {
                    driver.drive(STEP);
                    harness.target.source_pos.x += STEP.x;
                    harness.target.source_pos.y += STEP.y;
                }
                driver.advance(1.0 / 60, SIZE);
                harness.frame = driver.frame(SIZE);
                const Image IMAGE = harness.render(field);
                if (i % 3 == 0) {
                    char name[32];
                    (void)std::snprintf(name, sizeof name, "trail-%03d.ppm", i);
                    save(std::filesystem::path(capture) / name, IMAGE, CW, CH);
                }
            }
            field.release();
            capture_erratic(harness, field, capture, SIZE, CW, CH);
            harness.upload(pixels);
            harness.canvas = {W, H};
            harness.target = {.origin = {0, 0}, .source_pos = {40, 68}, .source_size = {TW, TH}};
        }
    }

}  // namespace

int main(int argc, char **argv) {
    const char *capture = std::getenv("SMOKE_CAPTURE_DIR");
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--capture-dir") == 0) {
            capture = argv[++i];
        }
    }
    const gl_harness::CContext CONTEXT;
    if (!CONTEXT.valid()) {
        return 1;
    }
    {
        Image pixels(static_cast<std::size_t>(TW) * TH);
        for (int y = 0; y < TH; ++y) {
            for (int x = 0; x < TW; ++x) {
                pixels[static_cast<std::size_t>(y * TW + x)] = {static_cast<unsigned char>(30 + x),
                    static_cast<unsigned char>(20 + y),
                    static_cast<unsigned char>(x < 80 ? 180 : 35),
                    255};
            }
        }
        Harness h;
        SSmokePrograms programs;
        const std::array<const char *, SSmokePrograms::ProgramCount> SOURCES{
            smoke_shader::VELOCITY.data(),
            smoke_shader::DIVERGENCE.data(),
            smoke_shader::PRESSURE.data(),
            smoke_shader::PROJECT.data(),
            smoke_shader::ADVECT.data(),
            smoke_shader::DYE.data(),
            smoke_shader::COMPOSITE.data()};
        for (std::size_t i = 0; i < SOURCES.size(); ++i) {
            programs.ids[i] = gl_harness::build_program(smoke_shader::VERTEX.data(), SOURCES[i]);
            require(programs.ids[i] != 0);
        }
        test_initialization_failures(h.material, programs);
        require(h.material.initialize(programs));
        glGenTextures(1, &h.texture);
        glBindTexture(GL_TEXTURE_2D, h.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        test_smoke_tint(h);
        h.upload(pixels);
        h.frame.size = {TW, TH};
        h.frame.seed = 0.37;
        test_field_failures(h);
        test_stationary_close_keeps_solid_content_in_place(h);
        for (const int frames : {18, 84, 150}) {  // 300, 1400, and 2500 ms at 60 Hz
            for (const bool edge : {false, true}) {
                test_smoke_forms_the_window(h, capture, frames, edge);
            }
        }
        h.frame = {};
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

        test_resize(h, capture);

        test_wake(h);
        test_wake_controls(h);
        test_dragged_core_tracks_between_steps(h);

        test_substep(h);

        capture_transition(h, capture);

        capture_wake(h, pixels, capture);

        // Full-HD cost of one simulation step plus the composite.
        CSmokeField field;
        h.frame = {};
        h.frame.size = {1280, 720};
        h.frame.presence = h.frame.fade = 1;
        h.canvas = {1920, 1080};
        h.target = {.origin = {0, 0}, .source_pos = {320, 180}, .source_size = {1280, 720}};
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
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - START)
                .count() /
            5.0;
        (void)std::fprintf(
            stderr, "Smoke 1920x1080 fluid step + composite: %.2f ms/frame (5 frames)\n", ELAPSED);
        CHECK(glGetError() == GL_NO_ERROR);
        field.release();
        h.material.release();
        for (const GLuint PROGRAM : programs.ids) {
            glDeleteProgram(PROGRAM);
        }
        glDeleteTextures(1, &h.texture);
    }
    return g_failures == 0 ? 0 : 1;
}
