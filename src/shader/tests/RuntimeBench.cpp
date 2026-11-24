#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "GlHarness.hpp"
#include "shader/GlesRuntime.hpp"

namespace {
    using Clock = std::chrono::steady_clock;
    using BeginQuery = void (*)(GLenum, GLuint);
    using EndQuery = void (*)(GLenum);
    using QueryResult = void (*)(GLuint, GLenum, GLuint64 *);
    constexpr GLenum TIME_ELAPSED = 0x88BF, GPU_DISJOINT = 0x8FBB;

    bool run(const std::filesystem::path &path, unsigned width, unsigned height) {
        const auto parse_start = Clock::now();
        auto loaded = hyprimate::shader::load_package(path.string());
        if (!loaded) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), loaded.error().c_str());
            return false;
        }
        auto package = std::move(*loaded);
        auto values = hyprimate::shader::parameter_values(package);
        if (!values) return false;
        const auto compile_start = Clock::now();
        hyprimate::shader::Program program;
        hyprimate::shader::Instance instance;
        std::string error;
        const bool prepared = program.initialize(std::move(package), *values, error, eglGetProcAddress);
        const auto allocation_start = Clock::now();
        if (!prepared || !instance.initialize(program, width, height, error)) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            instance.release();
            program.release();
            return false;
        }
        const auto allocated = Clock::now();
        GLuint textures[2]{}, framebuffer = 0;
        glGenTextures(2, textures);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 255);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = 64;
            pixels[i + 1] = 128;
            pixels[i + 2] = 192;
        }
        for (unsigned i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(width),
                static_cast<GLsizei>(height));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width),
            static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[1], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        const hyprimate::shader::Source source{textures[0], width, height};
        const hyprimate::shader::Target target{framebuffer, textures[1], width, height};
        hyprimate::shader::Frame frame;
        frame.width = static_cast<float>(width);
        frame.height = static_cast<float>(height);
        frame.duration = 3;
        frame.delta = 1.0F / 60;
        frame.seed = 42;
        frame.pointer_valid = true;
        bool success = true;
        auto first_submit = Clock::duration::zero();
        for (unsigned i = 0; i < 8; ++i) {
            frame.tick = i + 1;
            frame.elapsed += frame.delta;
            frame.progress = frame.elapsed / frame.duration;
            instance.advance(frame);
            const auto submit_start = Clock::now();
            if (!instance.render(source, target, true, error)) { success = false; break; }
            if (i == 0) first_submit = Clock::now() - submit_start;
        }
        // Only measurement boundaries synchronize; production execution must not.
        glFinish();
        const auto before = instance.statistics();
        auto begin = reinterpret_cast<BeginQuery>(eglGetProcAddress("glBeginQueryEXT"));
        auto end = reinterpret_cast<EndQuery>(eglGetProcAddress("glEndQueryEXT"));
        auto result = reinterpret_cast<QueryResult>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
        const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
        const bool timer = begin && end && result && extensions &&
                           std::string_view(extensions).find("GL_EXT_disjoint_timer_query") != std::string_view::npos;
        GLuint query = 0;
        if (timer) { glGenQueries(1, &query); begin(TIME_ELAPSED, query); }
        constexpr unsigned FRAMES = 120;
        const auto start = Clock::now();
        for (unsigned i = 0; success && i < FRAMES; ++i) {
            ++frame.tick;
            frame.elapsed += frame.delta;
            frame.progress = std::min(frame.elapsed / frame.duration, 0.95F);
            instance.advance(frame);
            success = instance.render(source, target, true, error);
        }
        const auto submitted = Clock::now();
        if (timer) end(TIME_ELAPSED);
        glFinish();
        const auto complete = Clock::now();
        GLuint64 gpu_ns = 0;
        GLint disjoint = 0;
        if (timer) {
            result(query, GL_QUERY_RESULT, &gpu_ns);
            glGetIntegerv(GPU_DISJOINT, &disjoint);
            glDeleteQueries(1, &query);
        }
        const auto after = instance.statistics();
        const double submit_us = std::chrono::duration<double, std::micro>(submitted - start).count() / FRAMES;
        const double completed_us = std::chrono::duration<double, std::micro>(complete - start).count() / FRAMES;
        std::printf("{\"effect\":\"%s\",\"width\":%u,\"height\":%u,\"frames\":%u,"
                    "\"submit_us_per_frame\":%.3f,\"completed_us_per_frame\":%.3f,",
            path.parent_path().filename().c_str(), width, height, FRAMES, submit_us, completed_us);
        if (timer && !disjoint) std::printf("\"gpu_us_per_frame\":%.3f,", static_cast<double>(gpu_ns) / (1000 * FRAMES));
        else std::printf("\"gpu_us_per_frame\":null,");
        auto us = [](Clock::duration duration) { return std::chrono::duration<double, std::micro>(duration).count(); };
        std::printf("\"parse_us\":%.3f,\"prepare_us\":%.3f,\"allocate_us\":%.3f,\"first_submit_us\":%.3f,",
            us(compile_start - parse_start), us(allocation_start - compile_start),
            us(allocated - allocation_start), us(first_submit));
        std::printf("\"allocation_calls_during_frames\":%llu,\"upload_bytes_during_frames\":%llu,"
                    "\"copy_bytes_during_frames\":%llu,\"draws_during_frames\":%llu,"
                    "\"instance_bytes\":%llu}\n",
            static_cast<unsigned long long>(after.allocations - before.allocations),
            static_cast<unsigned long long>(after.upload_bytes - before.upload_bytes),
            static_cast<unsigned long long>(after.copy_bytes - before.copy_bytes),
            static_cast<unsigned long long>(after.draws - before.draws),
            static_cast<unsigned long long>(after.allocated_bytes));
        if (!success) std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        success = success && after.allocations == before.allocations && after.copy_bytes == before.copy_bytes;
        instance.release();
        program.release();
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(2, textures);
        return success && glGetError() == GL_NO_ERROR;
    }
}

int main() {
    gl_harness::CContext context;
    if (!context.valid()) return 77;
    std::fprintf(stderr, "GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
        glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    bool success = true;
    for (const auto &[width, height] : {std::pair{1280U, 720U}, std::pair{1920U, 1080U},
             std::pair{2560U, 1440U}, std::pair{3840U, 2160U}}) {
        for (const auto *name : {"fade", "diffusion", "particles"}) {
            success = run(std::filesystem::path{HYPRIMATE_SHADER_EXAMPLES} / name / "effect.json",
                          width, height) && success;
        }
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
