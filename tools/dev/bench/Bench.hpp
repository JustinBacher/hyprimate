#pragma once

// Minimal deterministic benchmark harness for Hyprimate's compositor-independent animation models.
// A scenario is one complete scripted workload (for example 90 frames of a bubble opening). The
// harness times whole workloads, so per-frame cost is workload time / frames, and it counts heap
// allocations around each run. Scenarios return a checksum so the optimiser cannot discard the work
// and so a nondeterministic scenario is detected.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bench {

    struct Scenario {
        std::string name;  // group.workload
        std::string description;
        unsigned frames = 1;  // frames (or steps) of animation per run, to report per-frame cost
        std::function<double()> run;  // one full deterministic workload; returns a checksum
        bool threaded = false;  // work happens on other threads: wall time includes waiting
    };

    using Registry = std::vector<Scenario>;

    void register_geometry(Registry &registry);
    void register_bubble(Registry &registry);
    void register_smoke(Registry &registry);
    void register_sheets(Registry &registry);
    void register_construction(Registry &registry);

    // Keeps a value observable to the compiler without emitting code that touches memory.
    template <class T>
    inline void keep(const T &value) {
        // NOLINTNEXTLINE(hicpp-no-assembler)
        asm volatile("" : : "r,m"(value) : "memory");
    }

    // Allocation counters for the whole process (all threads), maintained by the operator new
    // replacement in BenchMain.cpp. They exist only in the benchmark executable.
    struct AllocationCounters {
        std::uint64_t count = 0;
        std::uint64_t bytes = 0;
    };
    AllocationCounters allocations() noexcept;

}  // namespace bench
