#include <array>
#include <dlfcn.h>

#include "Check.hpp"
#include "plugin/PluginImports.hpp"

namespace {
    int replacement() { return 19; }
}

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    constexpr std::array NAMES{"hyprimate_import_value", "hyprimate_import_bias"};
    // Reloads may reuse both the library handle and its virtual addresses. Slots
    // must be rediscovered, not borrowed from the unloaded mapping.
    for (int iteration = 0; iteration < 3; ++iteration) {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr);
        if (library == nullptr) return 1;
        const auto CONSUMER = reinterpret_cast<int (*)()>(dlsym(library, "hyprimate_import_consumer"));
        CHECK(CONSUMER != nullptr);
        if (CONSUMER == nullptr) return 1;
        CHECK(CONSUMER() == 10);
        const auto SLOTS = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(SLOTS[0] != nullptr && SLOTS[1] != nullptr);
        if (SLOTS[0] == nullptr || SLOTS[1] == nullptr) return 1;
        void *previous = *SLOTS[0];
        *SLOTS[0] = reinterpret_cast<void *>(&replacement);
        CHECK(CONSUMER() == 22);
        *SLOTS[0] = previous;
        CHECK(CONSUMER() == 10);
        CHECK(dlclose(library) == 0);
    }
    // A full-RELRO build must be refused without relaxing loader protections.
    void *protected_library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    CHECK(protected_library != nullptr);
    if (protected_library == nullptr) return 1;
    const auto PROTECTED = hyprimate::writable_plugin_imports(protected_library, NAMES);
    CHECK(PROTECTED[0] == nullptr && PROTECTED[1] == nullptr);
    const auto CONSUMER = reinterpret_cast<int (*)()>(dlsym(protected_library, "hyprimate_import_consumer"));
    CHECK(CONSUMER != nullptr && CONSUMER() == 10);
    CHECK(dlclose(protected_library) == 0);
    return g_failures == 0 ? 0 : 1;
}
