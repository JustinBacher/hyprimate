#include <cstddef>
#include <cstdint>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include "Check.hpp"
#include "window/paper/PaperWorkers.hpp"

namespace {
    // Allocation faults are thread-local and target solver-sized allocations in the worker.
    thread_local int allocation_countdown = -1;
    thread_local std::size_t worker_allocation_limit = 0;
    thread_local bool pause_worker_allocation = false;
    bool fail_worker_allocation = false;  // Read only by the event-thread pthread_create wrapper.
    bool wait_for_worker_allocation = false;
    std::atomic<bool> worker_allocation_entered = false;
    std::atomic<bool> release_worker_allocation = false;
    thread_local bool fail_after_start = false;
    thread_local bool fail_thread_start = false;
    thread_local bool saw_thread_start = false;

    struct WorkerStart {
        void *(*start)(void *);
        void *argument;
        bool pause_allocation;
    };

    void *start_worker_with_fault(void *argument) {
        const auto context = *static_cast<WorkerStart *>(argument);
        std::free(argument);
        worker_allocation_limit = 4096;
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
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept {
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
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
    if (fail_worker_allocation) {
        auto *context = static_cast<WorkerStart *>(std::malloc(sizeof(WorkerStart)));
        if (!context) return ENOMEM;
        *context = {start, argument, wait_for_worker_allocation};
        const int result = REAL(thread, attr, start_worker_with_fault, context);
        if (result != 0) std::free(context);
        fail_worker_allocation = false;
        wait_for_worker_allocation = false;
        if (result == 0 && fail_after_start) allocation_countdown = 0;
        return result;
    }
    const int result = REAL(thread, attr, start, argument);
    if (result == 0 && fail_after_start) allocation_countdown = 0;
    return result;
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
        // Destruction requests cancellation and joins even with work in flight.
        PaperWorkers workers;
        std::shared_ptr<PaperRun> run;
        CHECK(begin(workers, run));
    }
    return g_failures != 0;
}
