#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hyprimate::perf {
    enum class Domain { Window, Workspace };
    enum class GpuStatus { NoContext, Unsupported, Available, Disjoint };
    enum class Phase { Setup, Update, Draw, Worker };

    // Callers supply static style/operation names, never window titles or application data.
    struct Key {
        Domain domain;
        std::string_view style;
        std::string_view operation;
    };
    void gpu_status(std::uint64_t session, GpuStatus status) noexcept;

    [[nodiscard]] bool enabled() noexcept;
    [[nodiscard]] std::uint64_t generation() noexcept;
    void start();
    void stop() noexcept;
    void reset();
    [[nodiscard]] std::string report();

    class CpuScope {
      public:
        CpuScope(Key key, Phase phase) noexcept;
        ~CpuScope();
        CpuScope(const CpuScope &) = delete;
        CpuScope(CpuScope &&) = delete;
        CpuScope &operator=(CpuScope &&) = delete;
        CpuScope &operator=(const CpuScope &) = delete;
        [[nodiscard]] std::uint64_t session() const noexcept { return m_generation; }
      private:
        Key m_key;
        Phase m_phase;
        std::uint64_t m_generation = 0;
        std::uint64_t m_cpu = 0;
        std::uint64_t m_wall = 0;
    };

    // Called only for actual draws, not queued/discarded pass elements. Monitor IDs must
    // match frame_begin/frame_end/present. The collector deduplicates styles per frame.
    void touch(Key key, std::uint64_t monitor) noexcept;
    void frame_begin(std::uint64_t monitor, double refresh_hz) noexcept;
    void frame_end(std::uint64_t monitor) noexcept;
    void present(std::uint64_t monitor, std::uint64_t timestamp_ns, bool presented,
        double refresh_hz) noexcept;
    void remove_monitor(std::uint64_t monitor) noexcept;
    void sample_memory(std::uint64_t monitor = 0) noexcept;

    // Lower-bound resource sample for one animation, not driver VRAM residency or a
    // complete per-style heap census. Shared/borrowed resources must not be counted.
    void resource_sample(Key key, std::uint64_t cpu_bytes, std::uint64_t gpu_bytes) noexcept;
    void resource_sample(Key key, std::uint64_t cpu_bytes, std::uint64_t gpu_bytes,
        std::uint64_t session) noexcept;
    void gpu_sample(Key key, std::uint64_t session, std::uint64_t elapsed_ns) noexcept;
    void gpu_dropped(std::uint64_t session) noexcept;
}  // namespace hyprimate::perf
