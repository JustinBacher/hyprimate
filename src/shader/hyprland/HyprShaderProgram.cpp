#include "shader/hyprland/HyprShaderProgram.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
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

        compute::GlResolver::Proc resolve_compute_proc(const char *name, void *) noexcept {
            return reinterpret_cast<compute::GlResolver::Proc>(eglGetProcAddress(name));
        }

        bool compute_context_current(void *owner) noexcept {
            return owner && static_cast<gl::CompositorContextOwner *>(owner)->ensure_current();
        }

        bool has_compute_pass(const Package &package) noexcept {
            return std::ranges::any_of(package.passes, [](const Pass &pass) { return !pass.kernel.empty(); });
        }

        compute::Backend compute_backend(const Package &package, std::string &error) {
            if (!has_compute_pass(package)) return compute::Backend::Auto;
            const char *override_value = std::getenv("HYPRIMATE_COMPUTE_BACKEND");
            if (!override_value) return compute::Backend::Auto;
            auto parsed = compute::parse_backend(override_value);
            if (!parsed) error = "invalid HYPRIMATE_COMPUTE_BACKEND (expected auto, native, or slibs): " + parsed.error();
            return parsed ? *parsed : compute::Backend::Auto;
        }
        const char *backend_name(compute::Backend backend) noexcept {
            switch (backend) {
                case compute::Backend::Auto: return "auto";
                case compute::Backend::Native: return "native";
                case compute::Backend::Slibs: return "slibs";
            }
            return "unknown";
        }
    }

    std::uint64_t next_compute_instance_id() noexcept {
        static std::atomic<std::uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }

    void WindowProgram::log_compute_instance_retirement(std::uint64_t instance_id,
        const compute::Statistics &stats, const char *kind) const noexcept {
        if (!has_compute) return;
        LOG(Log::DEBUG, "[hyprimate] compute instance retire instance_id={} kind={} compiles={} allocations={} pool_hits={} native={} slibs={} samples={} barriers={} live_bytes={} idle_bytes={} upload_bytes={} copy_bytes={} readback_bytes={}",
            instance_id, kind, stats.compiles, stats.allocations, stats.pool_hits, stats.native_dispatches,
            stats.slibs_dispatches, stats.samples, stats.barriers, stats.live_bytes, stats.idle_bytes,
            stats.upload_bytes, stats.copy_bytes, stats.readback_bytes);
    }

    WindowProgram::~WindowProgram() {
        const auto stats = has_compute ? program.compute_statistics() : compute::Statistics{};
        if (has_compute) {
            LOG(Log::DEBUG, "[hyprimate] compute generation retire compiles={} allocations={} pool_hits={} native={} slibs={} samples={} barriers={} live_bytes={} idle_bytes={}",
                stats.compiles, stats.allocations, stats.pool_hits, stats.native_dispatches,
                stats.slibs_dispatches, stats.samples, stats.barriers, stats.live_bytes, stats.idle_bytes);
        }
        const gl::ScopedCompositorContext context{*owner};
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
        const auto backend = compute_backend(*package, error);
        if (!error.empty()) return {};
        const gl::ScopedCompositorContext context;
        if (!context) {
            error = "no usable compositor GLES context";
            return {};
        }
        static std::vector<std::weak_ptr<WindowProgram>> cache;
        std::erase_if(cache, [](const auto &entry) { return entry.expired(); });
        for (const auto &entry : cache) {
            if (auto old = entry.lock(); old && old->owner->ensure_current() &&
                old->compute_backend == backend && old->values == *values && old->program.package() == *package) {
                LOG(Log::DEBUG, "[hyprimate] shader preparation parse_us={} gpu_us=0 reused=true",
                    micros(start, parsed));
                return old;
            }
        }
        auto program = std::make_shared<WindowProgram>();
        if (!program->owner->ensure_current()) {
            error = "no usable compositor GLES context";
            return {};
        }
        const bool compute_package = has_compute_pass(*package);
        program->compute_backend = backend;
        const compute::GlResolver compute_resolver{
            .resolve = resolve_compute_proc,
            .is_current = compute_context_current,
            .owner = program->owner.get()};
        if (!program->program.initialize(std::move(*package), *values, error, eglGetProcAddress,
                compute_resolver, backend, std::static_pointer_cast<void>(program->owner))) {
            return {};
        }
        program->has_compute = compute_package;
        program->values = std::move(*values);
        cache.emplace_back(program);
        if (program->has_compute) {
            const auto stats = program->program.compute_statistics();
            const auto *caps = program->program.compute_capabilities();
            LOG(Log::DEBUG, "[hyprimate] compute generation prepare backend={} compiles={} allocations={} native={} slibs={} capabilities={} reused=false",
                backend_name(backend), stats.compiles, stats.allocations, stats.native_dispatches,
                stats.slibs_dispatches, caps ? caps->version.c_str() : "unavailable");
        }
        LOG(Log::DEBUG, "[hyprimate] shader preparation parse_us={} gpu_us={} reused=false",
            micros(start, parsed), micros(parsed));
        return program;
    }
}  // namespace hyprimate::shader
