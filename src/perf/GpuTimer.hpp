#pragma once

#include "LiveProfiler.hpp"

#include <cstdint>

namespace hyprimate::perf {
    // Compositor-thread only. Wrap actual GL execution, never deferred pass submission.
    // Nested scopes and caller-owned elapsed queries are skipped rather than disturbed.
    class GpuScope {
      public:
        explicit GpuScope(Key key) noexcept;
        ~GpuScope();
        GpuScope(const GpuScope &) = delete;
        GpuScope &operator=(const GpuScope &) = delete;
        GpuScope(GpuScope &&) = delete;
        GpuScope &operator=(GpuScope &&) = delete;

      private:
        std::uint64_t m_token = 0;
        unsigned m_context = 0;
        unsigned m_slot = 0;
    };

    // Nonblocking: consume only results explicitly reported available by the driver.
    void poll_gpu() noexcept;
    // Call before compositor context destruction, including backend replacement. Restores
    // the caller binding and retires only objects whose owning context can be verified.
    // Temporarily unavailable contexts retain their bounded slots for a later retry;
    // destroyed contexts that cannot be rebound can continue occupying those slots.
    void release_gpu() noexcept;
}  // namespace hyprimate::perf
