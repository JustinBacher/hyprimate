#include "Check.hpp"
#include "SlotPool.hpp"

#include <stdexcept>
#include <vector>

namespace {
    struct State {
        explicit State(int number) : value(number) {
            if (number == -1) {
                throw std::runtime_error("construction failed");
            }
        }
        ~State() = default;
        State(const State &) = delete;
        State &operator=(const State &) = delete;
        State(State &&) = delete;
        State &operator=(State &&) = delete;
        int value;
    };
}

int main() {
    using Pool = hyprimate::CSlotPool<State>;
    constexpr auto INVALID = Pool::INVALID_HANDLE;
    static_assert(INVALID == Pool::INVALID_HANDLE);
    Pool pool;
    CHECK(pool.get(INVALID) == nullptr);
    CHECK(!pool.erase(INVALID));
    pool.reserve(2);
    auto first = pool.emplace(10);
    State *address = pool.get(first);
    CHECK(address != nullptr);
    auto erased = pool.emplace(20);
    CHECK(pool.erase(erased));
    CHECK(pool.get(erased) == nullptr);
    CHECK(!pool.erase(erased));
    auto replacement = pool.emplace(30);
    CHECK(replacement.index == erased.index);
    CHECK(replacement != erased);
    CHECK(pool.get(erased) == nullptr);
    CHECK(pool.get(replacement)->value == 30);

    std::vector<Pool::SHandle> handles{first, replacement};
    for (int i = 0; i < 200; ++i) {
        handles.push_back(pool.emplace(i));
    }
    CHECK(pool.get(first) == address);
    CHECK(address->value == 10);
    CHECK(pool.size() == handles.size());
    std::size_t visited = 0;
    const Pool &const_pool = pool;
    const_pool.for_each([&](Pool::SHandle handle, const State &state) {
        CHECK(const_pool.get(handle) == &state);
        ++visited;
    });
    CHECK(visited == handles.size());
    pool.for_each([](Pool::SHandle, State &state) { ++state.value; });
    CHECK(pool.get(first)->value == 11);

    try {
        (void)pool.emplace(-1);
        CHECK(false);
    } catch (const std::runtime_error &) {
        CHECK(pool.size() == handles.size());
    }
    auto after_failure = pool.emplace(99);
    handles.push_back(after_failure);
    CHECK(pool.get(after_failure)->value == 99);
    pool.clear();
    CHECK(pool.size() == 0);
    for (auto handle : handles) {
        CHECK(pool.get(handle) == nullptr);
    }
    visited = 0;
    pool.for_each([&](Pool::SHandle, State &) { ++visited; });
    CHECK(visited == 0);
    auto after_clear = pool.emplace(42);
    for (auto handle : handles) {
        CHECK(handle != after_clear);
        CHECK(pool.get(handle) == nullptr);
    }
    CHECK(pool.get(after_clear)->value == 42);
    return g_failures == 0 ? 0 : 1;
}
