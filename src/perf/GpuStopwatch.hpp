#pragma once

#include <string>

namespace hyprimate::perf {
    // GPU duration of a code region from a pair of GL_TIMESTAMP queries. Unlike GpuScope (one
    // GL_TIME_ELAPSED query, which the host's enclosing style scope already holds), timestamp
    // queries nest inside it, so an inner region of an already-timed draw can be measured.
    // Compositor-thread only, active only while the live profiler is running. Results are read
    // without blocking, a few frames later. The diagnostics harness must gate samples against
    // disjoint events; GpuTimer owns the GL_GPU_DISJOINT_EXT read.
    class GpuStopwatch {
      public:
        explicit GpuStopwatch(const char *label) noexcept;
        ~GpuStopwatch();
        GpuStopwatch(const GpuStopwatch &) = delete;
        GpuStopwatch &operator=(const GpuStopwatch &) = delete;
        GpuStopwatch(GpuStopwatch &&) = delete;
        GpuStopwatch &operator=(GpuStopwatch &&) = delete;

      private:
        int m_slot = -1;
    };

    // One line per label: count, mean/p50/p95/max in microseconds, and samples dropped (no free
    // slot). Consumes any results the driver reports ready first; the harness must exclude any
    // samples spanning a disjoint event reported by GpuTimer.
    [[nodiscard]] std::string gpu_stopwatch_report();
    // Deletes the query objects (compositor context must be current) and clears samples.
    void gpu_stopwatch_release() noexcept;
}  // namespace hyprimate::perf
