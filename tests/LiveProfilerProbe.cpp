#include "perf/LiveProfiler.hpp"

#include <atomic>
#include <iostream>
#include <thread>

using namespace hyprimate::perf;
namespace {
    constexpr Key PAPER{Domain::Window, "paper", "close"};
    constexpr Key SMOKE{Domain::Window, "smoke", "shared"};
    constexpr Key CUBE{Domain::Workspace, "cube", "transition"};
    void emit(const char *name) { std::cout << "{\"case\":\"" << name << "\",\"report\":" << report() << "}\n"; }
    void frame(std::uint64_t monitor, Key key, std::uint64_t stamp) {
        frame_begin(monitor, 100);
        touch(key, monitor);
        touch(key, monitor); // Multiple windows/passes of one style are one pacing sample.
        frame_end(monitor);
        present(monitor, stamp, true, 100);
    }
}
int main() {
    { CpuScope disabled(PAPER, Phase::Update); }
    gpu_sample(PAPER, generation(), 1000);
    emit("disabled");
    start();
    const auto first = generation();
    {
        CpuScope update(PAPER, Phase::Update);
        for (unsigned i = 0; i < 10000; ++i) std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    std::thread worker([] {
        CpuScope simulation(PAPER, Phase::Worker);
        for (unsigned i = 0; i < 10000; ++i) std::atomic_signal_fence(std::memory_order_seq_cst);
    });
    worker.join();
    gpu_status(first, GpuStatus::Available);
    gpu_sample(PAPER, first, 2'000'000);
    gpu_sample(SMOKE, first, 4'000'000);
    gpu_sample(CUBE, first, 6'000'000);
    resource_sample(PAPER, 1000, 2000);
    resource_sample(PAPER, 900, 3000);
    frame(11, PAPER, 1'000'000'000);
    frame(11, PAPER, 1'010'000'000);
    frame(11, PAPER, 1'040'000'000);
    // Another monitor's clock/cadence must never join monitor 11's interval sequence.
    frame(12, PAPER, 5'000'000'000);
    frame_begin(11, 100);
    frame_end(11);
    present(11, 1'050'000'000, true, 100);
    frame(11, PAPER, 2'000'000'000); // First frame after idle is not a 950 ms style hitch.
    sample_memory(11);
    emit("measured");
    stop();
    gpu_sample(PAPER, first, 99'000'000);
    frame(11, PAPER, 2'030'000'000);
    { CpuScope disabled(PAPER, Phase::Draw); }
    emit("stopped");
    stop();
    emit("stopped_again");
    start();
    gpu_sample(PAPER, first, 99'000'000);
    emit("new_session");
    {
        CpuScope stale(PAPER, Phase::Setup);
        reset();
        resource_sample(PAPER, 123, 456, stale.session());
    }
    emit("stale_scope");
    const auto session = generation();
    for (unsigned i = 0; i < 600; ++i) gpu_sample(PAPER, session, i * 1'000'000ULL);
    gpu_dropped(session);
    gpu_status(session, GpuStatus::Disjoint);
    emit("bounded");
    reset();
    frame(11, PAPER, 1'000'000'000);
    present(11, 1'010'000'000, false, 100);
    frame(11, PAPER, 1'040'000'000);
    emit("rejected");
    stop();
    reset();
    emit("reset_stopped");
    start();
    frame(11, PAPER, 0);
    frame(11, PAPER, 0);
    frame(11, PAPER, 0);
    emit("untimestamped");
    reset();
    frame(11, PAPER, 1'000'000'000);
    present(11, 1'010'000'000, true, 100);
    present(11, 1'020'000'000, true, 100);
    frame(11, PAPER, 1'030'000'000);
    emit("unassociated");
    stop();
}
