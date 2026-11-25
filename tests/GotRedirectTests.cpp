#include <cstdint>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "Check.hpp"
#include "gl/GotRedirect.hpp"
#include "gl/IndexedDrawContract.hpp"

namespace {
    int pins = 0;
    bool pin(void *) noexcept { ++pins; return true; }
    pid_t replacement() { return static_cast<pid_t>(424242); }
    pid_t other() { return static_cast<pid_t>(424243); }
    __attribute__((noinline)) pid_t call_getpid() { return getpid(); }

    bool set_slot(void **slot, void *value) {
        const long size = sysconf(_SC_PAGESIZE);
        if (size <= 0) return false;
        const auto address = reinterpret_cast<std::uintptr_t>(slot);
        void *page = reinterpret_cast<void *>(address - address % static_cast<std::uintptr_t>(size));
        if (mprotect(page, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE)) return false;
        __atomic_store_n(slot, value, __ATOMIC_SEQ_CST);
        return mprotect(page, static_cast<std::size_t>(size), PROT_READ) == 0;
    }
}

int main() {
    using hyprimate::gl::GotRedirect;
    using hyprimate::gl::IndexedDrawContract;
    const pid_t original = call_getpid();
    std::string error;
    GotRedirect redirect{pin};
    CHECK(redirect.install("getpid", reinterpret_cast<void *>(&replacement), error));
    CHECK(call_getpid() == replacement());
    void **slot = redirect.slot_for_testing();
    CHECK(slot != nullptr);
    if (slot) {
        redirect.reset();
        CHECK(call_getpid() == original);
        void *canonical = __atomic_load_n(slot, __ATOMIC_SEQ_CST);
        CHECK(set_slot(slot, reinterpret_cast<void *>(&other)));
        CHECK(!redirect.install("getpid", reinterpret_cast<void *>(&replacement), error));
        CHECK(call_getpid() == other());
        CHECK(set_slot(slot, canonical));
        CHECK(redirect.install("getpid", reinterpret_cast<void *>(&replacement), error));
        CHECK(set_slot(slot, reinterpret_cast<void *>(&other)));
        redirect.reset();
        CHECK(pins == 1);
        CHECK(call_getpid() == other());
        CHECK(set_slot(slot, canonical));
    }

    IndexedDrawContract contract;
    CHECK(contract.dispatch(4, 0, 12) == IndexedDrawContract::Decision::Forward);
    contract.active = true;
    contract.vertex_count = 12;
    CHECK(contract.dispatch(4, 0, 12) == IndexedDrawContract::Decision::Elements);
    CHECK(!contract.consumed);
    CHECK(contract.dispatch(4, 0, 13) == IndexedDrawContract::Decision::Suppress);
    CHECK(contract.broken);
    CHECK(contract.dispatch(4, 0, 12) == IndexedDrawContract::Decision::Suppress);
    return g_failures == 0 ? 0 : 1;
}
