#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <thread>
#include <utility>

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include "Check.hpp"
#include "window/paper/PaperInteraction.hpp"
#include "window/paper/PaperTopology.hpp"
#include "window/paper/PaperWorkers.hpp"

namespace {
    // Allocation faults are thread-local and target solver-sized allocations in the worker.
    thread_local int allocation_countdown = -1;
    thread_local std::size_t worker_allocation_limit = 0;
    thread_local bool pause_worker_allocation = false;
    // This fault identifies libstdc++'s 48-to-96 node-buffer growth for the
    // fixed low-quality fixture, then fails the first split-parent allocation.
    // The worker-local pointers also let the test pause its solver destruction.
    thread_local bool mutation_fault = false, pause_mutation_destruction = false;
    thread_local void *initial_nodes = nullptr, *grown_nodes = nullptr;
    thread_local bool growing_nodes = false, nodes_grown = false;
    bool fail_worker_mutation = false;
    bool fail_worker_allocation = false;  // Read only by the event-thread pthread_create wrapper.
    bool wait_for_worker_allocation = false;
    std::atomic<bool> worker_allocation_entered = false;
    std::atomic<bool> release_worker_allocation = false;
    std::atomic<bool> worker_destruction_entered = false;
    std::atomic<bool> release_worker_destruction = false;
    thread_local bool observe_worker_join = false;
    std::atomic<bool> worker_join_entered = false;
    thread_local bool fail_after_start = false;
    thread_local bool fail_thread_start = false;
    thread_local bool saw_thread_start = false;

    struct WorkerStart {
        void *(*start)(void *);
        void *argument;
        bool pause_allocation;
        bool mutation;
    };

    void *start_worker_with_fault(void *argument) {
        const auto context = *static_cast<WorkerStart *>(argument);
        std::free(argument);
        worker_allocation_limit = context.mutation ? 0 : 4096;
        mutation_fault = context.mutation;
        pause_worker_allocation = context.pause_allocation;
        return context.start(context.argument);
    }

    class StderrCapture {
      public:
        StderrCapture() : m_file(std::tmpfile()) {
            if (!m_file) return;
            m_saved = ::dup(STDERR_FILENO);
            if (m_saved < 0) return;
            if (::dup2(::fileno(m_file), STDERR_FILENO) < 0) return;
            m_redirected = true;
        }
        ~StderrCapture() {
            restore();
            if (m_file) std::fclose(m_file);
        }
        StderrCapture(const StderrCapture &) = delete;
        StderrCapture &operator=(const StderrCapture &) = delete;

        bool ready() const {
            return m_redirected;
        }
        bool read(char *buffer, std::size_t capacity, std::size_t &length) {
            if (!m_redirected || !buffer || capacity == 0) return false;
            std::fflush(stderr);
            if (std::fseek(m_file, 0, SEEK_END) != 0) return false;
            const long end = std::ftell(m_file);
            if (end < 0 || static_cast<unsigned long>(end) >= capacity) return false;
            if (std::fseek(m_file, 0, SEEK_SET) != 0) return false;
            length = std::fread(buffer, 1, static_cast<std::size_t>(end), m_file);
            buffer[length] = '\0';
            return length == static_cast<std::size_t>(end);
        }

      private:
        void restore() {
            if (m_redirected) {
                std::fflush(stderr);
                (void)::dup2(m_saved, STDERR_FILENO);
                m_redirected = false;
            }
            if (m_saved >= 0) {
                (void)::close(m_saved);
                m_saved = -1;
            }
        }

        std::FILE *m_file = nullptr;
        int m_saved = -1;
        bool m_redirected = false;
    };

    std::uint64_t effective_seed() {
        constexpr std::uint64_t SEED = 7;
        const char *value = std::getenv("HYPRIMATE_PAPER_REPLAY_SEED");
        if (!value) return SEED;
        std::uint64_t replay_seed = 0;
        const char *end = value + std::strlen(value);
        const auto [position, error] = std::from_chars(value, end, replay_seed);
        return error == std::errc{} && position == end ? replay_seed : SEED;
    }
}  // namespace

void *operator new(std::size_t size) {
    if (mutation_fault && nodes_grown && size == sizeof(std::array<std::uint64_t, 3>)) {
        mutation_fault = false;
        pause_mutation_destruction = true;
        worker_allocation_entered.store(true, std::memory_order_release);
        while (!release_worker_allocation.load(std::memory_order_acquire))
            std::this_thread::yield();
        throw std::bad_alloc();
    }
    if (mutation_fault && initial_nodes &&
        size == 2 * 48 * sizeof(hyprimate::paper::Node))
        growing_nodes = true;
    if (worker_allocation_limit != 0 && size >= worker_allocation_limit) {
        worker_allocation_limit = 0;
        if (pause_worker_allocation) {
            worker_allocation_entered.store(true, std::memory_order_release);
            while (!release_worker_allocation.load(std::memory_order_acquire))
                std::this_thread::yield();
            pause_worker_allocation = false;
        }
        throw std::bad_alloc();
    }
    if (allocation_countdown == -2 || allocation_countdown == 0) {
        if (allocation_countdown == 0) allocation_countdown = -1;
        throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (void *memory = std::malloc(size ? size : 1)) {
        if (mutation_fault && !initial_nodes && size == 48 * sizeof(hyprimate::paper::Node))
            initial_nodes = memory;
        if (mutation_fault && growing_nodes &&
            size == 2 * 48 * sizeof(hyprimate::paper::Node))
            grown_nodes = memory;
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept {
    if (mutation_fault && growing_nodes && memory == initial_nodes) nodes_grown = true;
    if (pause_mutation_destruction && memory == grown_nodes) {
        pause_mutation_destruction = false;
        worker_destruction_entered.store(true, std::memory_order_release);
        while (!release_worker_destruction.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept {
    ::operator delete(memory);
}

extern "C" int pthread_create(
    pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *argument) {
    if (fail_thread_start) {
        saw_thread_start = true;
        return EAGAIN;
    }
    using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static const auto REAL = reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    if (!REAL) std::abort();
    if (fail_worker_allocation || fail_worker_mutation) {
        auto *context = static_cast<WorkerStart *>(std::malloc(sizeof(WorkerStart)));
        if (!context) return ENOMEM;
        *context = {start, argument, wait_for_worker_allocation, fail_worker_mutation};
        const int result = REAL(thread, attr, start_worker_with_fault, context);
        if (result != 0) std::free(context);
        fail_worker_allocation = false;
        fail_worker_mutation = false;
        wait_for_worker_allocation = false;
        if (result == 0 && fail_after_start) allocation_countdown = 0;
        return result;
    }
    const int result = REAL(thread, attr, start, argument);
    if (result == 0 && fail_after_start) allocation_countdown = 0;
    return result;
}

extern "C" int pthread_join(pthread_t thread, void **result) {
    using Join = int (*)(pthread_t, void **);
    static const auto REAL = reinterpret_cast<Join>(dlsym(RTLD_NEXT, "pthread_join"));
    if (!REAL) std::abort();
    if (observe_worker_join) worker_join_entered.store(true, std::memory_order_release);
    return REAL(thread, result);
}

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{1000, 700};
    constexpr Vec2 CURSOR{0.5, 0.5};
    const PaperParams PARAMS{.duration = 0.5, .crumpliness = 0.3, .shading = 0.0, .variation = 1.0};

    bool begin(PaperWorkers &workers, std::shared_ptr<PaperRun> &run) {
        return workers.begin(run, Transition::Close, PARAMS, 7, SIZE, CURSOR);
    }

    bool publishes(PaperWorkers &workers, const std::shared_ptr<PaperRun> &run) {
        PaperSurface display;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto status = workers.present(run, 1.0 / 120.0, display);
            if (!display.faces.empty()) return status != PaperPlayback::Interrupted;
            if (status != PaperPlayback::Running) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    bool publishes_without_time(
        PaperWorkers &workers, const std::shared_ptr<PaperRun> &run, PaperSurface &display) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto status = workers.present(run, 0, display);
            if (!display.faces.empty()) return status == PaperPlayback::Running;
            if (status != PaperPlayback::Running) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    std::shared_ptr<PaperSurface> curved_surface() {
        PaperInteraction interaction;
        auto initial = std::make_shared<PaperSurface>();
        animation::Step step{};
        step.body = step.capture = {0, 0, SIZE.x, SIZE.y};
        step.elapsed = 1.0 / 60;
        (void)interaction.advance(*initial, {}, step);
        const double width = std::sqrt(SIZE.x / SIZE.y), height = 1 / width;
        constexpr double RADIUS = 0.45;
        for (auto &point : initial->points) {
            const double angle = (point.uv.x - 0.5) * width / RADIUS;
            point.position = {RADIUS * std::sin(angle), (point.uv.y - 0.5) * height,
                RADIUS * (1 - std::cos(angle))};
        }
        return initial;
    }
    bool same_pose(const PaperSurface &a, const PaperSurface &b) {
        if (a.points.size() != b.points.size() || a.faces.size() != b.faces.size() ||
            a.points.empty() || a.faces.empty()) return false;
        for (std::size_t i = 0; i < a.points.size(); ++i) {
            const auto &p = a.points[i], &q = b.points[i];
            if (p.position.x != q.position.x || p.position.y != q.position.y ||
                p.position.z != q.position.z || p.uv.x != q.uv.x || p.uv.y != q.uv.y ||
                p.contact != q.contact)
                return false;
        }
        for (std::size_t i = 0; i < a.faces.size(); ++i)
            if (a.faces[i].vertex != b.faces[i].vertex || a.faces[i].crease != b.faces[i].crease)
                return false;
        return true;
    }
    bool fresh_low_surface(const PaperSurface &surface) {
        // Low quality at SIZE has 7 by 5 cells. A fresh close starts flat.
        if (surface.points.size() != 48 || surface.faces.size() != 70) return false;
        const double width = std::sqrt(SIZE.x / SIZE.y), height = 1 / width;
        for (const auto &point : surface.points) {
            if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y) ||
                !std::isfinite(point.position.z) || !std::isfinite(point.uv.x) ||
                !std::isfinite(point.uv.y) || point.uv.x < 0 || point.uv.x > 1 ||
                point.uv.y < 0 || point.uv.y > 1 ||
                std::abs(point.position.x - (point.uv.x - 0.5) * width) > 1e-10 ||
                std::abs(point.position.y - (point.uv.y - 0.5) * height) > 1e-10 ||
                std::abs(point.position.z) > 1e-10)
                return false;
        }
        double area = 0;
        for (const auto &face : surface.faces) {
            if (face.vertex[0] >= surface.points.size() ||
                face.vertex[1] >= surface.points.size() ||
                face.vertex[2] >= surface.points.size()) return false;
            const auto a = surface.points[face.vertex[0]].uv;
            const auto b = surface.points[face.vertex[1]].uv;
            const auto c = surface.points[face.vertex[2]].uv;
            const double twice = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (twice <= 0) return false;
            area += twice / 2;
        }
        return std::abs(area - 1) < 1e-10;
    }
}  // namespace

int main() {
    {
        char diagnostic[512]{};
        std::size_t diagnostic_size = 0;
        bool capture_ready = false, started = false, interrupted = false, captured = false;
        bool recovered = false;
        {
            StderrCapture capture;
            capture_ready = capture.ready();
            if (capture_ready) {
                {
                    PaperWorkers workers;
                    std::shared_ptr<PaperRun> run;
                    fail_worker_allocation = true;
                    started = begin(workers, run);
                    fail_worker_allocation = false;
                    PaperSurface display;
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (started && std::chrono::steady_clock::now() < deadline) {
                        if (workers.present(run, 0, display) == PaperPlayback::Interrupted) {
                            interrupted = true;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    if (interrupted) recovered = begin(workers, run) && publishes(workers, run);
                    workers.cancel(run);
                }
                captured = capture.read(diagnostic, sizeof(diagnostic), diagnostic_size);
            }
        }
        char expected[512]{};
        char seed_text[64]{};
        (void)std::snprintf(seed_text, sizeof(seed_text), "seed=%" PRIu64, effective_seed());
        const int expected_size = std::snprintf(expected,
            sizeof(expected),
            "hyprimate: paper worker phase=begin direction=close %s: %s\n",
            seed_text,
            std::bad_alloc{}.what());
        CHECK(capture_ready && started && interrupted && captured);
        CHECK(expected_size > 0 && static_cast<std::size_t>(expected_size) == diagnostic_size);
        CHECK(expected_size > 0 && static_cast<std::size_t>(expected_size) < sizeof(expected) &&
            std::strcmp(diagnostic, expected) == 0);
        CHECK(recovered);
    }
    {
        char diagnostic[512]{};
        std::size_t diagnostic_size = 0;
        bool capture_ready = false, started = false, fault_reached = false;
        bool cancelled = false, recovery_published = false, captured = false;
        {
            StderrCapture capture;
            capture_ready = capture.ready();
            if (capture_ready) {
                worker_allocation_entered.store(false, std::memory_order_relaxed);
                release_worker_allocation.store(false, std::memory_order_relaxed);
                {
                    PaperWorkers workers;
                    std::shared_ptr<PaperRun> run;
                    wait_for_worker_allocation = true;
                    fail_worker_allocation = true;
                    started = begin(workers, run);
                    fail_worker_allocation = false;
                    wait_for_worker_allocation = false;
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (started && std::chrono::steady_clock::now() < deadline &&
                        !worker_allocation_entered.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    fault_reached = worker_allocation_entered.load(std::memory_order_acquire);
                    if (fault_reached) {
                        workers.cancel(run);
                        cancelled = !run;
                    } else {
                        workers.cancel(run);
                    }
                    release_worker_allocation.store(true, std::memory_order_release);
                    if (fault_reached && cancelled) {
                        std::shared_ptr<PaperRun> recovery;
                        recovery_published = begin(workers, recovery) && publishes(workers, recovery);
                        workers.cancel(recovery);
                    }
                }
                captured = capture.read(diagnostic, sizeof(diagnostic), diagnostic_size);
            }
        }
        CHECK(capture_ready && started && fault_reached && cancelled && recovery_published);
        CHECK(captured && diagnostic_size == 0);
    }
    {
        char diagnostic[512]{};
        std::size_t diagnostic_size = 0;
        bool capture_ready = false, started = false, fault_reached = false;
        bool superseded = false, replacement_published = false, captured = false;
        {
            StderrCapture capture;
            capture_ready = capture.ready();
            if (capture_ready) {
                worker_allocation_entered.store(false, std::memory_order_relaxed);
                release_worker_allocation.store(false, std::memory_order_relaxed);
                {
                    PaperWorkers workers;
                    std::shared_ptr<PaperRun> run;
                    wait_for_worker_allocation = true;
                    fail_worker_allocation = true;
                    started = begin(workers, run);
                    fail_worker_allocation = false;
                    wait_for_worker_allocation = false;
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (started && std::chrono::steady_clock::now() < deadline &&
                        !worker_allocation_entered.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    fault_reached = worker_allocation_entered.load(std::memory_order_acquire);
                    if (fault_reached) superseded = begin(workers, run);
                    if (!superseded) workers.cancel(run);
                    release_worker_allocation.store(true, std::memory_order_release);
                    if (superseded) {
                        replacement_published = publishes(workers, run);
                        workers.cancel(run);
                    }
                }
                captured = capture.read(diagnostic, sizeof(diagnostic), diagnostic_size);
            }
        }
        CHECK(capture_ready && started && fault_reached && superseded && replacement_published);
        CHECK(captured && diagnostic_size == 0);
    }
    {
        // No physical tick is queued: a same-handle reversal must retain the
        // non-flat published pose rather than reconstructing a fresh solver.
        PaperWorkers workers;
        std::shared_ptr<PaperRun> run;
        auto params = PARAMS;
        params.simulation_quality = PaperQuality::Low;
        const auto initial = curved_surface();
        const bool started =
            workers.begin(run, Transition::Close, params, 7, SIZE, CURSOR, initial);
        PaperSurface before, after;
        const bool first_published = started && publishes_without_time(workers, run, before);
        const bool reversed = first_published &&
            workers.begin(run, Transition::Open, params, 8, SIZE, CURSOR);
        const bool second_published = reversed && publishes_without_time(workers, run, after);
        CHECK(started && first_published && reversed && second_published);
        CHECK(second_published && same_pose(before, after));
        workers.cancel(run);
    }
    {
        // Start on a gently cylindrical interaction sheet: its curvature forces
        // adaptive bisection without relying on a particular random force seed.
        auto initial = curved_surface();
        char diagnostic[512]{};
        std::size_t diagnostic_size = 0;
        bool capture_ready = false, started = false, fault_reached = false;
        bool superseded = false, old_frame_suppressed = false, replacement_valid = false;
        bool destruction_reached = false, busy_held = false, second_worker_progress = false;
        bool captured = false;
        {
            StderrCapture capture;
            capture_ready = capture.ready();
            if (capture_ready) {
                worker_allocation_entered.store(false, std::memory_order_relaxed);
                release_worker_allocation.store(false, std::memory_order_relaxed);
                worker_destruction_entered.store(false, std::memory_order_relaxed);
                release_worker_destruction.store(false, std::memory_order_relaxed);
                {
                    PaperWorkers workers;
                    std::shared_ptr<PaperRun> run;
                    auto params = PARAMS;
                    params.simulation_quality = PaperQuality::Low;
                    fail_worker_mutation = true;
                    started = workers.begin(run, Transition::Close, params, 7, SIZE, CURSOR, initial);
                    fail_worker_mutation = false;
                    PaperSurface old_display;
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (started && std::chrono::steady_clock::now() < deadline &&
                        !worker_allocation_entered.load(std::memory_order_acquire)) {
                        if (workers.present(run, 1.0 / 240, old_display) != PaperPlayback::Running)
                            break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    fault_reached = worker_allocation_entered.load(std::memory_order_acquire);
                    PaperSurface replacement;
                    if (fault_reached) {
                        superseded = workers.begin(run, Transition::Close, params, 7, SIZE, CURSOR);
                        if (superseded)
                            old_frame_suppressed =
                                workers.present(run, 0, replacement) == PaperPlayback::Running &&
                                replacement.faces.empty();
                    }
                    release_worker_allocation.store(true, std::memory_order_release);
                    if (fault_reached) {
                        const auto destruction_deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (std::chrono::steady_clock::now() < destruction_deadline &&
                            !worker_destruction_entered.load(std::memory_order_acquire))
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        destruction_reached =
                            worker_destruction_entered.load(std::memory_order_acquire);
                    }
                    if (destruction_reached && superseded) {
                        // The pool's production limit permits two workers only on
                        // systems reporting at least six hardware threads.
                        second_worker_progress = std::thread::hardware_concurrency() < 6;
                        if (!second_worker_progress) {
                            std::shared_ptr<PaperRun> other;
                            PaperSurface other_display;
                            second_worker_progress = begin(workers, other) &&
                                publishes_without_time(workers, other, other_display);
                            workers.cancel(other);
                        }
                        busy_held = workers.present(run, 0, replacement) ==
                                PaperPlayback::Running &&
                            replacement.faces.empty();
                    }
                    release_worker_destruction.store(true, std::memory_order_release);
                    if (superseded) {
                        const auto replacement_deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (std::chrono::steady_clock::now() < replacement_deadline) {
                            const auto status = workers.present(run, 0, replacement);
                            if (!replacement.faces.empty()) {
                                replacement_valid = status == PaperPlayback::Running &&
                                    fresh_low_surface(replacement);
                                break;
                            }
                            if (status != PaperPlayback::Running) break;
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                    workers.cancel(run);
                }
                captured = capture.read(diagnostic, sizeof(diagnostic), diagnostic_size);
            }
        }
        CHECK(capture_ready && started && fault_reached && superseded && destruction_reached);
        CHECK(second_worker_progress && busy_held && old_frame_suppressed && replacement_valid);
        CHECK(captured && diagnostic_size == 0);
    }
    {
        PaperWorkers workers;
        std::shared_ptr<PaperRun> run;
        allocation_countdown = 0;  // pool reserve, before the first worker exists
        CHECK(!begin(workers, run));
        allocation_countdown = -1;
        CHECK(!run);
        fail_thread_start = true;
        CHECK(!begin(workers, run));
        fail_thread_start = false;
        CHECK(saw_thread_start && !run);
        // A successfully started worker remains available after run allocation fails.
        fail_after_start = true;  // fail the first allocation after pthread_create succeeds
        CHECK(!begin(workers, run));
        fail_after_start = false;
        allocation_countdown = -1;
        CHECK(!run);
        // Sweep every event-thread allocation through the first successful admission.
        // PaperRun's members can allocate between make_shared and queue growth.
        bool registered = false;
        for (int allocation = 0; allocation < 64 && !registered; ++allocation) {
            allocation_countdown = allocation;
            registered = begin(workers, run);
            allocation_countdown = -1;
            if (!registered && run) {
                CHECK(!run);  // An unqueued handle must never be passed back to the caller.
                return 1;
            }
        }
        CHECK(registered);
        if (!registered) return 1;
        CHECK(publishes(workers, run));

        std::shared_ptr<PaperRun> other;
        allocation_countdown = -2;
        CHECK(!begin(workers, other));
        allocation_countdown = -1;
        CHECK(!other);
        CHECK(publishes(workers, run));
        CHECK(begin(workers, other));
        CHECK(publishes(workers, other));
        workers.cancel(other);
        workers.cancel(run);
    }
    {
        // Hold a worker after it takes the close handoff, then destroy its pool.
        // No animation or event-thread handle remains to keep the surface alive.
        auto initial = curved_surface();
        std::weak_ptr<const PaperSurface> handoff = initial;
        auto workers = std::make_unique<PaperWorkers>();
        std::shared_ptr<PaperRun> run;
        worker_allocation_entered.store(false, std::memory_order_relaxed);
        release_worker_allocation.store(false, std::memory_order_relaxed);
        worker_join_entered.store(false, std::memory_order_relaxed);
        wait_for_worker_allocation = true;
        fail_worker_allocation = true;
        const bool started = workers->begin(run, Transition::Close, PARAMS, 7, SIZE, CURSOR, initial);
        fail_worker_allocation = false;
        wait_for_worker_allocation = false;
        const auto allocation_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (started && !worker_allocation_entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now() < allocation_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool paused = worker_allocation_entered.load(std::memory_order_acquire);
        initial.reset();
        run.reset();

        std::atomic<bool> teardown_done = false;
        std::thread teardown([owner = std::move(workers), &teardown_done]() mutable {
            observe_worker_join = true;
            owner.reset();
            observe_worker_join = false;
            teardown_done.store(true, std::memory_order_release);
        });
        const auto join_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!worker_join_entered.load(std::memory_order_acquire) &&
            !teardown_done.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now() < join_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool joining = worker_join_entered.load(std::memory_order_acquire);
        CHECK(started && paused && joining);
        if (paused && joining) {
            CHECK(!teardown_done.load(std::memory_order_acquire));
            CHECK(!handoff.expired());
        }
        // Release even on failure so neither the worker nor teardown can hang the test.
        release_worker_allocation.store(true, std::memory_order_release);
        teardown.join();
        CHECK(teardown_done.load(std::memory_order_acquire));
        CHECK(handoff.expired());
    }
    {
        // Destruction requests cancellation and joins even with work in flight.
        PaperWorkers workers;
        std::shared_ptr<PaperRun> run;
        CHECK(begin(workers, run));
    }
    return g_failures != 0;
}
