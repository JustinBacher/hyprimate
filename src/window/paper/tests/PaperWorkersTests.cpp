#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>

#include <dlfcn.h>
#include <pthread.h>

#include "Check.hpp"
#include "window/paper/PaperWorkers.hpp"

namespace {
    // Faults apply only to the event thread; solver allocations remain undisturbed.
    thread_local int allocation_countdown = -1;
    thread_local bool fail_after_start = false;
    thread_local bool fail_thread_start = false;
    thread_local bool saw_thread_start = false;
}  // namespace

void *operator new(std::size_t size) {
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
