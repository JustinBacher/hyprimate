#include "GlHarness.hpp"
#include "compute/Compute.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace hc = hyprimate::compute;

namespace {
    constexpr unsigned WIDTH = 17, HEIGHT = 9, STEPS = 4;
    constexpr float INCREMENT = 1.F / 32.F;
    constexpr int ORIGIN_X = 3, ORIGIN_Y = 4;

    template<class T> T checked(hc::Result<T> result) {
        if (!result) throw std::runtime_error(result.error());
        if constexpr (!std::is_void_v<T>) return std::move(*result);
    }

    hc::GlResolver gl_resolver(EGLContext& context) {
        return {[](const char* symbol, void*) noexcept -> hc::GlResolver::Proc { return eglGetProcAddress(symbol); },
            [](void* cookie) noexcept { return eglGetCurrentContext() == *static_cast<EGLContext*>(cookie); }, &context};
    }

    void present(hc::Device& device, const hc::Field& field, hc::Type type) {
        constexpr auto VERTEX = R"(#version 300 es
            void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                          gl_Position = vec4(2.0*p - 1.0, 0.0, 1.0); })";
        struct Target {
            GLuint framebuffer{}, image{}, vao{}, program{};
            ~Target() {
                glUseProgram(0);
                glDeleteProgram(program);
                glDeleteVertexArrays(1, &vao);
                glDeleteFramebuffers(1, &framebuffer);
                glDeleteTextures(1, &image);
            }
        } target;
        std::string fragment = "#version 300 es\nprecision highp float;\nprecision highp int;\n" + checked(hc::sample_source("result", type));
        fragment += R"(
            uniform ivec2 origin;
            layout(location = 0) out vec4 color;
            void main() {
                ivec2 p = ivec2(gl_FragCoord.xy) - origin;
                color = vec4(hc_read_result(ivec3(p, 0)), float(p.x & 1), float(p.y & 1), 1.0);
            })";
        target.program = gl_harness::build_program(VERTEX, fragment);
        if (!target.program) throw std::runtime_error("presentation shader did not link");
        glGenTextures(1, &target.image);
        glBindTexture(GL_TEXTURE_2D, target.image);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 24, 16);
        glGenFramebuffers(1, &target.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("presentation target is incomplete");
        glGenVertexArrays(1, &target.vao);
        glBindVertexArray(target.vao);
        glDisable(GL_DITHER);
        glViewport(ORIGIN_X, ORIGIN_Y, WIDTH, HEIGHT);
        glUseProgram(target.program);

        // Immediate, same-context read-only texture use. No readback or copy feeds the draw.
        const auto view = checked(device.sample(field));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, view.texture);
        glUniform1i(glGetUniformLocation(target.program, "hc_tex_result"), 0);
        glUniform3i(glGetUniformLocation(target.program, "hc_size_result"), view.desc.domain.x, view.desc.domain.y, view.desc.domain.z);
        glUniform2i(glGetUniformLocation(target.program, "origin"), ORIGIN_X, ORIGIN_Y);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, 0);

        constexpr int X = 5, Y = 2;
        std::array<GLubyte, 4> pixel{};
        glReadPixels(ORIGIN_X + X, ORIGIN_Y + Y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        const auto red = std::lround((float(X + 2 * Y) / 64.F + STEPS * INCREMENT) * 255.F);
        if (std::abs(pixel[0] - red) > 1 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255)
            throw std::runtime_error("presentation pixel did not match the logical field coordinate");
        std::printf("Sampled pixel (%d, %d): [%u, %u, %u, %u]\n", X, Y,
            static_cast<unsigned>(pixel[0]), static_cast<unsigned>(pixel[1]),
            static_cast<unsigned>(pixel[2]), static_cast<unsigned>(pixel[3]));
    }
}

int main(int argc, char** argv) try {
    const auto backend = argc == 2 ? checked(hc::parse_backend(argv[1])) : hc::Backend::Auto;
    if (argc > 2) throw std::runtime_error("usage: compute-example [auto|native|slibs]");
    gl_harness::CContext context(32, 32);
    if (!context.valid()) throw std::runtime_error("no EGL GLES context; try EGL_PLATFORM=surfaceless");
    EGLContext owner = eglGetCurrentContext();
    hc::Device device;
    checked(device.initialize(gl_resolver(owner), backend));
    constexpr hc::Type TYPE{hc::Element::Float, 1};
    constexpr hc::FieldDesc DESC{TYPE, {WIDTH, HEIGHT, 1}};
    auto step = checked(device.prepare({"increment", R"(
        void kernel(ivec3 id, inout float next) {
            next = hc_read_previous(id) + increment;
        })", {{"previous", TYPE}}, {{"result", TYPE}}, {{"increment", TYPE}}}));
    std::printf("%s / %s: %s\n", device.capabilities().version.c_str(), device.capabilities().renderer.c_str(),
        step.backend() == hc::Backend::Native ? "native" : "SLIBS");

    auto initial = checked(device.allocate(DESC));
    auto scratch = checked(device.allocate(DESC));
    std::array<float, WIDTH * HEIGHT> host{};
    for (unsigned i = 0; i < host.size(); ++i) host[i] = float(i % WIDTH + 2 * (i / WIDTH)) / 64.F;
    checked(device.upload(initial, std::as_bytes(std::span(host))));
    hc::FieldPair state(std::move(initial), std::move(scratch));

    const std::array kernels{std::cref(step)};
    const std::array parameters{hc::ParameterValue{std::array{INCREMENT, 0.F, 0.F, 0.F}, 1}};
    {
        auto batch = checked(device.begin(kernels));
        for (unsigned i = 0; i < STEPS; ++i) {
            const std::array inputs{&state.read()};
            const std::array outputs{&state.write()};
            checked(batch.dispatch(step, inputs, outputs, parameters));
            state.swap();
        }
    }
    present(device, state.read(), TYPE);

    // Explicit blocking diagnostic, not a simulation or presentation step.
    std::array<float, WIDTH * HEIGHT> checked_values{};
    checked(device.readback(state.read(), std::as_writable_bytes(std::span(checked_values))));
    for (unsigned i = 0; i < checked_values.size(); ++i)
        if (checked_values[i] != host[i] + STEPS * INCREMENT)
            throw std::runtime_error("offline readback mismatch at element " + std::to_string(i));
    std::printf("Verified %zu values after %u dispatches\n", checked_values.size(), STEPS);
    checked(device.release()); // Release with the same EGL context current.
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("unexpected GL error in the example");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "compute-example: %s\n", error.what());
    return 1;
}
