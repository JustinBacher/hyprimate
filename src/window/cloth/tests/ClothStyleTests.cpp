#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include <dlfcn.h>
#include <pthread.h>

#include "Check.hpp"
#include "window/cloth/ClothResources.hpp"
#include "window/cloth/ClothWorkers.hpp"

using namespace hyprimate;

namespace {
    thread_local bool fail_next_thread = false;
    thread_local unsigned thread_attempts = 0;
    thread_local bool count_allocations = false;
    thread_local unsigned allocations = 0;

    constexpr Box WINDOW{.x = 200, .y = 150, .w = 400, .h = 300};
    constexpr Box SCREEN{.x = 0, .y = 0, .w = 1000, .h = 800};
    constexpr ClothCloseGeometry CLOSE{.edge = Edge::Bottom, .screen = SCREEN};

    bool registry_available() {
        const cloth_detail::WorkerAdmission admission;
        return admission.available();
    }

    bool begin(SClothRuntimeResources &resources, ClothFlutter &cloth, Transition kind) {
        return resources.begin(cloth, kind, ClothParams{}, 123, WINDOW, {},
            kind == Transition::Close ? std::optional{CLOSE} : std::nullopt);
    }

    void no_owner_before_valid_admission() {
        SClothRuntimeResources resources;
        ClothFlutter cloth;
        const unsigned BEFORE = thread_attempts;
        CHECK(!registry_available());
        auto bad_params = ClothParams{};
        bad_params.shine = std::numeric_limits<double>::quiet_NaN();
        CHECK(!resources.begin(cloth, Transition::Open, bad_params, 1, WINDOW, {}));
        CHECK(!resources.begin(cloth, Transition::Close, bad_params, 1, WINDOW, {}, CLOSE));
        auto bad = WINDOW;
        bad.w = 0;
        resources.begin_movement(cloth, {}, 1, bad, WINDOW, 0);
        CHECK(!cloth.active());
        bad.w = 0.5;
        resources.begin_movement(cloth, {}, 1, WINDOW, bad, 0);
        CHECK(!cloth.active());
        for (const double elapsed : {-0.01, STALL_SECONDS + 0.01,
                 std::numeric_limits<double>::quiet_NaN()}) {
            resources.begin_movement(cloth, {}, 1, WINDOW, WINDOW, elapsed);
            CHECK(!cloth.active());
        }
        auto bad_movement = ClothMovementParams{};
        bad_movement.gravity = std::numeric_limits<double>::quiet_NaN();
        resources.begin_movement(cloth, bad_movement, 1, WINDOW, WINDOW, 0);
        CHECK(!cloth.active());
        CHECK(ClothFlutter::can_update_movement(WINDOW, 0));
        CHECK(ClothFlutter::can_update_movement(WINDOW, STALL_SECONDS));
        CHECK(!registry_available());
        CHECK(thread_attempts == BEFORE);
    }

    void movement_only_and_retry() {
        SClothRuntimeResources resources;
        ClothFlutter cloth;
        const unsigned BEFORE = thread_attempts;
        fail_next_thread = true;  // The first thread is the registry reaper.
        resources.begin_movement(cloth, {}, 2, WINDOW, WINDOW, 0);
        CHECK(!fail_next_thread);
        CHECK(thread_attempts == BEFORE + 1);
        CHECK(!registry_available());
        CHECK(!cloth.active());

        resources.begin_movement(cloth, {}, 2, WINDOW, WINDOW, 0);
        CHECK(registry_available());
        CHECK(cloth.moving());
        const unsigned AFTER_START = thread_attempts;
        fail_next_thread = true;
        count_allocations = true;
        cloth.update_movement(WINDOW, 0);
        const bool RUNNING = cloth.advance(0);
        count_allocations = false;
        CHECK(RUNNING);
        CHECK(allocations == 0);
        CHECK(fail_next_thread);
        fail_next_thread = false;
        CHECK(thread_attempts == AFTER_START);
    }

    void transition_owner_failure_and_retry(Transition kind) {
        SClothRuntimeResources resources;
        ClothFlutter cloth;
        fail_next_thread = true;
        CHECK(!begin(resources, cloth, kind));
        CHECK(!fail_next_thread);
        CHECK(!registry_available());
        CHECK(begin(resources, cloth, kind));
        CHECK(cloth.active());
    }

    void transition_first_and_pool_teardown() {
        ClothFlutter retained;
        ClothFlutter retained_from_live_slot;
        {
            SClothRuntimeResources resources;
            const auto slot_handle = resources.animations.emplace();
            auto *slot = resources.animations.get(slot_handle);
            CHECK(slot != nullptr);
            if (slot == nullptr) {
                return;
            }
            CHECK(begin(resources, slot->state.cloth, Transition::Open));
            CHECK(slot->state.cloth.wait_for_recording());
            retained = slot->state.cloth;
            resources.animations.clear();
            CHECK(registry_available());
            CHECK(retained.active());
            CHECK(retained.wait_for_recording(0));
            CHECK(retained.advance(0));
            const auto live_handle = resources.animations.emplace();
            auto *live = resources.animations.get(live_handle);
            CHECK(live != nullptr);
            if (live != nullptr) {
                CHECK(begin(resources, live->state.cloth, Transition::Open));
                CHECK(live->state.cloth.wait_for_recording());
                retained_from_live_slot = live->state.cloth;
                CHECK(retained_from_live_slot.wait_for_recording(0));
            }
            CHECK(resources.animations.size() == 1);
        }
        CHECK(!registry_available());
        CHECK(retained.wait_for_recording(0));
        CHECK(retained_from_live_slot.wait_for_recording(0));
        CHECK(retained_from_live_slot.advance(0.1));
        CHECK(retained.advance(0.1));
        CHECK(retained.advance(0));
    }
}  // namespace

void *operator new(std::size_t size) {
    if (count_allocations) {
        ++allocations;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (void *memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    ::operator delete(memory);
}

// std::thread calls pthread_create in libstdc++, so interpose the first creation on this thread.
extern "C" int pthread_create(pthread_t *thread,
    const pthread_attr_t *attributes,
    void *(*entry)(void *),
    void *argument) noexcept {
    using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static const auto CREATE = std::bit_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    ++thread_attempts;
    if (fail_next_thread) {
        fail_next_thread = false;
        return EAGAIN;
    }
    return CREATE != nullptr ? CREATE(thread, attributes, entry, argument) : EAGAIN;
}

int main() {
    static_assert(!std::is_copy_constructible_v<SClothRuntimeResources>);
    no_owner_before_valid_admission();
    transition_owner_failure_and_retry(Transition::Open);
    transition_owner_failure_and_retry(Transition::Close);
    movement_only_and_retry();
    transition_first_and_pool_teardown();
    return g_failures == 0 ? 0 : 1;
}
