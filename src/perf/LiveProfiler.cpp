#include "LiveProfiler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <time.h>
#include <unistd.h>

namespace hyprimate::perf {
    namespace {
        constexpr std::size_t ROWS = 96;
        constexpr std::size_t MONITORS = 16;
        constexpr std::size_t SAMPLES = 512;
        constexpr std::uint64_t MEMORY_INTERVAL = 250'000'000;
        constexpr std::array<std::string_view, 4> PHASES{"setup", "update", "draw", "worker"};

        std::uint64_t clock_ns(clockid_t clock) noexcept {
            timespec value{};
            if (clock_gettime(clock, &value) != 0) return 0;
            return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL + value.tv_nsec;
        }
        std::uint64_t now() noexcept { return clock_ns(CLOCK_MONOTONIC); }
        struct Memory {
            std::uint64_t rss = 0;
            std::uint64_t virtual_bytes = 0;
            bool valid = false;
        };
        Memory process_memory() noexcept {
            auto *file = std::fopen("/proc/self/statm", "re");
            if (!file) return {};
            unsigned long total = 0, resident = 0;
            const bool valid = std::fscanf(file, "%lu %lu", &total, &resident) == 2;
            std::fclose(file);
            const long page = sysconf(_SC_PAGESIZE);
            if (!valid || page <= 0) return {};
            return {resident * static_cast<std::uint64_t>(page),
                total * static_cast<std::uint64_t>(page), true};
        }
        struct Series {
            std::array<double, SAMPLES> values{};
            std::uint64_t count = 0;
            double total = 0;
            double maximum = 0;
            void add(double value) noexcept {
                if (!std::isfinite(value) || value < 0) return;
                values[count % SAMPLES] = value;
                ++count;
                total += value;
                maximum = std::max(maximum, value);
            }
        };
        struct Row {
            Domain domain{};
            std::array<char, 40> style{};
            std::array<char, 24> operation{};
            std::array<Series, 4> cpu;
            std::array<Series, 4> wall;
            Series gpu, draw_intervals, presentation_intervals, presentation_callback_intervals;
            std::uint64_t draws = 0, presentations = 0, untimestamped_presentations = 0;
            std::uint64_t late_draws = 0, late_presentations = 0;
            std::uint64_t estimated_missed_refreshes = 0;
            std::uint64_t rss_min = std::numeric_limits<std::uint64_t>::max(), rss_max = 0;
            std::uint64_t known_cpu_peak = 0, known_gpu_peak = 0, resource_samples = 0;
        };
        struct Monitor {
            std::uint64_t id = 0, frame = 0;
            double refresh = 0;
            std::bitset<ROWS> drawn, submitted, previous_present, previous_callback;
            std::array<std::uint64_t, ROWS> draw_frame{}, draw_time{};
            std::uint64_t present_time = 0, callback_time = 0;
            bool open = false, pending_presentation = false;
        };
        struct State {
            std::array<Row, ROWS> rows;
            std::array<Monitor, MONITORS> monitors;
            std::size_t row_count = 0;
            std::uint64_t started = now(), stopped = 0, memory_time = 0;
            std::uint64_t dropped_rows = 0, dropped_monitors = 0, dropped_gpu = 0;
            std::uint64_t disjoint_events = 0, rejected_presentations = 0;
            GpuStatus gpu_status = GpuStatus::NoContext;
            Memory baseline, current;
            std::uint64_t rss_peak = 0;
        };
        std::mutex mutex;
        std::unique_ptr<State> state;
        // One atomic token publishes both the session identity and enabled bit.
        std::atomic<std::uint64_t> token{0};

        template <std::size_t N>
        void copy_name(std::array<char, N> &destination, std::string_view source) noexcept {
            const auto size = std::min(source.size(), N - 1);
            std::copy_n(source.data(), size, destination.data());
            destination[size] = '\0';
        }
        std::size_t row_for(State &data, Key key) noexcept {
            // Refuse ambiguous truncation rather than merging differently named styles.
            if (key.style.size() >= data.rows[0].style.size() ||
                key.operation.size() >= data.rows[0].operation.size()) {
                ++data.dropped_rows;
                return ROWS;
            }
            for (std::size_t i = 0; i < data.row_count; ++i) {
                const auto &row = data.rows[i];
                if (row.domain == key.domain && key.style == row.style.data() &&
                    key.operation == row.operation.data()) return i;
            }
            if (data.row_count == ROWS) {
                ++data.dropped_rows;
                return ROWS;
            }
            const auto index = data.row_count++;
            auto &row = data.rows[index];
            row.domain = key.domain;
            copy_name(row.style, key.style);
            copy_name(row.operation, key.operation);
            return index;
        }
        Monitor *monitor_for(State &data, std::uint64_t id) noexcept {
            if (!id) return nullptr;
            for (auto &monitor : data.monitors) if (monitor.id == id) return &monitor;
            for (auto &monitor : data.monitors) {
                if (monitor.id) continue;
                monitor.id = id;
                return &monitor;
            }
            ++data.dropped_monitors;
            return nullptr;
        }
        bool current(std::uint64_t session) noexcept {
            return (session & 1U) != 0 && session == token.load(std::memory_order_acquire) && state;
        }
        void replace_state(bool running) {
            auto next = std::make_unique<State>();
            state = std::move(next);
            state->baseline = state->current = process_memory();
            state->rss_peak = state->current.rss;
            state->memory_time = 0;
            if (!running) state->stopped = state->started;
            const auto next_token = (token.load(std::memory_order_relaxed) & ~std::uint64_t{1}) + 2;
            token.store(next_token | static_cast<std::uint64_t>(running), std::memory_order_release);
        }
        void observe_memory(Row &row, const Memory &memory) noexcept {
            if (!memory.valid) return;
            row.rss_min = std::min(row.rss_min, memory.rss);
            row.rss_max = std::max(row.rss_max, memory.rss);
        }
        std::string quoted(std::string_view value) {
            std::string out = "\"";
            for (const unsigned char ch : value) {
                if (ch == '\\' || ch == '"') out += '\\';
                if (ch >= 0x20) out += static_cast<char>(ch);
            }
            out += '"';
            return out;
        }
        void series_json(std::ostream &out, const Series &series) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(series.count, SAMPLES));
            out << "{\"samples\":" << series.count << ",\"retained_samples\":" << count;
            if (count) {
                auto values = series.values;
                std::sort(values.begin(), values.begin() + count);
                const auto percentile = [&](unsigned p) { return values[((count - 1) * p) / 100]; };
                out << ",\"mean_ms\":" << series.total / static_cast<double>(series.count)
                    << ",\"p50_ms\":" << percentile(50) << ",\"p95_ms\":" << percentile(95)
                    << ",\"p99_ms\":" << percentile(99) << ",\"max_ms\":" << series.maximum;
            }
            out << '}';
        }
    }  // namespace

    bool enabled() noexcept { return (token.load(std::memory_order_acquire) & 1U) != 0; }
    std::uint64_t generation() noexcept { return token.load(std::memory_order_acquire); }

    void start() {
        std::lock_guard lock(mutex);
        replace_state(true);
    }
    void stop() noexcept {
        std::lock_guard lock(mutex);
        if (!enabled() || !state) return;
        state->stopped = now();
        state->current = process_memory();
        if (state->current.valid) state->rss_peak = std::max(state->rss_peak, state->current.rss);
        token.fetch_and(~std::uint64_t{1}, std::memory_order_release);
    }
    void reset() {
        std::lock_guard lock(mutex);
        replace_state(enabled());
    }
    CpuScope::CpuScope(Key key, Phase phase) noexcept : m_key(key), m_phase(phase) {
        m_generation = token.load(std::memory_order_acquire);
        if ((m_generation & 1U) == 0) {
            m_generation = 0;
            return;
        }
        m_wall = now();
        m_cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    }
    CpuScope::~CpuScope() {
        if (!m_generation || !enabled()) return;
        const auto cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID);
        const auto wall = now();
        std::lock_guard lock(mutex);
        if (!current(m_generation)) return;
        const auto index = row_for(*state, m_key);
        if (index == ROWS) return;
        auto &row = state->rows[index];
        const auto phase = static_cast<std::size_t>(m_phase);
        if (m_cpu && cpu >= m_cpu) row.cpu[phase].add(static_cast<double>(cpu - m_cpu) / 1e6);
        if (m_wall && wall >= m_wall) row.wall[phase].add(static_cast<double>(wall - m_wall) / 1e6);
    }
    void frame_begin(std::uint64_t id, double refresh_hz) noexcept {
        if (!enabled()) return;
        std::lock_guard lock(mutex);
        if (!enabled() || !state) return;
        auto *monitor = monitor_for(*state, id);
        if (!monitor) return;
        ++monitor->frame;
        monitor->drawn.reset();
        monitor->open = true;
        monitor->refresh = std::isfinite(refresh_hz) && refresh_hz > 0 ? refresh_hz : 0;
    }
    void frame_end(std::uint64_t id) noexcept {
        if (!enabled()) return;
        std::lock_guard lock(mutex);
        if (!enabled() || !state) return;
        auto *monitor = monitor_for(*state, id);
        if (!monitor) return;
        monitor->submitted = monitor->drawn;
        monitor->pending_presentation = true;
        monitor->open = false;
    }
    void touch(Key key, std::uint64_t id) noexcept {
        if (!enabled()) return;
        const auto stamp = now();
        std::lock_guard lock(mutex);
        if (!enabled() || !state) return;
        const auto index = row_for(*state, key);
        auto *monitor = monitor_for(*state, id);
        if (index == ROWS || !monitor || !monitor->open || monitor->drawn[index]) return;
        auto &row = state->rows[index];
        ++row.draws;
        if (monitor->draw_frame[index] && monitor->draw_frame[index] + 1 == monitor->frame &&
            stamp > monitor->draw_time[index]) {
            const double interval = static_cast<double>(stamp - monitor->draw_time[index]) / 1e6;
            row.draw_intervals.add(interval);
            if (monitor->refresh > 0 && interval > 1500.0 / monitor->refresh) ++row.late_draws;
        }
        monitor->draw_time[index] = stamp;
        monitor->draw_frame[index] = monitor->frame;
        monitor->drawn.set(index);
    }
    void present(std::uint64_t id, std::uint64_t stamp, bool presented, double refresh_hz) noexcept {
        if (!enabled()) return;
        const auto arrival = now();
        std::lock_guard lock(mutex);
        if (!enabled() || !state) return;
        auto *monitor = monitor_for(*state, id);
        if (!monitor) return;
        // Consume the execution-time frame association once. Direct-scanout or other
        // later output events must not keep inheriting the last animation's styles.
        const auto mask = monitor->pending_presentation ? monitor->submitted : std::bitset<ROWS>{};
        monitor->pending_presentation = false;
        if (!presented) {
            ++state->rejected_presentations;
            monitor->previous_present.reset();
            monitor->previous_callback.reset();
            monitor->present_time = monitor->callback_time = 0;
            return;
        }
        const double interval = monitor->present_time && stamp > monitor->present_time
                                    ? static_cast<double>(stamp - monitor->present_time) / 1e6 : 0;
        const double callback_interval = monitor->callback_time && arrival > monitor->callback_time
                                    ? static_cast<double>(arrival - monitor->callback_time) / 1e6 : 0;
        const double hz = std::isfinite(refresh_hz) && refresh_hz > 0 ? refresh_hz : monitor->refresh;
        for (std::size_t i = 0; i < state->row_count; ++i) {
            if (!mask[i]) continue;
            auto &row = state->rows[i];
            ++row.presentations;
            if (!stamp) ++row.untimestamped_presentations;
            if (callback_interval > 0 && monitor->previous_callback[i])
                row.presentation_callback_intervals.add(callback_interval);
            if (interval <= 0 || !monitor->previous_present[i]) continue;
            row.presentation_intervals.add(interval);
            if (hz > 0 && interval > 1500.0 / hz) {
                ++row.late_presentations;
                row.estimated_missed_refreshes += static_cast<std::uint64_t>(
                    std::max(0.0, std::round(interval * hz / 1000.0) - 1.0));
            }
        }
        monitor->present_time = stamp;
        monitor->callback_time = arrival;
        monitor->previous_present = stamp ? mask : std::bitset<ROWS>{};
        monitor->previous_callback = mask;
    }
    void remove_monitor(std::uint64_t id) noexcept {
        std::lock_guard lock(mutex);
        if (!state) return;
        for (auto &monitor : state->monitors) if (monitor.id == id) monitor = {};
    }
    void sample_memory(std::uint64_t id) noexcept {
        if (!enabled()) return;
        const auto stamp = now();
        std::lock_guard lock(mutex);
        if (!enabled() || !state || stamp - state->memory_time < MEMORY_INTERVAL) return;
        state->memory_time = stamp;
        state->current = process_memory();
        if (!state->current.valid) return;
        state->rss_peak = std::max(state->rss_peak, state->current.rss);
        // Only the output that triggered this observation contributes attribution.
        // Other outputs' retained masks may describe effects that ended long ago.
        for (const auto &monitor : state->monitors) {
            if (monitor.id != id || !id) continue;
            for (std::size_t i = 0; i < state->row_count; ++i)
                if (monitor.drawn[i]) observe_memory(state->rows[i], state->current);
        }
    }
    void resource_sample(Key key, std::uint64_t cpu_bytes, std::uint64_t gpu_bytes) noexcept {
        resource_sample(key, cpu_bytes, gpu_bytes, token.load(std::memory_order_acquire));
    }
    void resource_sample(Key key, std::uint64_t cpu_bytes, std::uint64_t gpu_bytes,
        std::uint64_t session) noexcept {
        if ((session & 1U) == 0) return;
        std::lock_guard lock(mutex);
        if (!current(session)) return;
        const auto index = row_for(*state, key);
        if (index == ROWS) return;
        auto &row = state->rows[index];
        row.known_cpu_peak = std::max(row.known_cpu_peak, cpu_bytes);
        row.known_gpu_peak = std::max(row.known_gpu_peak, gpu_bytes);
        ++row.resource_samples;
    }
    void gpu_sample(Key key, std::uint64_t session, std::uint64_t elapsed_ns) noexcept {
        if (!enabled()) return;
        std::lock_guard lock(mutex);
        if (!current(session)) return;
        const auto index = row_for(*state, key);
        if (index != ROWS) state->rows[index].gpu.add(static_cast<double>(elapsed_ns) / 1e6);
    }
    void gpu_dropped(std::uint64_t session) noexcept {
        if (!enabled()) return;
        std::lock_guard lock(mutex);
        if (current(session)) ++state->dropped_gpu;
    }
    void gpu_status(std::uint64_t session, GpuStatus status) noexcept {
        if (!enabled()) return;
        std::lock_guard lock(mutex);
        if (!current(session)) return;
        state->gpu_status = status;
        if (status == GpuStatus::Disjoint) ++state->disjoint_events;
    }
    std::string report() {
        // Formatting holds the sampling lock only on explicit report requests. Avoid a
        // multi-megabyte snapshot allocation contaminating subsequent RSS samples.
        std::lock_guard lock(mutex);
        if (!state) return "{\"schema_version\":1,\"enabled\":false,\"styles\":[]}";
        if (enabled()) {
            state->current = process_memory();
            if (state->current.valid) state->rss_peak = std::max(state->rss_peak, state->current.rss);
        }
        const bool running = enabled();
        const auto session = generation();
        const auto &data = *state;
        constexpr std::array<std::string_view, 4> GPU_STATUSES{"no_context", "unsupported", "available", "disjoint"};
        std::ostringstream out;
        out.precision(9);
        out << "{\"schema_version\":1,\"enabled\":" << (running ? "true" : "false")
            << ",\"generation\":" << session << ",\"elapsed_ms\":"
            << static_cast<double>((data.stopped ? data.stopped : now()) - data.started) / 1e6
            << ",\"retained_samples_per_series\":" << SAMPLES
            << ",\"gpu_status\":" << quoted(GPU_STATUSES[static_cast<std::size_t>(data.gpu_status)])
            << ",\"gpu_dropped_samples\":" << data.dropped_gpu
            << ",\"gpu_disjoint_events\":" << data.disjoint_events
            << ",\"dropped_style_samples\":" << data.dropped_rows
            << ",\"dropped_monitor_samples\":" << data.dropped_monitors
            << ",\"rejected_presentation_events\":" << data.rejected_presentations
            << ",\"notes\":[\"CPU is calling-thread CPU time; wall time includes waits. Scope samples are not whole-frame times.\","
               "\"Percentiles cover the most recent retained samples; means, maxima and counts cover the session.\","
               "\"Presentation cadence is output-wide while a style is drawn, not exact per-image scanout attribution. Concurrent styles share output observations.\","
               "\"Untimestamped backend presentations have callback-arrival intervals only; those are not hardware presentation timestamps.\","
               "\"GPU capture scopes are inclusive: nested window work inside workspace captures is charged to the outer workspace scope, with nested samples skipped.\","
               "\"Late intervals exceed 1.5 refresh periods; missed refreshes are estimates, especially with VRR or nested backends.\","
               "\"RSS and virtual memory belong to the entire compositor. RSS during a style is correlation, not style-owned heap usage.\","
               "\"Known resource peaks are lower-bound samples of one animation, not total style heap, simultaneous instances, or physical VRAM residency.\","
               "\"GPU results are asynchronous and may be pending at stop; zero samples means unmeasured, never zero cost.\"]"
            << ",\"memory\":{\"available\":" << (data.current.valid ? "true" : "false")
            << ",\"baseline_rss_bytes\":" << data.baseline.rss
            << ",\"current_rss_bytes\":" << data.current.rss
            << ",\"peak_sampled_rss_bytes\":" << data.rss_peak
            << ",\"rss_delta_bytes\":" << static_cast<std::int64_t>(data.current.rss) - static_cast<std::int64_t>(data.baseline.rss)
            << ",\"virtual_bytes\":" << data.current.virtual_bytes
            << ",\"collector_storage_bytes\":" << sizeof(State)
            << ",\"sampling_interval_ms\":250},\"styles\":[";
        for (std::size_t i = 0; i < data.row_count; ++i) {
            if (i) out << ',';
            const auto &row = data.rows[i];
            const auto domain = row.domain == Domain::Window ? "window" :
                row.domain == Domain::Workspace ? "workspace" : "wallpaper";
            out << "{\"domain\":" << quoted(domain)
                << ",\"style\":" << quoted(row.style.data()) << ",\"operation\":" << quoted(row.operation.data())
                << ",\"cpu\":{";
            for (std::size_t p = 0; p < PHASES.size(); ++p) {
                if (p) out << ',';
                out << quoted(PHASES[p]) << ":{\"thread_cpu\":";
                series_json(out, row.cpu[p]);
                out << ",\"wall\":";
                series_json(out, row.wall[p]);
                out << '}';
            }
            out << "},\"gpu\":";
            series_json(out, row.gpu);
            out << ",\"draw_intervals\":";
            series_json(out, row.draw_intervals);
            out << ",\"presentation_intervals\":";
            series_json(out, row.presentation_intervals);
            out << ",\"presentation_callback_intervals\":";
            series_json(out, row.presentation_callback_intervals);
            out << ",\"drawn_frames\":" << row.draws << ",\"presentation_events\":" << row.presentations
                << ",\"untimestamped_presentation_events\":" << row.untimestamped_presentations
                << ",\"late_draw_intervals\":" << row.late_draws
                << ",\"late_presentation_intervals\":" << row.late_presentations
                << ",\"estimated_missed_refreshes\":" << row.estimated_missed_refreshes
                << ",\"memory\":{\"process_rss_observed_min_bytes\":";
            if (row.rss_min == std::numeric_limits<std::uint64_t>::max()) out << "null";
            else out << row.rss_min;
            out << ",\"process_rss_observed_max_bytes\":";
            if (row.rss_min == std::numeric_limits<std::uint64_t>::max()) out << "null";
            else out << row.rss_max;
            out << ",\"resource_samples\":" << row.resource_samples
                << ",\"known_cpu_resource_peak_bytes\":" << row.known_cpu_peak
                << ",\"known_gpu_resource_peak_bytes\":" << row.known_gpu_peak << "}}";
        }
        out << "]}";
        return out.str();
    }
}  // namespace hyprimate::perf
