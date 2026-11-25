#pragma once

#include <string>

namespace hyprimate::perf {
    // GPU duration of a code region from a pair of GL_TIMESTAMP queries. Unlike GpuScope (one
    // GL_TIME_ELAPSED query, which the host's enclosing style scope already holds), timestamp
    // queries nest inside it, so an inner region of an already-timed draw can be measured.
    // Compositor-thread only, active only while the live profiler is running. Results are read
    // without blocking, a few frames later; discarded when the driver reports a disjoint event.
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
        const char *m_label = nullptr;
    };

    // One line per label: count, mean/p50/p95/max in microseconds, and samples dropped (no free
    // slot or disjoint). Consumes any results the driver reports ready first.
    [[nodiscard]] std::string gpu_stopwatch_report();
    // Deletes the query objects (compositor context must be current) and clears samples.
    void gpu_stopwatch_release() noexcept;
}  // namespace hyprimate::perf
