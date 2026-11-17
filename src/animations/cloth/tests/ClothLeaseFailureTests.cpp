#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>

#include <pthread.h>

#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/Core.hpp"

namespace {
    struct Attempt {
        unsigned allocations = 0;
        unsigned fail_at = 0;
        unsigned outstanding = 0;  // Protected by Tracker::mutex.
        void *first_allocation = nullptr;  // Protected by Tracker::mutex.
        bool first_freed = false;  // Protected by Tracker::mutex.
        bool injected = false;
        bool tracker_exhausted = false;
    };

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
        Attempt *const OWNER = current_attempt();
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

    enum class Startup { Transition, Movement };

    void start(ClothFlutter &cloth, Startup startup) {
        if (startup == Startup::Movement) {
            cloth.begin_movement((ClothMovementParams{}), 123, WINDOW);
        } else {
            cloth.begin(Transition::Open, (ClothParams{}), 123, WINDOW, {});
        }
    }

    struct Result {
        bool active;
        bool real_simulation;
        bool threw;
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
        return {
            .active = active,
            .real_simulation = real_simulation,
            .threw = threw,
            .remaining = wait_for_reclamation(attempt),
            .allocations = allocations,
        };
    }
}  // namespace

int main() {
    // Attempts outlive physics shutdown, including any timed-out worker cleanup.
    std::array<std::array<Attempt, MAX_ALLOCATIONS + 1>, 2> attempts{};
    Attempt warm_attempt;
    const ClothPhysics PHYSICS;
    // Filling 16 slots establishes capacity; reaping the first run leaves one free slot.
    // The warm attempt can own retained registry storage and must outlive physics.
    {
        std::array<ClothFlutter, 16> warm;
        current_attempt() = &warm_attempt;
        start(warm.front(), Startup::Movement);
        current_attempt() = nullptr;
        if (!warm.front().moving() || warm_attempt.tracker_exhausted) {
            std::fprintf(stderr, "cloth registry warmup failed\n");
            return 1;
        }
        for (std::size_t index = 1; index < warm.size(); ++index) {
            start(warm.at(index), Startup::Movement);
            if (!warm.at(index).moving()) {
                std::fprintf(stderr, "cloth registry warmup failed\n");
                return 1;
            }
        }
        warm.front().reset();
        if (!wait_for_first_free(warm_attempt)) {
            std::fprintf(stderr, "cloth registry warmup reclamation timed out\n");
            return 1;
        }
        for (std::size_t index = 1; index < warm.size(); ++index) {
            warm.at(index).reset();
        }
    }

    bool passed = true;
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
            std::fprintf(stderr,
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
            if (!attempt.injected || attempt.tracker_exhausted || RESULT.remaining != 0) {
                std::fprintf(stderr,
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
    return passed ? 0 : 1;
}
