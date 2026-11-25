#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>
#include <system_error>
#include <thread>

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "shared/Core.hpp"
#include "window/cloth/ClothFlutter.hpp"
#include "window/cloth/ClothPrivate.hpp"
#include "window/cloth/ClothSimulation.hpp"
#include "window/cloth/ClothWorkers.hpp"

namespace {
    struct SpawnGate {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
        bool fail = false, entered = false, release = false, timed_out = false, contended = false;
    };

    // Only the launching thread is intercepted, never unrelated reaper/helper threads.
    thread_local SpawnGate *s_spawn_gate = nullptr;
    thread_local SpawnGate *s_shutdown_gate = nullptr;
    thread_local pthread_t *s_capture_reaper = nullptr;

    struct ReaperGate {
        pthread_t thread{};
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
        unsigned waits = 0;
    };
    // The gate remains alive until the observed registry has shut down.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    std::atomic<ReaperGate *> s_reaper_gate{nullptr};

    timespec deadline();

    void note_reaper_wait() {
        auto *gate = s_reaper_gate.load(std::memory_order_acquire);
        if (gate != nullptr && pthread_equal(pthread_self(), gate->thread)) {
            pthread_mutex_lock(&gate->mutex);
            ++gate->waits;
            pthread_cond_broadcast(&gate->changed);
            pthread_mutex_unlock(&gate->mutex);
        }
    }

    bool wait_for_reaper_scans(ReaperGate &gate) {
        const auto LIMIT = deadline();
        pthread_mutex_lock(&gate.mutex);
        // Two wait entries guarantee a complete registry scan after the gate was armed.
        while (gate.waits < 2) {
            if (pthread_cond_timedwait(&gate.changed, &gate.mutex, &LIMIT) != 0) {
                break;
            }
        }
        const bool SCANNED = gate.waits >= 2;
        pthread_mutex_unlock(&gate.mutex);
        return SCANNED;
    }

    // Fail the large recording allocation on the worker rather than the startup thread.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    std::atomic<int> s_recording_failure{0};  // 0 disabled, 1 armed, 2 injected

    timespec deadline() {
        timespec value{};
        clock_gettime(CLOCK_REALTIME, &value);
        value.tv_sec += 10;
        return value;
    }

    bool wait_for_flag(SpawnGate &gate, const bool &flag) {
        const auto LIMIT = deadline();
        pthread_mutex_lock(&gate.mutex);
        while (!flag) {
            if (pthread_cond_timedwait(&gate.changed, &gate.mutex, &LIMIT) != 0) {
                pthread_mutex_unlock(&gate.mutex);
                return false;
            }
        }
        pthread_mutex_unlock(&gate.mutex);
        return true;
    }

    void release_gate(SpawnGate &gate) {
        pthread_mutex_lock(&gate.mutex);
        gate.release = true;
        pthread_cond_broadcast(&gate.changed);
        pthread_mutex_unlock(&gate.mutex);
    }

    struct Attempt {
        unsigned allocations = 0;
        unsigned fail_at = 0;
        unsigned outstanding = 0;  // Protected by Tracker::mutex.
        void *first_allocation = nullptr;  // Protected by Tracker::mutex.
        bool first_freed = false;  // Protected by Tracker::mutex.
        pthread_t first_freed_on{};  // Protected by Tracker::mutex.
        bool injected = false;
        bool tracker_exhausted = false;
    };

    // The worker publishes recording allocation tracking to this test's longer-lived Attempt.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    std::atomic<Attempt *> s_recording_tracker{nullptr};

    struct Slot {
        void *pointer = nullptr;
        Attempt *owner = nullptr;
    };

    struct Tracker {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
        std::array<Slot, 4096> slots{};
    };

    Tracker &tracker() {
        static Tracker instance;
        return instance;
    }

    Attempt *&current_attempt() {
        static thread_local Attempt *current = nullptr;
        return current;
    }

    void *allocate(std::size_t size, std::size_t alignment = 0) {
        constexpr std::size_t MIN_RECORDING_BYTES =
            hyprimate::SHEET_GRID_VERTICES * 6 * sizeof(float) * 100;
        int armed = 1;
        if (size >= MIN_RECORDING_BYTES &&
            s_recording_failure.compare_exchange_strong(armed, 2, std::memory_order_relaxed)) {
            throw std::bad_alloc();
        }
        Attempt *OWNER = current_attempt();
        if (OWNER == nullptr && size >= MIN_RECORDING_BYTES) {
            OWNER = s_recording_tracker.load(std::memory_order_acquire);
        }
        if (OWNER != nullptr && ++OWNER->allocations == OWNER->fail_at) {
            OWNER->injected = true;
            throw std::bad_alloc();
        }
        void *pointer = nullptr;
        if (alignment != 0) {
            if (posix_memalign(&pointer, alignment, size != 0 ? size : 1) != 0) {
                pointer = nullptr;
            }
        } else {
            // Underlying allocation for the standalone new override; ownership passes to its
            // caller. NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
            pointer = std::malloc(size != 0 ? size : 1);
        }
        if (pointer == nullptr) {
            throw std::bad_alloc();
        }
        if (OWNER != nullptr) {
            auto &state = tracker();
            pthread_mutex_lock(&state.mutex);
            Slot *empty = nullptr;
            for (auto &slot : state.slots) {
                if (slot.pointer == nullptr) {
                    empty = &slot;
                    break;
                }
            }
            if (empty == nullptr) {
                OWNER->tracker_exhausted = true;
                pthread_mutex_unlock(&state.mutex);
                // Release caller-bound storage when the tracker cannot record the allocation.
                // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
                std::free(pointer);
                throw std::bad_alloc();
            }
            *empty = {.pointer = pointer, .owner = OWNER};
            if (OWNER->first_allocation == nullptr) {
                OWNER->first_allocation = pointer;
            }
            ++OWNER->outstanding;
            pthread_mutex_unlock(&state.mutex);
        }
        return pointer;
    }

    void release(void *pointer) noexcept {
        if (pointer == nullptr) {
            return;
        }
        auto &state = tracker();
        pthread_mutex_lock(&state.mutex);
        for (auto &slot : state.slots) {
            if (slot.pointer == pointer) {
                if (!slot.owner->first_freed && slot.owner->first_allocation == pointer) {
                    slot.owner->first_freed = true;
                    slot.owner->first_freed_on = pthread_self();
                }
                --slot.owner->outstanding;
                slot = {};
                pthread_cond_broadcast(&state.changed);
                break;
            }
        }
        pthread_mutex_unlock(&state.mutex);
        // Underlying release for the standalone delete override, after updating borrowed tracker
        // slots. NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
        std::free(pointer);
    }

    [[nodiscard]] unsigned wait_for_reclamation(const Attempt &attempt) {
        timespec deadline{};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 10;
        auto &state = tracker();
        pthread_mutex_lock(&state.mutex);
        while (attempt.outstanding != 0) {
            if (pthread_cond_timedwait(&state.changed, &state.mutex, &deadline) != 0) {
                break;
            }
        }
        const unsigned REMAINING = attempt.outstanding;
        pthread_mutex_unlock(&state.mutex);
        return REMAINING;
    }

    [[nodiscard]] bool wait_for_first_free(const Attempt &attempt) {
        timespec deadline{};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 10;
        auto &state = tracker();
        pthread_mutex_lock(&state.mutex);
        while (attempt.first_allocation != nullptr && !attempt.first_freed) {
            if (pthread_cond_timedwait(&state.changed, &state.mutex, &deadline) != 0) {
                break;
            }
        }
        const bool FREED = attempt.first_allocation != nullptr && attempt.first_freed;
        pthread_mutex_unlock(&state.mutex);
        return FREED;
    }
}  // namespace

// Interpose rather than --wrap: std::thread calls pthread_create from libstdc++.so.
extern "C" int pthread_create(pthread_t *thread,
    const pthread_attr_t *attributes,
    void *(*entry)(void *),
    void *argument) noexcept {
    using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static const auto CREATE = std::bit_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    if (CREATE == nullptr) {
        return EAGAIN;
    }
    auto *gate = s_spawn_gate;
    if (gate != nullptr && gate->fail) {
        gate->entered = true;
        return EAGAIN;
    }
    const int RESULT = CREATE(thread, attributes, entry, argument);
    if (RESULT == 0 && s_capture_reaper != nullptr) {
        *s_capture_reaper = *thread;
    }
    if (gate != nullptr && RESULT == 0) {
        const auto LIMIT = deadline();
        pthread_mutex_lock(&gate->mutex);
        gate->entered = true;
        pthread_cond_broadcast(&gate->changed);
        while (!gate->release) {
            if (pthread_cond_timedwait(&gate->changed, &gate->mutex, &LIMIT) != 0) {
                gate->timed_out = true;
                break;
            }
        }
        pthread_mutex_unlock(&gate->mutex);
    }
    return RESULT;
}

// Observe the real reaper's condition-variable cycles without changing worker production code.
extern "C" int pthread_cond_timedwait(
    pthread_cond_t *condition, pthread_mutex_t *mutex, const timespec *limit) {
    using Wait = int (*)(pthread_cond_t *, pthread_mutex_t *, const timespec *);
    static const auto WAIT = std::bit_cast<Wait>(dlsym(RTLD_NEXT, "pthread_cond_timedwait"));
    note_reaper_wait();
    return WAIT != nullptr ? WAIT(condition, mutex, limit) : EINVAL;
}

extern "C" int pthread_cond_clockwait(pthread_cond_t *condition,
    pthread_mutex_t *mutex,
    clockid_t clock,
    const timespec *limit) {
    using Wait = int (*)(pthread_cond_t *, pthread_mutex_t *, clockid_t, const timespec *);
    static const auto WAIT = std::bit_cast<Wait>(dlsym(RTLD_NEXT, "pthread_cond_clockwait"));
    note_reaper_wait();
    return WAIT != nullptr ? WAIT(condition, mutex, clock, limit) : EINVAL;
}

extern "C" int __real_pthread_mutex_lock(pthread_mutex_t *mutex) noexcept;
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) noexcept {
    auto *gate = s_shutdown_gate;
    if (gate != nullptr) {
        s_shutdown_gate = nullptr;
        // The destructor's first lock is the lifecycle mutex. A failed trylock proves
        // shutdown actually contends with the paused launch, rather than merely being queued.
        const int RESULT = pthread_mutex_trylock(mutex);
        if (RESULT == 0) {
            pthread_mutex_unlock(mutex);
        }
        __real_pthread_mutex_lock(&gate->mutex);
        gate->contended = RESULT == EBUSY;
        gate->entered = true;
        pthread_cond_broadcast(&gate->changed);
        pthread_mutex_unlock(&gate->mutex);
    }
    return __real_pthread_mutex_lock(mutex);
}

void *operator new(std::size_t size) {
    return allocate(size);
}
void *operator new[](std::size_t size) {
    return allocate(size);
}
void *operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new(std::size_t size, const std::nothrow_t & /*tag*/) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, const std::nothrow_t & /*tag*/) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new(
    std::size_t size, std::align_val_t alignment, const std::nothrow_t & /*tag*/) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](
    std::size_t size, std::align_val_t alignment, const std::nothrow_t & /*tag*/) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *pointer) noexcept {
    release(pointer);
}
void operator delete[](void *pointer) noexcept {
    release(pointer);
}
void operator delete(void *pointer, std::size_t /*size*/) noexcept {
    release(pointer);
}
void operator delete[](void *pointer, std::size_t /*size*/) noexcept {
    release(pointer);
}
void operator delete(void *pointer, std::align_val_t /*alignment*/) noexcept {
    release(pointer);
}
void operator delete[](void *pointer, std::align_val_t /*alignment*/) noexcept {
    release(pointer);
}
void operator delete(void *pointer, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    release(pointer);
}
void operator delete[](
    void *pointer, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    release(pointer);
}
void operator delete(void *pointer, const std::nothrow_t & /*tag*/) noexcept {
    release(pointer);
}
void operator delete[](void *pointer, const std::nothrow_t & /*tag*/) noexcept {
    release(pointer);
}
void operator delete(
    void *pointer, std::align_val_t /*alignment*/, const std::nothrow_t & /*tag*/) noexcept {
    release(pointer);
}
void operator delete[](
    void *pointer, std::align_val_t /*alignment*/, const std::nothrow_t & /*tag*/) noexcept {
    release(pointer);
}

namespace {
    using namespace hyprimate;
    constexpr Box WINDOW{.x = 0, .y = 0, .w = 640, .h = 480};
    constexpr unsigned MAX_ALLOCATIONS = 256;

    enum class Startup : std::uint8_t { Transition, Movement };

    void start(ClothFlutter &cloth, Startup startup) {
        if (startup == Startup::Movement) {
            cloth.begin_movement((ClothMovementParams{}), 123, WINDOW);
        } else {
            constexpr std::array OBSTACLES{Box{.x = 700, .y = 0, .w = 80, .h = 200},
                Box{.x = -100, .y = 0, .w = 80, .h = 200}};
            cloth.begin(Transition::Open, (ClothParams{}), 123, WINDOW, OBSTACLES);
        }
    }

    struct Result {
        bool active;
        bool real_simulation;
        bool threw;
        bool retried;
        unsigned remaining;
        unsigned allocations;
    };

    [[nodiscard]] Result run_attempt(Attempt &attempt, Startup startup, unsigned fail_at) {
        bool active = false;
        bool real_simulation = false;
        bool threw = false;
        unsigned allocations = 0;
        {
            ClothFlutter cloth;
            attempt.fail_at = fail_at;
            current_attempt() = &attempt;
            try {
                start(cloth, startup);
            } catch (const std::bad_alloc &) {
                threw = true;
            }
            current_attempt() = nullptr;
            allocations = attempt.allocations;
            if (fail_at == 0) {
                real_simulation = startup == Startup::Movement ? cloth.wait_for_snapshots(2)
                                                               : cloth.wait_for_recording();
            }
            active = cloth.active();
            cloth.reset();
        }
        const unsigned REMAINING = wait_for_reclamation(attempt);
        ClothFlutter retry;
        start(retry, startup);
        const bool RETRIED =
            startup == Startup::Movement ? retry.wait_for_snapshots(2) : retry.wait_for_recording();
        return {
            .active = active,
            .real_simulation = real_simulation,
            .threw = threw,
            .retried = RETRIED,
            .remaining = REMAINING,
            .allocations = allocations,
        };
    }

    bool worker_recording_failure() {
        ClothFlutter cloth;
        s_recording_failure.store(1, std::memory_order_relaxed);
        cloth.begin(Transition::Open, {}, 123, WINDOW, {});
        const bool INCOMPLETE = !cloth.wait_for_recording();
        const bool INJECTED = s_recording_failure.exchange(0, std::memory_order_relaxed) == 2;
        const bool FAILED = INCOMPLETE && INJECTED;
        const bool ENTERED_FALLBACK = cloth.active() && cloth.advance(0);
        cloth.advance(.2);
        const bool RECOVERED = !cloth.active() && cloth.opacity() == 1;
        cloth.begin(Transition::Open, {}, 124, WINDOW, {});
        return FAILED && ENTERED_FALLBACK && RECOVERED && cloth.wait_for_recording();
    }

    bool restart_allocation_failure() {
        ClothFlutter cloth;
        cloth.begin_movement({}, 1, WINDOW);
        bool ready = cloth.wait_for_snapshots(2);
        for (int step = 1; step <= 64 && ready; ++step) {
            // At step 64 the sheet has reached the settling fallback, before it expires.
            cloth.advance(1.0 / 120);
            if (step < 60) {
                ready = cloth.wait_for_snapshots(step + 2);
            }
        }
        Attempt attempt;
        attempt.fail_at = 1;
        current_attempt() = &attempt;
        cloth.update_movement(Box{.x = 10, .y = 0, .w = 640, .h = 480}, 1.0 / 120);
        current_attempt() = nullptr;
        const bool FAILED_SAFELY =
            ready && attempt.injected && !cloth.active() && wait_for_reclamation(attempt) == 0;
        cloth.begin_movement({}, 2, WINDOW);
        return FAILED_SAFELY && cloth.wait_for_snapshots(2);
    }

    struct Launch {
        ClothFlutter cloth;
        Startup startup = Startup::Movement;
        SpawnGate gate;
    };

    void *launch_thread(void *argument) {
        auto &launch = *static_cast<Launch *>(argument);
        s_spawn_gate = &launch.gate;
        start(launch.cloth, launch.startup);
        s_spawn_gate = nullptr;
        return nullptr;
    }

    struct Shutdown {
        std::unique_ptr<ClothPhysics> physics;
        SpawnGate gate;
    };

    void *shutdown_thread(void *argument) {
        auto &shutdown = *static_cast<Shutdown *>(argument);
        s_shutdown_gate = &shutdown.gate;
        shutdown.physics.reset();
        s_shutdown_gate = nullptr;
        release_gate(shutdown.gate);  // completed
        return nullptr;
    }

    bool movement_copy_does_not_keep_an_idle_worker() {
        Attempt attempt;  // Outlives any tracked allocations reclaimed by the reaper.
        const ClothPhysics PHYSICS;
        ClothFlutter producer;
        current_attempt() = &attempt;
        start(producer, Startup::Movement);
        current_attempt() = nullptr;
        if (!attempt.first_allocation || !producer.wait_for_snapshots(2)) {
            return false;
        }
        auto retained = producer;
        producer.reset();
        // The retained pose can still play, but nobody can post another movement target.
        return wait_for_first_free(attempt) && retained.advance(0);
    }

    bool recording_is_destroyed_on_reaper() {
        // Keep an actual Run owner across the exact post-release reaper scans. This models the
        // releasing Lease before its shared_ptr member has finished destruction.
        ReaperGate gate;
        s_capture_reaper = &gate.thread;
        const ClothPhysics PHYSICS;
        s_capture_reaper = nullptr;
        static Attempt recording;  // Outlives a timeout and any asynchronous reclamation.
        auto run = std::make_shared<ClothFlutter::Run>();
        run->kind = Transition::Close;
        run->window = run->screen = WINDOW;
        run->frames = 169;
        run->outside_frame.store(run->frames, std::memory_order_relaxed);
        run->start = cloth_detail::rest_grid();
        std::shared_ptr<cloth_detail::Lease> lease;
        s_recording_tracker.store(&recording, std::memory_order_release);
        {
            const cloth_detail::WorkerAdmission admission;
            if (admission.available()) {
                lease = admission.launch_transition(run, cloth_detail::SimInput{});
            }
        }
        const auto LIMIT = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (lease && run->recorded.load(std::memory_order_acquire) < 1 &&
               !run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < LIMIT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        lease.reset();
        while (!run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < LIMIT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const bool FINISHED = run->finished.load(std::memory_order_acquire);
        s_recording_tracker.store(nullptr, std::memory_order_release);
        s_reaper_gate.store(&gate, std::memory_order_release);
        const bool SCANNED = FINISHED && wait_for_reaper_scans(gate);
        s_reaper_gate.store(nullptr, std::memory_order_release);
        const bool REGISTRY_OWNS_RUN = run.use_count() >= 2;
        run.reset();
        const bool FREED = wait_for_first_free(recording);
        return FINISHED && SCANNED && REGISTRY_OWNS_RUN && FREED &&
               pthread_equal(recording.first_freed_on, gate.thread);
    }

    bool shutdown_during_launch(Startup startup) {
        Shutdown shutdown{.physics = std::make_unique<ClothPhysics>(), .gate = {}};
        Launch launch;
        launch.startup = startup;
        pthread_t launcher{};
        if (pthread_create(&launcher, nullptr, launch_thread, &launch) != 0) {
            return false;
        }
        const bool LAUNCH_ENTERED = wait_for_flag(launch.gate, launch.gate.entered);
        pthread_t closer{};
        const bool CLOSER_STARTED =
            pthread_create(&closer, nullptr, shutdown_thread, &shutdown) == 0;
        const bool SHUTDOWN_ENTERED =
            CLOSER_STARTED && wait_for_flag(shutdown.gate, shutdown.gate.entered);
        pthread_mutex_lock(&shutdown.gate.mutex);
        const bool SHUTDOWN_BLOCKED = shutdown.gate.contended && !shutdown.gate.release;
        pthread_mutex_unlock(&shutdown.gate.mutex);
        release_gate(launch.gate);
        pthread_join(launcher, nullptr);
        if (CLOSER_STARTED) {
            pthread_join(closer, nullptr);
        }
        launch.cloth.reset();
        start(launch.cloth, startup);
        const bool NO_RUNTIME_IDLE = !launch.cloth.active();
        const ClothPhysics RETRY;
        start(launch.cloth, startup);
        const bool READY = startup == Startup::Movement ? launch.cloth.wait_for_snapshots(2)
                                                        : launch.cloth.wait_for_recording();
        return LAUNCH_ENTERED && SHUTDOWN_ENTERED && SHUTDOWN_BLOCKED && !launch.gate.timed_out &&
               NO_RUNTIME_IDLE && READY;
    }

    // First-owner allocation failures must also leave the runtime retryable.
    bool owner_lifetimes() {
        for (unsigned fail_at = 1; fail_at <= 2; ++fail_at) {
            Attempt attempt;
            attempt.fail_at = fail_at;
            bool allocation_failed = false;
            current_attempt() = &attempt;
            try {
                const ClothPhysics FAIL;
            } catch (const std::bad_alloc &) {
                allocation_failed = true;
            }
            current_attempt() = nullptr;
            if (!allocation_failed || !attempt.injected || wait_for_reclamation(attempt) != 0) {
                return false;
            }
        }
        // Reaper construction failure must not poison the next first owner.
        SpawnGate failure;
        failure.fail = true;
        bool threw = false;
        s_spawn_gate = &failure;
        try {
            const ClothPhysics FAIL;
        } catch (const std::system_error &) {
            threw = true;
        }
        s_spawn_gate = nullptr;
        if (!threw || !failure.entered) {
            return false;
        }
        auto first = std::make_unique<ClothPhysics>();
        const ClothPhysics SECOND;
        ClothFlutter cloth;
        start(cloth, Startup::Movement);
        first.reset();
        return cloth.wait_for_snapshots(2);
    }
}  // namespace

int main() {
    // Attempts outlive physics shutdown, including any timed-out worker cleanup.
    std::array<std::array<Attempt, MAX_ALLOCATIONS + 1>, 2> attempts{};
    Attempt warm_attempt;
    bool passed = owner_lifetimes();
    if (!movement_copy_does_not_keep_an_idle_worker()) {
        std::fputs("retained movement kept its cancelled producer alive\n", stderr);
        passed = false;
    }
    passed = shutdown_during_launch(Startup::Transition) && passed;
    passed = shutdown_during_launch(Startup::Movement) && passed;
    if (!recording_is_destroyed_on_reaper()) {
        std::fputs("cloth recording was not reclaimed by its reaper\n", stderr);
        passed = false;
    }
    const ClothPhysics PHYSICS;
    if (!worker_recording_failure()) {
        std::fputs("worker recording allocation failure/retry failed\n", stderr);
        passed = false;
    }
    // Filling 16 slots establishes capacity; reaping the first run leaves one free slot.
    // The warm attempt can own retained registry storage and must outlive physics.
    {
        std::array<ClothFlutter, 16> warm;
        current_attempt() = &warm_attempt;
        start(warm.front(), Startup::Movement);
        current_attempt() = nullptr;
        if (!warm.front().moving() || warm_attempt.tracker_exhausted) {
            (void)std::fprintf(stderr, "cloth registry warmup failed\n");
            return 1;
        }
        for (std::size_t index = 1; index < warm.size(); ++index) {
            start(warm.at(index), Startup::Movement);
            if (!warm.at(index).moving()) {
                (void)std::fprintf(stderr, "cloth registry warmup failed\n");
                return 1;
            }
        }
        warm.front().reset();
        if (!wait_for_first_free(warm_attempt)) {
            (void)std::fprintf(stderr, "cloth registry warmup reclamation timed out\n");
            return 1;
        }
        for (std::size_t index = 1; index < warm.size(); ++index) {
            warm.at(index).reset();
        }
    }

    passed = restart_allocation_failure() && passed;
    {
        ClothFlutter cloth;
        cloth.begin_movement({}, 123, WINDOW);
        const bool READY = cloth.wait_for_snapshots(2);
        Attempt retune_attempt;
        retune_attempt.fail_at = 1;
        current_attempt() = &retune_attempt;
        cloth.set_movement_params(ClothMovementParams{.gravity = 2});
        cloth.advance(1.0 / 120);
        current_attempt() = nullptr;
        if (!READY || !cloth.moving() || retune_attempt.allocations != 0 ||
            retune_attempt.injected) {
            (void)std::fprintf(stderr, "movement retune allocated or lost its lease\n");
            passed = false;
        }
    }
    for (const auto STARTUP : {Startup::Transition, Startup::Movement}) {
        SpawnGate failure;
        failure.fail = true;
        ClothFlutter cloth;
        s_spawn_gate = &failure;
        start(cloth, STARTUP);
        s_spawn_gate = nullptr;
        const bool FAILED_SAFELY = failure.entered && !cloth.active();
        start(cloth, STARTUP);
        const bool RETRIED =
            STARTUP == Startup::Movement ? cloth.wait_for_snapshots(2) : cloth.wait_for_recording();
        if (!FAILED_SAFELY || !RETRIED) {
            (void)std::fprintf(stderr, "thread spawn failure/retry failed\n");
            passed = false;
        }
    }
    constexpr std::array MODES{Startup::Transition, Startup::Movement};
    for (std::size_t mode_index = 0; mode_index < MODES.size(); ++mode_index) {
        const Startup STARTUP = MODES.at(mode_index);
        const char *const NAME = STARTUP == Startup::Movement ? "movement" : "transition";
        auto &cases = attempts.at(mode_index);
        const Result BASELINE = run_attempt(cases.front(), STARTUP, 0);
        const unsigned COUNT = BASELINE.allocations;
        if (!BASELINE.active || !BASELINE.real_simulation || BASELINE.threw ||
            BASELINE.remaining != 0 || cases.front().injected || cases.front().tracker_exhausted ||
            COUNT == 0 || COUNT > MAX_ALLOCATIONS) {
            (void)std::fprintf(stderr,
                "%s baseline failed: active=%d real_simulation=%d threw=%d remaining=%u "
                "allocations=%u tracker_exhausted=%d\n",
                NAME,
                static_cast<int>(BASELINE.active),
                static_cast<int>(BASELINE.real_simulation),
                static_cast<int>(BASELINE.threw),
                BASELINE.remaining,
                COUNT,
                static_cast<int>(cases.front().tracker_exhausted));
            passed = false;
        }
        const unsigned LIMIT = COUNT < MAX_ALLOCATIONS ? COUNT : MAX_ALLOCATIONS;
        for (unsigned index = 1; index <= LIMIT; ++index) {
            Attempt &attempt = cases.at(index);
            const Result RESULT = run_attempt(attempt, STARTUP, index);
            if (!attempt.injected || attempt.tracker_exhausted || RESULT.remaining != 0 ||
                RESULT.threw || !RESULT.retried) {
                (void)std::fprintf(stderr,
                    "%s allocation %u failed: injected=%d remaining=%u tracker_exhausted=%d\n",
                    NAME,
                    index,
                    static_cast<int>(attempt.injected),
                    RESULT.remaining,
                    static_cast<int>(attempt.tracker_exhausted));
                passed = false;
            }
        }
    }
    if (!passed) {
        (void)std::fprintf(stderr, "cloth runtime lifetime/failure regression failed\n");
    }
    return passed ? 0 : 1;
}
