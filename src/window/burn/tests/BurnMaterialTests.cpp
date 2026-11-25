#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Check.hpp"
#include "GlAllocationFault.hpp"
#include "GlHarness.hpp"
#include "window/burn/BurnGroundDebris.hpp"
#include "window/burn/BurnGroundShader.hpp"
#include "window/burn/BurnMaterial.hpp"
#include "window/burn/BurnShaders.hpp"

namespace {
    constexpr int WIDTH = 96, HEIGHT = 96;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;
    constexpr hyprimate::Box CAPTURE{17, 31, WIDTH, HEIGHT};

    struct Target {
        GLuint framebuffer = 0, texture = 0;
        bool valid = false;
        explicit Target(int width = WIDTH, int height = HEIGHT) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            CHECK(valid);
        }
        ~Target() {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteTextures(1, &texture);
        }
        Target(const Target &) = delete;
        Target &operator=(const Target &) = delete;
    };
    Image read(const Target &target) {
        Image image(WIDTH * HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        return image;
    }
    GLuint upload(const Image &pixels) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return texture;
    }
    SBurnTarget placement() {
        return {.canvas = {WIDTH, HEIGHT},
            .origin = {CAPTURE.x, CAPTURE.y},
            .source = CAPTURE,
            .texture_origin = {CAPTURE.x, CAPTURE.y},
            .texture_size = {WIDTH, HEIGHT},
            .alpha = 1,
            .output_top_first = true};
    }
    void advance(hyprimate::BurnSimulation &simulation, int steps) {
        for (int index = 0; index < steps; ++index) simulation.advance(.01, CAPTURE);
    }
    void near(Pixel actual, Pixel expected) {
        for (unsigned channel = 0; channel < 4; ++channel)
            CHECK(std::abs(static_cast<int>(actual[channel]) - expected[channel]) <= 2);
    }
    bool allocation_failure_retry(
        GLuint program, const Target &target, GLuint texture, const Image &source) {
        hyprimate::BurnParams params;
        params.duration = 1;
        params.ember_density = params.fragment_density = 0;
        params.consume = true;
        hyprimate::BurnSimulation simulation;
        simulation.open(CAPTURE, params, 29);
        advance(simulation, 150);
        CHECK(simulation.front().progress == 1);
        if (simulation.front().progress != 1) return false;
        const auto DRAW =
            [&](CBurnMaterial &material, const SBurnTarget &placement, const Target &output) {
                const bool OK = material.draw(simulation, placement, texture, output.framebuffer);
                CHECK(OK);
                if (!OK) return false;
                const auto IMAGE = read(output);
                for (std::size_t index = 0; index < source.size(); ++index)
                    near(IMAGE[index], source[index]);
                return true;
            };
        for (const unsigned occurrence : {1U, 2U}) {
            CBurnMaterial material;
            {
                const gl_harness::FailAllocation FAIL(gl_harness::Allocation::buffer, occurrence);
                const bool INITIALIZED = material.initialize(program);
                CHECK(!INITIALIZED);
                if (INITIALIZED) {
                    material.release();
                    return false;
                }
            }
            CHECK(!material.draw(simulation, placement(), texture, target.framebuffer));
            const bool INITIALIZED = material.initialize(program);
            CHECK(INITIALIZED);
            if (!INITIALIZED) return false;
            const bool DRAWN = DRAW(material, placement(), target);
            material.release();
            if (!DRAWN) return false;
        }
        const Target larger{384, HEIGHT};
        if (!larger.valid) return false;
        auto growth = placement();
        growth.canvas.x = 384;
        for (const auto allocation :
            {gl_harness::Allocation::texture_2d, gl_harness::Allocation::renderbuffer}) {
            CBurnMaterial material;
            const bool INITIALIZED = material.initialize(program);
            CHECK(INITIALIZED);
            if (!INITIALIZED) return false;
            {
                const gl_harness::FailAllocation FAIL(allocation);
                CHECK(!material.draw(simulation, placement(), texture, target.framebuffer));
            }
            if (!DRAW(material, placement(), target)) {
                material.release();
                return false;
            }
            {
                const gl_harness::FailAllocation FAIL(allocation);
                CHECK(!material.draw(simulation, growth, texture, larger.framebuffer));
            }
            // Failed growth must preserve both the old FBO and its capacity.
            if (!DRAW(material, placement(), target) || !DRAW(material, growth, larger)) {
                material.release();
                return false;
            }
            material.release();
        }
        return true;
    }
    void opening_and_consume(
        CBurnMaterial &material, const Target &target, GLuint texture, const Image &source) {
        hyprimate::BurnParams params;
        params.duration = 1;
        params.ember_density = params.fragment_density = 0;
        params.variation = 1;
        params.consume = true;
        hyprimate::BurnSimulation simulation;
        simulation.open(CAPTURE, params, 29);
        CHECK(material.draw(simulation, placement(), texture, target.framebuffer));
        for (const auto &pixel : read(target)) CHECK(pixel[3] == 0);
        advance(simulation, 40);
        const double THRESHOLD = simulation.front().progress;
        CHECK(THRESHOLD > 0 && THRESHOLD < 1);
        CHECK(material.draw(simulation, placement(), texture, target.framebuffer));
        const auto REVEAL = read(target);
        unsigned retained = 0, removed = 0;
        for (int row = 0; row < HEIGHT; ++row) {
            for (int column = 0; column < WIDTH; ++column) {
                const double ARRIVAL = hyprimate::burn_arrival(
                    {(column + .5) / WIDTH, (row + .5) / HEIGHT}, 29, params.variation);
                if (std::abs(ARRIVAL - THRESHOLD) < .008) continue;
                const auto INDEX = static_cast<std::size_t>(row * WIDTH + column);
                if (ARRIVAL < THRESHOLD) {
                    near(REVEAL[INDEX], source[INDEX]);
                    ++retained;
                } else {
                    CHECK(REVEAL[INDEX][3] == 0);
                    ++removed;
                }
            }
        }
        CHECK(retained > 300 && removed > 300);
        advance(simulation, 100);
        CHECK(simulation.front().progress == 1);
        CHECK(material.draw(simulation, placement(), texture, target.framebuffer));
        const auto COMPLETE = read(target);
        for (std::size_t index = 0; index < source.size(); ++index)
            near(COMPLETE[index], source[index]);
        simulation.close(CAPTURE, params, 29);
        advance(simulation, 40);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glClearColor(.13F, .27F, .39F, .41F);
        CHECK(material.draw(simulation, placement(), texture, target.framebuffer));
        GLint depth_function = 0;
        GLboolean depth_mask = GL_TRUE;
        std::array<GLfloat, 4> clear{};
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
        CHECK(depth_function == GL_GREATER && depth_mask == GL_FALSE);
        CHECK(std::abs(clear[0] - .13F) < .0001F && std::abs(clear[3] - .41F) < .0001F);
        const auto CLOSING = read(target);
        // Consume does not globally dim, curl, or filter the untouched content.
        for (int row = 42; row < 54; ++row)
            for (int column = 42; column < 54; ++column)
                near(CLOSING[static_cast<std::size_t>(row * WIDTH + column)],
                    source[static_cast<std::size_t>(row * WIDTH + column)]);
        CHECK(CLOSING.front()[3] == 0 && CLOSING.back()[3] == 0);
        // A folded sheet loses edge pixels through contraction while printed
        // pixels well inside the cold part remain at their native positions.
        hyprimate::BurnSimulation folded;
        params.consume = false;
        params.curl = 1;
        folded.close(CAPTURE, params, 29);
        advance(folded, 40);
        CHECK(material.draw(folded, placement(), texture, target.framebuffer));
        const auto CURL = read(target);
        unsigned contracted = 0;
        for (std::size_t i = 0; i < CURL.size(); ++i) {
            if (CLOSING[i][3] > 240 && CURL[i][3] < 16) ++contracted;
        }
        CHECK(contracted > 15);
        for (int row = 44; row < 52; ++row)
            for (int column = 44; column < 52; ++column) {
                const auto index = static_cast<std::size_t>(row * WIDTH + column);
                near(CURL[index], source[index]);
            }
        advance(simulation, 150);
        CHECK(material.draw(simulation, placement(), texture, target.framebuffer));
        for (const auto &pixel : read(target)) CHECK(pixel[3] == 0);
    }
    void flake_carries_birth_print(const Target &target) {
        CBurnGroundDebris debris;
        {
            const gl_harness::FailAllocation FAIL(gl_harness::Allocation::buffer);
            CHECK(!debris.initialize());
        }
        CHECK(debris.initialize());
        Image paper(WIDTH * HEIGHT, Pixel{16, 25, 36, 255});
        for (std::size_t i = 0; i < paper.size(); ++i)
            if (i % WIDTH < WIDTH / 2) paper[i] = {230, 140, 80, 255};
        glActiveTexture(GL_TEXTURE0);
        const GLuint WALL = upload(paper);
        // A cropped viewport on the right of an offset output. Both flakes are over
        // the dark half; only the first was torn from the light half.
        constexpr hyprimate::Box SCREEN{100, -100, 192, 96}, VIEW{196, -100, 96, 96};
        CBurnGround::Particle flake{.position = {220, -62},
            .source = {120, -50},
            .age = .15,
            .lifetime = 1,
            .size = 15,
            .angle = .4,
            .fold = .25,
            .char_depth = .35,
            .shape = .35,
            .velocity = {},
            .terminal = {}};
        const auto LIGHT = CBurnGroundDebris::pack(flake, SCREEN);
        flake.position.x += 44;
        flake.source.x += 120;
        const std::array INSTANCES{LIGHT, CBurnGroundDebris::pack(flake, SCREEN)};
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(0, 0, WIDTH, HEIGHT);
        constexpr std::array<float, 4> CLEAR{};
        glClearBufferfv(GL_COLOR, 0, CLEAR.data());
        debris.draw(INSTANCES, VIEW, SCREEN, {0, 0}, {1, 0}, {0, 1});
        const auto PIXELS = read(target);
        const auto LEFT = PIXELS[38 * WIDTH + 24], RIGHT = PIXELS[38 * WIDTH + 68];
        CHECK(LEFT[3] == 255 && RIGHT[3] == 255);
        CHECK(LEFT[0] > RIGHT[0] + 80);
        CHECK(PIXELS[38 * WIDTH + 46][3] == 0);
        debris.release();
        glDeleteTextures(1, &WALL);
    }
    // An opaque mask could look convincing on one wallpaper. Change the print under
    // the same cold damage: surviving paper must respond, the exposed backing must not.
    void missing_paper_and_surviving_print(const Target &target) {
        const GLuint PROGRAM =
            gl_harness::build_program(burn_ground_shader::VERTEX, burn_ground_shader::fragment());
        CHECK(PROGRAM != 0);
        if (!PROGRAM) return;
        glUseProgram(PROGRAM);
        glUniform4f(glGetUniformLocation(PROGRAM, "viewUV"), 0, 0, 1, 1);
        glUniform2f(glGetUniformLocation(PROGRAM, "axisX"), 1, 0);
        glUniform2f(glGetUniformLocation(PROGRAM, "axisY"), 0, 1);
        glUniform4f(glGetUniformLocation(PROGRAM, "logicalScreen"), 0, 0, 480, 480);
        glUniform1i(glGetUniformLocation(PROGRAM, "field"), 1);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(0, 0, WIDTH, HEIGHT);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        Image field(WIDTH * HEIGHT);
        for (std::size_t i = 0; i < field.size(); ++i) {
            const auto col = i % WIDTH;
            field[i][2] = col < WIDTH / 3 ? 0 : col < WIDTH * 2 / 3 ? 140 : 244;
        }
        glActiveTexture(GL_TEXTURE1);
        const GLuint FIELD = upload(field);
        glActiveTexture(GL_TEXTURE0);
        const auto RENDER = [&](Pixel ink) {
            const GLuint WALL = upload(Image(WIDTH * HEIGHT, ink));
            constexpr std::array<float, 4> CLEAR{};
            glClearBufferfv(GL_COLOR, 0, CLEAR.data());
            glDrawArrays(GL_TRIANGLES, 0, 3);
            auto image = read(target);
            glDeleteTextures(1, &WALL);
            return image;
        };
        const auto DARK = RENDER({12, 24, 30, 255});
        const auto LIGHT = RENDER({230, 210, 178, 255});
        int holes = 0, printed = 0;
        for (std::size_t i = 0; i < LIGHT.size(); ++i) {
            const auto col = i % WIDTH;
            if (col < WIDTH / 3 - 1) CHECK(LIGHT[i][3] == 0);
            if (col > WIDTH / 3 && col < WIDTH * 2 / 3 - 1 && LIGHT[i][0] > DARK[i][0] + 32)
                ++printed;
            if (col > WIDTH * 2 / 3 && LIGHT[i][3] == 255 && LIGHT[i] == DARK[i]) ++holes;
        }
        CHECK(printed > WIDTH * HEIGHT / 6);
        CHECK(holes > WIDTH * HEIGHT / 100);
        glDeleteTextures(1, &FIELD);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(PROGRAM);
    }
    void opening_over_saturated_scorch(const Target &target) {
        const GLuint program =
            gl_harness::build_program(burn_ground_shader::VERTEX, burn_ground_shader::fragment());
        CHECK(program != 0);
        if (!program) return;
        glUseProgram(program);
        glUniform4f(glGetUniformLocation(program, "viewUV"), 0, 0, 1, 1);
        glUniform2f(glGetUniformLocation(program, "axisX"), 1, 0);
        glUniform2f(glGetUniformLocation(program, "axisY"), 0, 1);
        glUniform4f(glGetUniformLocation(program, "logicalScreen"), 0, 0, WIDTH, HEIGHT);
        glUniform1i(glGetUniformLocation(program, "field"), 1);
        glUniform4f(glGetUniformLocation(program, "openingRect[0]"), 0, 0, 1, 1);
        glUniform4f(glGetUniformLocation(program, "openingState[0]"), 0, 0, 1, 0);
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(0, 0, WIDTH, HEIGHT);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glActiveTexture(GL_TEXTURE1);
        const GLuint field = upload(Image(WIDTH * HEIGHT, Pixel{255, 0, 0, 0}));
        glActiveTexture(GL_TEXTURE0);
        const GLuint wall = upload(Image(WIDTH * HEIGHT, Pixel{210, 180, 140, 255}));
        const auto render = [&](int openings) {
            glUniform1i(glGetUniformLocation(program, "openingCount"), openings);
            constexpr std::array<float, 4> clear{};
            glClearBufferfv(GL_COLOR, 0, clear.data());
            glDrawArrays(GL_TRIANGLES, 0, 3);
            return read(target);
        };
        const auto scorch = render(0);
        const auto with_opening = render(1);
        CHECK(scorch != with_opening);
        glDeleteTextures(1, &wall);
        glDeleteTextures(1, &field);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
    }

    void common_front_and_cold_ink(const Target &target) {
        constexpr std::string_view VERTEX = R"GLSL(#version 300 es
precision highp float;
void main() {
    vec2 p=gl_VertexID==0 ? vec2(-1,-1) : (gl_VertexID==1 ? vec2(3,-1) : vec2(-1,3));
    gl_Position=vec4(p,0,1);
}
)GLSL";
        const std::string FRAGMENT = std::string{R"GLSL(#version 300 es
precision highp float;
uniform float seedPhase, variation, heat;
uniform int mode;
layout(location=0) out vec4 color;
)GLSL"} + std::string{burn_shader::COMMON} +
                                     R"GLSL(
void main() {
    vec2 uv=gl_FragCoord.xy/vec2(96,96);
    if(mode==0) color=vec4(vec3(burnArrival(uv,seedPhase,variation)),1);
    else color=vec4(burnChar(vec3(uv.x<0.5 ? 0.1 : 0.9),0.0,0.055,vec2(0.25),heat),1);
}
)GLSL";
        const GLuint PROGRAM = gl_harness::build_program(VERTEX, FRAGMENT);
        CHECK(PROGRAM != 0);
        if (!PROGRAM) return;
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(0, 0, WIDTH, HEIGHT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(PROGRAM);
        for (const unsigned seed : {0U, 29U, 4095U}) {
            for (const float variation : {0.F, .7F, 1.F}) {
                glUniform1i(glGetUniformLocation(PROGRAM, "mode"), 0);
                glUniform1f(
                    glGetUniformLocation(PROGRAM, "seedPhase"), hyprimate::burn_phase(seed));
                glUniform1f(glGetUniformLocation(PROGRAM, "variation"), variation);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                const auto IMAGE = read(target);
                for (int row = 0; row < HEIGHT; ++row) {
                    for (int column = 0; column < WIDTH; ++column) {
                        const double EXPECTED = hyprimate::burn_arrival(
                            {(column + .5) / WIDTH, (row + .5) / HEIGHT}, seed, variation);
                        CHECK(std::abs(
                                  IMAGE[static_cast<std::size_t>(row * WIDTH + column)][0] / 255. -
                                  EXPECTED) < .0041);
                    }
                }
            }
        }
        glUniform1i(glGetUniformLocation(PROGRAM, "mode"), 1);
        glUniform1f(glGetUniformLocation(PROGRAM, "heat"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const auto COLD = read(target);
        const auto DARK = COLD[static_cast<std::size_t>(48 * WIDTH + 20)];
        const auto LIGHT = COLD[static_cast<std::size_t>(48 * WIDTH + 76)];
        CHECK(LIGHT[0] > DARK[0] + 55);  // legible gray ink even at complete carbonization
        CHECK(std::abs(static_cast<int>(LIGHT[0]) - LIGHT[2]) < 5);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(PROGRAM);
    }
}  // namespace

int main() {
    const gl_harness::CContext context{WIDTH, HEIGHT};
    if (!context.valid()) {
        std::fprintf(stderr, "Burn material: surfaceless EGL unavailable\n");
        return 77;
    }
    const Target target;
    if (!target.valid) return 1;
    const GLuint PROGRAM =
        gl_harness::build_program(burn_shader::vertex(), burn_shader::fragment());
    CHECK(PROGRAM != 0);
    if (!PROGRAM) return 1;
    CBurnMaterial material;
    CHECK(material.initialize(PROGRAM));
    if (g_failures != 0) {
        material.release();
        glDeleteProgram(PROGRAM);
        return 1;
    }
    Image source(WIDTH * HEIGHT);
    for (int row = 0; row < HEIGHT; ++row)
        for (int column = 0; column < WIDTH; ++column)
            source[static_cast<std::size_t>(row * WIDTH + column)] =
                row % 9 < 2 || column % 7 < 2 ? Pixel{15, 19, 23, 255} : Pixel{210, 225, 240, 255};
    const GLuint TEXTURE = upload(source);
    CHECK(TEXTURE != 0);
    if (!TEXTURE || glGetError() != GL_NO_ERROR ||
        !allocation_failure_retry(PROGRAM, target, TEXTURE, source)) {
        material.release();
        glDeleteTextures(1, &TEXTURE);
        glDeleteProgram(PROGRAM);
        return 1;
    }
    opening_and_consume(material, target, TEXTURE, source);
    common_front_and_cold_ink(target);
    missing_paper_and_surviving_print(target);
    opening_over_saturated_scorch(target);
    flake_carries_birth_print(target);
    CHECK(glGetError() == GL_NO_ERROR);
    material.release();
    glDeleteTextures(1, &TEXTURE);
    glDeleteProgram(PROGRAM);
    return g_failures == 0 ? 0 : 1;
}
