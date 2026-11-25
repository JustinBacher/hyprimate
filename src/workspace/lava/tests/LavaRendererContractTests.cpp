#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>

#include <GLES3/gl32.h>

#include "LavaRendererTestFixture.hpp"
#include "Check.hpp"

namespace lava_renderer_tests {
    constexpr int WIDTH = 128;
    constexpr int HEIGHT = 96;
    Material default_material(int variant) {
        Material material{};
        material.variant = variant;
        material.sharedA = {0.72F, 0.80F, 0.68F, 0.55F};
        material.sharedB = {0.75F, 0.35F, 0.15F, 0.08F};
        switch (variant) {
            case 0:
                material.specialA = {0.45F, 0.40F, 0.30F, 0.F};
                break;
            case 1:
                material.specialA = {0.75F, 0.28F, 0.12F, 0.80F};
                break;
            case 2:
                material.specialA = {0.65F, 0.50F, 0.40F, 0.55F};
                break;
            case 3:
                material.specialA = {0.55F, 0.70F, 0.60F, 0.35F};
                material.specialB = {0.25F, 0.45F};
                break;
            default:
                break;
        }
        return material;
    }
    GLuint framebuffer_for(GLuint texture) {
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        return fbo;
    }

    Pixels read_output(GLuint fbo, int width, int height) {
        Pixels pixels(static_cast<std::size_t>(width) * height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    }

    bool draw(Renderer &renderer,
        GLuint source,
        GLuint destination,
        GLuint output,
        GLuint fbo,
        int width,
        int height,
        double progress,
        Direction direction,
        const Material &material,
        WindowPlanes planes) {
        return renderer.draw_raw(
            source, destination, output, fbo, width, height, progress, direction, material, planes);
    }
    namespace {
    struct GlState {
        GLint read_framebuffer = 0;
        GLint draw_framebuffer = 0;
        GLint active_texture = 0;
        GLint program = 0;
        GLint vertex_array = 0;
        std::array<GLint, 4> viewport{};
        std::array<GLint, 6> textures{};
        std::array<GLint, 6> samplers{};
        std::array<GLboolean, 5> caps{};
        bool operator==(const GlState &) const = default;
    };

    GlState gl_state() {
        GlState state{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.read_framebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.draw_framebuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state.active_texture);
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertex_array);
        glGetIntegerv(GL_VIEWPORT, state.viewport.data());
        for (int unit = 0; unit != 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[static_cast<std::size_t>(unit)]);
            glGetIntegerv(GL_SAMPLER_BINDING, &state.samplers[static_cast<std::size_t>(unit)]);
        }
        glActiveTexture(static_cast<GLenum>(state.active_texture));
        constexpr std::array<GLenum, 5> CAPS{
            GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_STENCIL_TEST};
        for (std::size_t i = 0; i < CAPS.size(); ++i) {
            state.caps[i] = glIsEnabled(CAPS[i]);
        }
        return state;
    }
    }

    void check_binding_restore(Renderer &renderer,
        GLuint source,
        GLuint destination,
        GLuint output,
        GLuint fbo,
        int width,
        int height,
        const Material &material,
        WindowPlanes planes) {
        const GlState ORIGINAL = gl_state();
        GLuint vao = 0;
        GLuint sampler = 0;
        glGenVertexArrays(1, &vao);
        glGenSamplers(1, &sampler);
        glBindVertexArray(vao);
        glBindSampler(0, sampler);
        glBindSampler(1, sampler);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, destination);
        glActiveTexture(GL_TEXTURE3);
        glViewport(7, 9, 31, 27);
        glEnable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        const GlState BEFORE = gl_state();
        CHECK(draw(renderer,
            source,
            destination,
            output,
            fbo,
            width,
            height,
            0.43,
            Direction::Forward,
            material,
            planes));
        CHECK(gl_state() == BEFORE);

        glBindVertexArray(static_cast<GLuint>(ORIGINAL.vertex_array));
        glUseProgram(static_cast<GLuint>(ORIGINAL.program));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(ORIGINAL.read_framebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(ORIGINAL.draw_framebuffer));
        glViewport(
            ORIGINAL.viewport[0], ORIGINAL.viewport[1], ORIGINAL.viewport[2], ORIGINAL.viewport[3]);
        constexpr std::array<GLenum, 5> CAPS{
            GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_STENCIL_TEST};
        for (std::size_t i = 0; i < CAPS.size(); ++i) {
            if (ORIGINAL.caps[i] != 0U) {
                glEnable(CAPS[i]);
            } else {
                glDisable(CAPS[i]);
            }
        }
        for (int unit = 0; unit != 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D,
                static_cast<GLuint>(ORIGINAL.textures[static_cast<std::size_t>(unit)]));
            glBindSampler(static_cast<GLuint>(unit),
                static_cast<GLuint>(ORIGINAL.samplers[static_cast<std::size_t>(unit)]));
        }
        glActiveTexture(static_cast<GLenum>(ORIGINAL.active_texture));
        glDeleteSamplers(1, &sampler);
        glDeleteVertexArrays(1, &vao);
    }
    namespace {
    GLuint float_texture(const std::array<float, 4> &pixel, GLenum internal_format) {
        std::vector<float> pixels(static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            std::ranges::copy(pixel, pixels.data() + i);
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internal_format),
            WIDTH,
            HEIGHT,
            0,
            GL_RGBA,
            GL_FLOAT,
            pixels.data());
        return texture;
    }

    bool check_extended_range(Renderer &renderer, GLenum internal_format, const char *format_name) {
        const std::array<float, 4> SOURCE_COLOR{0.75F, 1.10F, 1.45F, 1.F};
        const std::array<float, 4> DESTINATION_COLOR{2.25F, 1.75F, 1.30F, 1.F};
        const GLuint SOURCE = float_texture(SOURCE_COLOR, internal_format);
        const GLuint DESTINATION = float_texture(DESTINATION_COLOR, internal_format);
        GLuint output = 0;
        glGenTextures(1, &output);
        glBindTexture(GL_TEXTURE_2D, output);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internal_format),
            WIDTH,
            HEIGHT,
            0,
            GL_RGBA,
            GL_FLOAT,
            nullptr);
        const GLuint FBO = framebuffer_for(output);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &FBO);
            glDeleteTextures(1, &output);
            glDeleteTextures(1, &DESTINATION);
            glDeleteTextures(1, &SOURCE);
            return false;
        }
        const auto READ_FLOAT = [&] {
            std::vector<float> pixels(static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_FLOAT, pixels.data());
            return pixels;
        };
        const Material MATERIAL = default_material(3);
        CHECK(draw(renderer,
            SOURCE,
            DESTINATION,
            output,
            FBO,
            WIDTH,
            HEIGHT,
            1.0,
            Direction::Forward,
            MATERIAL));
        const std::vector<float> ENDPOINT = READ_FLOAT();
        for (std::size_t i = 0; i < ENDPOINT.size(); i += 4) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                CHECK(std::abs(ENDPOINT[i + channel] - DESTINATION_COLOR[channel]) < 0.003F);
            }
        }
        CHECK(ENDPOINT[0] > 2.F);
        CHECK(draw(renderer,
            SOURCE,
            DESTINATION,
            output,
            FBO,
            WIDTH,
            HEIGHT,
            0.9999,
            Direction::Forward,
            MATERIAL));
        const std::vector<float> NEAR_END = READ_FLOAT();
        float maximum_error = 0.F;
        for (std::size_t i = 0; i < NEAR_END.size(); i += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                maximum_error = std::max(
                    maximum_error, std::abs(NEAR_END[i + channel] - DESTINATION_COLOR[channel]));
            }
        }
        CHECK(maximum_error < 0.03F);
        CHECK(NEAR_END[0] > 2.F);
        std::printf("extended-range %s capture/output: supported near_end_max_error=%.5f\n",
            format_name,
            maximum_error);
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &output);
        glDeleteTextures(1, &DESTINATION);
        glDeleteTextures(1, &SOURCE);
        return true;
    }
    void check_extended_formats(Renderer &renderer) {
        if (check_extended_range(renderer, GL_RGBA16F, "RGBA16F")) {
            return;
        }
        if (check_extended_range(renderer, GL_RGBA32F, "RGBA32F")) {
            return;
        }
        std::puts(
            "extended-range float capture/output: skipped (RGBA16F/RGBA32F color-renderable "
            "unavailable)");
    }
    }
    void check_gl_contract(Renderer &renderer,
        GLuint source_texture,
        GLuint destination_texture,
        GLuint output,
        GLuint fbo,
        WindowPlanes burn_planes,
        const Material &base_material) {
        const GLuint SOURCE_TEXTURE = source_texture;
        const GLuint DESTINATION_TEXTURE = destination_texture;
        const GLuint OUTPUT = output;
        const GLuint FBO = fbo;
        const Material &BASE = base_material;
        // Invalid calls are rejected before touching the caller's output or GL bindings/state.
        const Pixels SENTINEL(static_cast<std::size_t>(WIDTH) * HEIGHT * 4, 0x5a);
        glBindTexture(GL_TEXTURE_2D, OUTPUT);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, SENTINEL.data());
        const auto BEFORE = read_output(FBO, WIDTH, HEIGHT);
        const auto EXPECT_REJECT = [&](GLuint src,
                                       GLuint dst,
                                       GLuint out,
                                       GLuint target_fbo,
                                       int width,
                                       int height,
                                       double progress,
                                       Direction direction,
                                       Material material,
                                       WindowPlanes planes = {}) {
            const GlState STATE_BEFORE = gl_state();
            CHECK(!draw(renderer,
                src,
                dst,
                out,
                target_fbo,
                width,
                height,
                progress,
                direction,
                material,
                planes));
            CHECK(gl_state() == STATE_BEFORE);
            CHECK(read_output(FBO, WIDTH, HEIGHT) == BEFORE);
        };
        EXPECT_REJECT(
            0, DESTINATION_TEXTURE, OUTPUT, FBO, WIDTH, HEIGHT, 0.4, Direction::Forward, BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            0,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            std::numeric_limits<double>::quiet_NaN(),
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            -0.01,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            1.01,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            0,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            SOURCE_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            SOURCE_TEXTURE,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            DESTINATION_TEXTURE,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        Material bad_variant = BASE;
        bad_variant.variant = 4;
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            bad_variant);

        GLuint deleted_texture = 0;
        glGenTextures(1, &deleted_texture);
        glDeleteTextures(1, &deleted_texture);
        EXPECT_REJECT(deleted_texture,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        GLuint empty_fbo = 0;
        glGenFramebuffers(1, &empty_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, empty_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            empty_fbo,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        glDeleteFramebuffers(1, &empty_fbo);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            empty_fbo,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);

        GLuint level_one_texture = 0;
        GLuint level_one_fbo = 0;
        glGenTextures(1, &level_one_texture);
        glBindTexture(GL_TEXTURE_2D, level_one_texture);
        // Immutable storage gives both levels a complete, sampleable mip chain.
        glTexStorage2D(GL_TEXTURE_2D, 2, GL_RGBA8, WIDTH, HEIGHT);
        const Pixels LEVEL_ONE_SENTINEL(
            static_cast<std::size_t>(WIDTH / 2) * (HEIGHT / 2) * 4, 0x35);
        glTexSubImage2D(GL_TEXTURE_2D,
            1,
            0,
            0,
            WIDTH / 2,
            HEIGHT / 2,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            LEVEL_ONE_SENTINEL.data());
        glGenFramebuffers(1, &level_one_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, level_one_fbo);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, level_one_texture, 1);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        CHECK(read_output(level_one_fbo, WIDTH / 2, HEIGHT / 2) == LEVEL_ONE_SENTINEL);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            level_one_texture,
            level_one_fbo,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            BASE);
        CHECK(read_output(level_one_fbo, WIDTH / 2, HEIGHT / 2) == LEVEL_ONE_SENTINEL);
        glDeleteFramebuffers(1, &level_one_fbo);
        glDeleteTextures(1, &level_one_texture);
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            static_cast<Direction>(77),
            BASE);
        Material missing_planes_material = BASE;
        missing_planes_material.burn_up = true;
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            missing_planes_material);
        WindowPlanes aliased_planes = burn_planes;
        aliased_planes.source_windows = OUTPUT;
        EXPECT_REJECT(SOURCE_TEXTURE,
            DESTINATION_TEXTURE,
            OUTPUT,
            FBO,
            WIDTH,
            HEIGHT,
            0.4,
            Direction::Forward,
            missing_planes_material,
            aliased_planes);
        check_extended_formats(renderer);
    }
}
