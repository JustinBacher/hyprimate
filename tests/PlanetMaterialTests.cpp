#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "planet_shaders_generated.hpp"

namespace {
    namespace shader = planet_shader;
    static_assert(shader::VERTEX.size() == 8587 && shader::FRAGMENT.size() == 10214);
    static_assert(shader::QUAD_VERTEX.size() == 332 && shader::QUAD_FRAGMENT.size() == 2163);
    static_assert(shader::SKY_VERTEX.size() == 203 && shader::SKY_FRAGMENT.size() == 2346);
    static_assert([] {
        constexpr std::array SOURCES{shader::VERTEX,
            shader::FRAGMENT,
            shader::QUAD_VERTEX,
            shader::QUAD_FRAGMENT,
            shader::SKY_VERTEX,
            shader::SKY_FRAGMENT};
        for (const auto source : SOURCES) {
            if (!source.starts_with("#version 300 es\n") || !source.ends_with('\n') ||
                source.find('\0') != std::string_view::npos || source.data()[source.size()] != '\0') {
                return false;
            }
        }
        return true;
    }());
    constexpr std::array<float, 16> IDENTITY4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr std::array<float, 9> IDENTITY3{1, 0, 0, 0, 1, 0, 0, 0, 1};
    using Pixel = std::array<std::uint8_t, 4>;

    void scalar(GLuint program, const char *name, float value) {
        glUniform1f(glGetUniformLocation(program, name), value);
    }
    void integer(GLuint program, const char *name, int value) {
        glUniform1i(glGetUniformLocation(program, name), value);
    }
    GLuint texture(const Pixel &pixel, int unit) {
        GLuint result = 0;
        glGenTextures(1, &result);
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, result);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        return result;
    }
    Pixel read() {
        Pixel result{};
        glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        return result;
    }
    void near(const Pixel &actual, const Pixel &expected) {
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) <= 1);
        }
    }
    void image_uniforms(GLuint program) {
        integer(program, "image", 0);
        integer(program, "backdrop", 1);
        glUniform4f(glGetUniformLocation(program, "uvTransform"), 0, 1, 1, -1);
        scalar(program, "opacity", 1);
        scalar(program, "liveMix", 1);
        scalar(program, "secondaryLiveMix", 1);
    }
    void matrix(GLuint program, const char *name, const std::array<float, 16> &value) {
        glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, value.data());
    }

    void check_mesh_endpoints(GLuint mesh) {
        glUseProgram(mesh);
        image_uniforms(mesh);
        matrix(mesh, "viewProjection", IDENTITY4);
        glUniformMatrix3fv(
            glGetUniformLocation(mesh, "planetRotation"), 1, GL_FALSE, IDENTITY3.data());
        glUniform3f(glGetUniformLocation(mesh, "planetCenter"), 0, 0, 0);
        glUniform3f(glGetUniformLocation(mesh, "cameraPosition"), 0, 0, 4);
        glUniform3f(glGetUniformLocation(mesh, "lightDirection"), 0, 0, -1);
        scalar(mesh, "curvature", 0);
        scalar(mesh, "endpointEnvelope", 0);
        scalar(mesh, "elevation", 0.5F);
        scalar(mesh, "depthStrength", 0.5F);
        integer(mesh, "uDepthEnabled", 1);
        integer(mesh, "depthSource", 1);
        integer(mesh, "depthImage", 2);
        integer(mesh, "samplingQuality", 2);
        integer(mesh, "lightingEnabled", 1);
        integer(mesh, "cloudsEnabled", 1);
        integer(mesh, "atmosphereEnabled", 1);
        scalar(mesh, "cloudOpacity", 1);
        scalar(mesh, "atmosphereIntensity", 1);
        integer(mesh, "layer", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {120, 80, 40, 128});
        integer(mesh, "parametricMesh", 1);
        scalar(mesh, "aspect", 1);
        scalar(mesh, "radius", 0.9F);
        scalar(mesh, "coverage", 0.7F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {120, 80, 40, 128});
        integer(mesh, "fullSphere", 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {120, 80, 40, 128});
        integer(mesh, "fullSphere", 0);
        integer(mesh, "parametricMesh", 0);
        integer(mesh, "reconstructLayer", 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {110, 60, 10, 128});
        integer(mesh, "previousImage", 3);
        integer(mesh, "previousBackdrop", 1);
        integer(mesh, "previousReconstruct", 1);
        scalar(mesh, "liveMix", 0.5F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {88, 65, 43, 96});
        scalar(mesh, "liveMix", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {65, 70, 75, 64});
        scalar(mesh, "liveMix", 1);
    }
    void check_mesh_silhouette(GLuint mesh) {
        // Attached layers have a genuinely empty unseen side, not a reflection.
        integer(mesh, "layer", 1);
        scalar(mesh, "endpointEnvelope", 1);
        scalar(mesh, "elevation", 0);
        scalar(mesh, "depthStrength", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {0, 0, 0, 0});
        integer(mesh, "layer", 0);
        integer(mesh, "lightingEnabled", 0);
        integer(mesh, "cloudsEnabled", 0);
        integer(mesh, "atmosphereEnabled", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {1, 2, 3, 128});
        // Near-native antialiasing belongs outside the chart, so its perimeter
        // is continuous with the exact endpoint. Curvature moves it inward.
        scalar(mesh, "endpointEnvelope", 1.0e-9F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        Pixel edge_pixel{};
        glReadPixels(0, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, edge_pixel.data());
        near(edge_pixel, {110, 60, 10, 128});
        near(read(), {110, 60, 10, 128});
        scalar(mesh, "endpointEnvelope", 0.4F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, edge_pixel.data());
        CHECK(edge_pixel[0] < 60);
        scalar(mesh, "endpointEnvelope", 1);
    }
    void check_clouds_and_indirect_light(GLuint mesh) {
        // The independent elevated cloud shell also covers the dark unseen base.
        // Its view-ray intersection changes with height and its pattern moves
        // independently with time; it is not a surface-normal decal.
        integer(mesh, "layer", 0);
        scalar(mesh, "planetRadius", 1);
        scalar(mesh, "cloudHeight", 0.035F);
        scalar(mesh, "cloudCoverage", 0.9F);
        scalar(mesh, "cloudOpacity", 1);
        scalar(mesh, "cloudSpeed", 0.25F);
        auto const FRAME = [&] {
            std::array<std::uint8_t, std::size_t{32} * 32 * 4> pixels{};
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const auto NO_CLOUDS = FRAME();
        integer(mesh, "cloudsEnabled", 1);
        const auto LOW_CLOUDS = FRAME();
        CHECK(NO_CLOUDS != LOW_CLOUDS);
        scalar(mesh, "cloudHeight", 0.25F);
        CHECK(LOW_CLOUDS != FRAME());
        scalar(mesh, "cloudHeight", 0.035F);
        scalar(mesh, "time", 12);
        CHECK(LOW_CLOUDS != FRAME());
        integer(mesh, "cloudsEnabled", 0);
        CHECK(NO_CLOUDS == FRAME());
        // Indirect illumination must illuminate the dark unseen hemisphere,
        // and the independent lighting toggle must disable it again.
        integer(mesh, "lightingEnabled", 1);
        scalar(mesh, "nightBrightness", 0.45F);
        scalar(mesh, "indirectBrightness", 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        const auto UNLIT_NIGHT = read();
        scalar(mesh, "indirectBrightness", 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        const auto BOUNCED_NIGHT = read();
        CHECK(BOUNCED_NIGHT[0] > UNLIT_NIGHT[0]);
        CHECK(BOUNCED_NIGHT[1] > UNLIT_NIGHT[1]);
        CHECK(BOUNCED_NIGHT[2] > UNLIT_NIGHT[2]);
        integer(mesh, "lightingEnabled", 0);
        CHECK(NO_CLOUDS == FRAME());
    }
    void check_hud_reconstruction(GLuint quad) {
        glUseProgram(quad);
        image_uniforms(quad);
        matrix(quad, "viewProjection", IDENTITY4);
        auto model = IDENTITY4;
        model[0] = 2;
        model[5] = 2;
        matrix(quad, "model", model);
        glUniform4f(glGetUniformLocation(quad, "imageRect"), 0, 0, 1, 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {120, 80, 40, 128});
        integer(quad, "reconstructLayer", 1);
        scalar(quad, "opacity", 0.5F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {55, 30, 5, 64});
        integer(quad, "secondaryImage", 3);
        integer(quad, "secondaryBackdrop", 1);
        integer(quad, "secondaryReconstruct", 1);
        scalar(quad, "mixAmount", 0.5F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {44, 33, 21, 48});
        // Each HUD endpoint may independently transition old->live captures;
        // recover all four premultiplied layers before the endpoint blend.
        integer(quad, "previousImage", 3);
        integer(quad, "previousBackdrop", 1);
        integer(quad, "previousReconstruct", 1);
        scalar(quad, "liveMix", 0.5F);
        integer(quad, "secondaryPreviousImage", 0);
        integer(quad, "secondaryPreviousBackdrop", 1);
        integer(quad, "secondaryPreviousReconstruct", 1);
        scalar(quad, "secondaryLiveMix", 0.25F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {47, 32, 17, 52});
        scalar(quad, "liveMix", 1);
        scalar(quad, "secondaryLiveMix", 1);
        scalar(quad, "mixAmount", 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {33, 35, 38, 32});
        scalar(quad, "mixAmount", 0);
        integer(quad, "reconstructLayer", 0);
        integer(quad, "forceOpaque", 1);
        scalar(quad, "opacity", 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        near(read(), {120, 80, 40, 255});
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void endpoint_and_reconstruction(GLuint mesh, GLuint quad) {
        const GLuint IMAGE = texture({120, 80, 40, 128}, 0);
        const GLuint BACKDROP = texture({20, 40, 60, 255}, 1);
        const GLuint DEPTH = texture({255, 255, 255, 255}, 2);
        const GLuint SECONDARY = texture({80, 100, 120, 64}, 3);
        GLuint vao = 0;
        GLuint vbo = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // Deliberately put the spherical chart on the unseen hemisphere. At
        // native endpoints neither the visibility mask nor displacement applies.
        constexpr std::array<float, 32> VERTICES{-1,
            -1,
            0,
            -1,
            -1,
            -1,
            0,
            1,
            1,
            -1,
            0,
            1,
            -1,
            -1,
            1,
            1,
            -1,
            1,
            0,
            -1,
            1,
            -1,
            0,
            0,
            1,
            1,
            0,
            1,
            1,
            -1,
            1,
            0};
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sizeof(VERTICES)),
            VERTICES.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glVertexAttribPointer(1,
            3,
            GL_FLOAT,
            GL_FALSE,
            8 * sizeof(float),
            std::bit_cast<const void *>(std::uintptr_t{3 * sizeof(float)}));
        glVertexAttribPointer(2,
            2,
            GL_FLOAT,
            GL_FALSE,
            8 * sizeof(float),
            std::bit_cast<const void *>(std::uintptr_t{6 * sizeof(float)}));
        for (GLuint i = 0; i < 3; ++i) {
            glEnableVertexAttribArray(i);
        }

        check_mesh_endpoints(mesh);
        check_mesh_silhouette(mesh);
        check_clouds_and_indirect_light(mesh);
        check_hud_reconstruction(quad);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        const std::array TEXTURES{IMAGE, BACKDROP, DEPTH, SECONDARY};
        glDeleteTextures(static_cast<GLsizei>(TEXTURES.size()), TEXTURES.data());
    }

    void stable_sky(GLuint sky) {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUseProgram(sky);
        constexpr GLsizei SKY_SIZE = 256;
        GLuint target = 0;
        GLuint framebuffer = 0;
        glGenTextures(1, &target);
        glBindTexture(GL_TEXTURE_2D, target);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, SKY_SIZE, SKY_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glViewport(0, 0, SKY_SIZE, SKY_SIZE);
        matrix(sky, "inverseViewProjection", IDENTITY4);
        glUniform3f(glGetUniformLocation(sky, "cameraPosition"), 0, 0, -2);
        scalar(sky, "starDensity", 0);
        scalar(sky, "endpointEnvelope", 0.5F);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        near(read(), {0, 0, 1, 255});
        scalar(sky, "starDensity", 1);
        glUniform3f(glGetUniformLocation(sky, "starOffsetNear"), 0.25F, 0.5F, 0.75F);
        glUniform3f(glGetUniformLocation(sky, "starOffsetMid"), 0.5F, 0.75F, 0.25F);
        glUniform3f(glGetUniformLocation(sky, "starOffsetFar"), 0.75F, 0.25F, 0.5F);
        scalar(sky, "endpointEnvelope", 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        near(read(), {0, 0, 0, 255});
        scalar(sky, "endpointEnvelope", 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<std::uint8_t, std::size_t{256} * 256 * 4> first{};
        std::array<std::uint8_t, std::size_t{256} * 256 * 4> second{};
        glReadPixels(0, 0, SKY_SIZE, SKY_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, first.data());
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, SKY_SIZE, SKY_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, second.data());
        CHECK(first == second);
        bool visible_stars = false;
        for (std::size_t i = 0; i < first.size(); i += 4) {
            visible_stars = visible_stars || first[i] > 8 || first[i + 1] > 8 || first[i + 2] > 8;
        }
        CHECK(visible_stars);
        // Floating-origin rebasing changes both relative camera and inverse VP,
        // but does not change the bounded world-space star offsets or pixels.
        auto rebased = IDENTITY4;
        rebased[12] = 100;
        rebased[13] = 200;
        rebased[14] = 302;
        matrix(sky, "inverseViewProjection", rebased);
        glUniform3f(glGetUniformLocation(sky, "cameraPosition"), 100, 200, 300);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, SKY_SIZE, SKY_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, second.data());
        CHECK(first == second);
        CHECK(glGetError() == GL_NO_ERROR);
        glDeleteVertexArrays(1, &vao);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &target);
    }
}  // namespace

int main() {
    gl_harness::CContext const CONTEXT(32, 32);
    if (!CONTEXT.valid()) {
        (void)std::fprintf(stderr, "PlanetMaterialTests: GLES 3 context unavailable\n");
        return 77;
    }
    const GLuint MESH = gl_harness::build_program(shader::VERTEX, shader::FRAGMENT);
    const GLuint QUAD = gl_harness::build_program(shader::QUAD_VERTEX, shader::QUAD_FRAGMENT);
    const GLuint SKY = gl_harness::build_program(shader::SKY_VERTEX, shader::SKY_FRAGMENT);
    CHECK(MESH != 0);
    CHECK(QUAD != 0);
    CHECK(SKY != 0);
    glViewport(0, 0, 32, 32);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    if (MESH != 0 && QUAD != 0) {
        endpoint_and_reconstruction(MESH, QUAD);
    }
    if (SKY != 0) {
        stable_sky(SKY);
    }
    glDeleteProgram(MESH);
    glDeleteProgram(QUAD);
    glDeleteProgram(SKY);
    return g_failures == 0 ? 0 : 1;
}
