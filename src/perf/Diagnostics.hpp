#pragma once

#ifdef HYPRIMATE_DIAGNOSTICS
#include "perf/GpuStopwatch.hpp"
#include "perf/LiveProfiler.hpp"

#define HYPRIMATE_DIAGNOSTICS_JOIN_IMPL(a, b) a##b
#define HYPRIMATE_DIAGNOSTICS_JOIN(a, b) HYPRIMATE_DIAGNOSTICS_JOIN_IMPL(a, b)
#define HYPRIMATE_GPU_STOPWATCH(label) \
    const ::hyprimate::perf::GpuStopwatch HYPRIMATE_DIAGNOSTICS_JOIN(hyprimate_gpu_stopwatch_, __LINE__){label}
#define HYPRIMATE_CPU_SCOPE(key) \
    const ::hyprimate::perf::CpuScope HYPRIMATE_DIAGNOSTICS_JOIN(hyprimate_cpu_scope_, __LINE__){key, ::hyprimate::perf::Phase::Draw}
#else
#define HYPRIMATE_GPU_STOPWATCH(label)
#define HYPRIMATE_CPU_SCOPE(key)
#endif
