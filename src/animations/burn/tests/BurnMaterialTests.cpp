#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "animations/burn/BurnMaterial.hpp"
#include "animations/burn/BurnShaders.hpp"

namespace {
    constexpr int WIDTH = 96, HEIGHT = 96;
    using Pixel = std::array<unsigned char, 4>;
    using Image = std::vector<Pixel>;
    constexpr hyprimate::Box CAPTURE{17, 31, WIDTH, HEIGHT};

    struct Target {
        GLuint framebuffer = 0, texture = 0;
        Target() {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
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
    const GLuint PROGRAM =
        gl_harness::build_program(burn_shader::vertex(), burn_shader::fragment());
    CHECK(PROGRAM != 0);
    if (!PROGRAM) return 1;
    CBurnMaterial material;
    CHECK(material.initialize(PROGRAM));
    Image source(WIDTH * HEIGHT);
    for (int row = 0; row < HEIGHT; ++row)
        for (int column = 0; column < WIDTH; ++column)
            source[static_cast<std::size_t>(row * WIDTH + column)] =
                row % 9 < 2 || column % 7 < 2 ? Pixel{15, 19, 23, 255} : Pixel{210, 225, 240, 255};
    const GLuint TEXTURE = upload(source);
    opening_and_consume(material, target, TEXTURE, source);
    common_front_and_cold_ink(target);
    CHECK(glGetError() == GL_NO_ERROR);
    material.release();
    glDeleteTextures(1, &TEXTURE);
    glDeleteProgram(PROGRAM);
    return g_failures == 0 ? 0 : 1;
}
