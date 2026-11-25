#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>

// Extension prototypes require core GLES types and calling conventions first.
#include <GLES2/gl2ext.h>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "PlanetRendererTestFixture.hpp"

namespace planet_renderer_tests {
    using namespace hyprimate::workspace;

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
        check_raw_endpoints(
            renderer, textures, JOURNEY, settings, width, height, source, destination, actual);
        if (!profile) {
            check_reversal_landing(
                renderer, textures, JOURNEY, settings, width, height, source, destination);
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
        check_renderer_ownership(
            renderer, textures, JOURNEY, settings, width, height, destination, SRC, DST, OUT);
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
}  // namespace planet_renderer_tests
int main(int argc, char *const *argv) {
    const bool PROFILE = argc > 1 && std::string_view(argv[1]) == "--profile";
    const int WIDTH_ARG = PROFILE && argc > 2
                              ? planet_renderer_tests::profile_argument(
                                  argv[2], {.minimum = 16, .maximum = 7680})
                              : planet_renderer_tests::WIDTH;
    const int HEIGHT_ARG = PROFILE && argc > 3
                               ? planet_renderer_tests::profile_argument(
                                   argv[3], {.minimum = 16, .maximum = 4320})
                               : planet_renderer_tests::HEIGHT;
    const int FRAMES = PROFILE && argc > 4
                           ? planet_renderer_tests::profile_argument(
                               argv[4], {.minimum = 1, .maximum = 10000})
                           : 120;
    const gl_harness::CContext CONTEXT(WIDTH_ARG, HEIGHT_ARG);
    if (!CONTEXT.valid()) {
        return 77;
    }
    planet_renderer_tests::run(PROFILE, WIDTH_ARG, HEIGHT_ARG, FRAMES);
    return g_failures == 0 ? 0 : 1;
}
