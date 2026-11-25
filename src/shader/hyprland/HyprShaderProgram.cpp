#include "shader/hyprland/HyprShaderProgram.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include <EGL/egl.h>
#include <hyprland/src/debug/log/Logger.hpp>

namespace hyprimate::shader {
    namespace {
        using Clock = std::chrono::steady_clock;
        auto micros(Clock::time_point start, Clock::time_point end = Clock::now()) {
            return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        }
    }

    WindowProgram::~WindowProgram() {
        const gl::ScopedCompositorContext context{owner};
        if (context) program.release();
        else program.abandon();
    }

    std::shared_ptr<WindowProgram> prepare_window_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error) {
        const auto start = Clock::now();
        auto package = load_package(path);
        if (!package) {
            error = std::move(package.error());
            return {};
        }
        if (!package->supports(event)) {
            error = "effect does not declare this event";
            return {};
        }
        auto values = parameter_values(*package, parameters);
        if (!values) {
            error = std::move(values.error());
            return {};
        }
        const auto parsed = Clock::now();
        // Path is diagnostic metadata, not part of the prepared GPU generation.
        package->path.clear();
        const gl::ScopedCompositorContext context;
        if (!context) {
            error = "no usable compositor GLES context";
            return {};
        }
        static std::vector<std::weak_ptr<WindowProgram>> cache;
        std::erase_if(cache, [](const auto &entry) { return entry.expired(); });
        for (const auto &entry : cache) {
            if (auto old = entry.lock(); old && old->owner.ensure_current() &&
                old->values == *values && old->program.package() == *package) {
                LOG(Log::DEBUG, "[hyprimate] shader preparation parse_us={} gpu_us=0 reused=true",
                    micros(start, parsed));
                return old;
            }
        }
        auto program = std::make_shared<WindowProgram>();
        if (!program->owner.ensure_current()) {
            error = "no usable compositor GLES context";
            return {};
        }
        if (!program->program.initialize(std::move(*package), *values, error, eglGetProcAddress)) {
            return {};
        }
        program->values = std::move(*values);
        cache.emplace_back(program);
        LOG(Log::DEBUG, "[hyprimate] shader preparation parse_us={} gpu_us={} reused=false",
            micros(start, parsed), micros(parsed));
        return program;
    }
}  // namespace hyprimate::shader
