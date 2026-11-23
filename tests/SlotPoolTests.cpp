#include <stdexcept>
#include <vector>

#include "Check.hpp"
#include "window/SlotPool.hpp"

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
}  // namespace

int main() try {
    using Pool = hyprimate::CSlotPool<State>;
    constexpr auto INVALID = Pool::INVALID_HANDLE;
    static_assert(INVALID == Pool::INVALID_HANDLE);
    Pool pool;
    CHECK(pool.get(INVALID) == nullptr);
    CHECK(!pool.erase(INVALID));
    pool.reserve(2);
    const auto FIRST = pool.emplace(10);
    const State *address = pool.get(FIRST);
    CHECK(address != nullptr);
    const auto ERASED = pool.emplace(20);
    CHECK(pool.erase(ERASED));
    CHECK(pool.get(ERASED) == nullptr);
    CHECK(!pool.erase(ERASED));
    const auto REPLACEMENT = pool.emplace(30);
    CHECK(REPLACEMENT.index == ERASED.index);
    CHECK(REPLACEMENT != ERASED);
    CHECK(pool.get(ERASED) == nullptr);
    CHECK(pool.get(REPLACEMENT)->value == 30);

    std::vector<Pool::SHandle> handles{FIRST, REPLACEMENT};
    for (int i = 0; i < 200; ++i) {
        handles.push_back(pool.emplace(i));
    }
    CHECK(pool.get(FIRST) == address);
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
    CHECK(pool.get(FIRST)->value == 11);

    try {
        (void)pool.emplace(-1);
        CHECK(false);
    } catch (const std::runtime_error &) {
        CHECK(pool.size() == handles.size());
    }
    const auto AFTER_FAILURE = pool.emplace(99);
    handles.push_back(AFTER_FAILURE);
    CHECK(pool.get(AFTER_FAILURE)->value == 99);
    pool.clear();
    CHECK(pool.size() == 0);
    for (const auto HANDLE : handles) {
        CHECK(pool.get(HANDLE) == nullptr);
    }
    visited = 0;
    pool.for_each([&](Pool::SHandle, State &) { ++visited; });
    CHECK(visited == 0);
    const auto AFTER_CLEAR = pool.emplace(42);
    for (const auto HANDLE : handles) {
        CHECK(HANDLE != AFTER_CLEAR);
        CHECK(pool.get(HANDLE) == nullptr);
    }
    CHECK(pool.get(AFTER_CLEAR)->value == 42);
    return g_failures == 0 ? 0 : 1;
} catch (const std::exception &) {
    return 1;
}
