#include <array>
#include <cstdint>
#include <dlfcn.h>

#include "Check.hpp"
#include "plugin/PluginImports.hpp"

namespace {
    int our_calls = 0;
    int foreign_calls = 0;
    int replacement() { return 19; }
    int owned_render() { ++our_calls; return 19; }
    int owned_apply() { return 3; }
    int foreign_render() { ++foreign_calls; return 31; }
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
        const std::array OWNED{
            reinterpret_cast<void *>(&owned_render), reinterpret_cast<void *>(&owned_apply)};
        const void *original_apply = *SLOTS[1];
        const int prior_foreign_calls = foreign_calls;
        const int load_generation = iteration;
        hyprimate::PluginImportOwnership<2> ownership;
        ownership.install(SLOTS, OWNED, &load_generation);
        CHECK(CONSUMER() == 22);
        *SLOTS[0] = reinterpret_cast<void *>(&foreign_render);
        int conflict_reports = 0;
        if (ownership.observe(SLOTS, OWNED, &load_generation)) ++conflict_reports;
        if (ownership.observe(SLOTS, OWNED, &load_generation)) ++conflict_reports;
        CHECK(conflict_reports == 1);
        CHECK(*SLOTS[0] == reinterpret_cast<void *>(&foreign_render));
        ownership.restore(SLOTS, OWNED);
        CHECK(*SLOTS[0] == reinterpret_cast<void *>(&foreign_render));
        CHECK(*SLOTS[1] == original_apply);
        const int prior_owned_calls = our_calls;
        CHECK(CONSUMER() == 34);
        CHECK(our_calls == prior_owned_calls);
        CHECK(foreign_calls == prior_foreign_calls + 1);
        CHECK(dlclose(library) == 0);
    }
    // Keep the adapter's ownership state across an unload and immediate reload.
    // The loader may recycle both the handle and slot addresses for the new DSO.
    {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr);
        if (library == nullptr) return 1;
        const auto old_slots = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(old_slots[0] != nullptr && old_slots[1] != nullptr);
        if (old_slots[0] == nullptr || old_slots[1] == nullptr) return 1;
        const auto old_handle_address = reinterpret_cast<std::uintptr_t>(library);
        const std::array old_slot_addresses{reinterpret_cast<std::uintptr_t>(old_slots[0]),
            reinterpret_cast<std::uintptr_t>(old_slots[1])};
        const std::array OWNED{
            reinterpret_cast<void *>(&owned_render), reinterpret_cast<void *>(&owned_apply)};
        int old_generation = 0;
        int new_generation = 1;
        hyprimate::PluginImportOwnership<2> ownership;
        ownership.install(old_slots, OWNED, &old_generation);
        CHECK(dlclose(library) == 0);

        library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr);
        if (library == nullptr) return 1;
        const auto slots = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(slots[0] != nullptr && slots[1] != nullptr);
        if (slots[0] == nullptr || slots[1] == nullptr) return 1;
        CHECK(reinterpret_cast<std::uintptr_t>(library) == old_handle_address);
        CHECK(reinterpret_cast<std::uintptr_t>(slots[0]) == old_slot_addresses[0]);
        CHECK(reinterpret_cast<std::uintptr_t>(slots[1]) == old_slot_addresses[1]);
        const auto consumer =
            reinterpret_cast<int (*)()>(dlsym(library, "hyprimate_import_consumer"));
        CHECK(consumer != nullptr);
        if (consumer == nullptr) return 1;

        // A new registry generation adopts fresh imports even when the loader
        // recycles its handle and GOT slot addresses.
        int conflict_reports = 0;
        if (ownership.observe(slots, OWNED, &new_generation)) ++conflict_reports;
        if (!ownership.conflicted() && (*slots[0] != OWNED[0] || *slots[1] != OWNED[1])) {
            ownership.restore(slots, OWNED);
            ownership.install(slots, OWNED, &new_generation);
        }
        CHECK(conflict_reports == 0);
        CHECK(*slots[0] == OWNED[0] && *slots[1] == OWNED[1]);
        const int previous_calls = our_calls;
        CHECK(consumer() == 22);
        CHECK(our_calls == previous_calls + 1);
        ownership.restore(slots, OWNED);
        CHECK(dlclose(library) == 0);
    }
    // A second loader reference keeps the original mapping alive while the
    // registry reference is closed and reopened under a new load token.
    {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        void *keeper = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr && keeper != nullptr);
        if (library == nullptr || keeper == nullptr) return 1;
        const auto slots = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(slots[0] != nullptr && slots[1] != nullptr);
        if (slots[0] == nullptr || slots[1] == nullptr) return 1;
        const std::array originals{*slots[0], *slots[1]};
        const std::array owned{
            reinterpret_cast<void *>(&owned_render), reinterpret_cast<void *>(&owned_apply)};
        int first_generation = 0;
        int second_generation = 1;
        hyprimate::PluginImportOwnership<2> ownership;
        ownership.install(slots, owned, &first_generation);
        CHECK(dlclose(library) == 0);
        ownership.suspend();
        CHECK(ownership.suspended());
        library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr);
        if (library == nullptr) return 1;
        const auto reloaded_slots = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(reloaded_slots == slots);
        CHECK(*reloaded_slots[0] == owned[0] && *reloaded_slots[1] == owned[1]);
        CHECK(!ownership.observe(reloaded_slots, owned, &second_generation));
        CHECK(!ownership.suspended());
        // Current refresh returns when both slots already contain our callbacks.
        ownership.restore(reloaded_slots, owned);
        CHECK(*reloaded_slots[0] == originals[0]);
        CHECK(*reloaded_slots[1] == originals[1]);
        CHECK(dlclose(library) == 0);
        CHECK(dlclose(keeper) == 0);
    }
    // Teardown after unregister resolves a temporary reference to the still-live
    // mapping; the ownership helper restores only currently installed callbacks.
    {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        void *keeper = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(library != nullptr && keeper != nullptr);
        if (library == nullptr || keeper == nullptr) return 1;
        const auto slots = hyprimate::writable_plugin_imports(library, NAMES);
        CHECK(slots[0] != nullptr && slots[1] != nullptr);
        if (slots[0] == nullptr || slots[1] == nullptr) return 1;
        const std::array originals{*slots[0], *slots[1]};
        const std::array owned{
            reinterpret_cast<void *>(&owned_render), reinterpret_cast<void *>(&owned_apply)};
        int generation = 0;
        hyprimate::PluginImportOwnership<2> ownership;
        ownership.install(slots, owned, &generation);
        CHECK(dlclose(library) == 0);
        ownership.suspend();
        void *temporary = dlopen(argv[1], RTLD_LAZY | RTLD_NOLOAD);
        CHECK(temporary != nullptr);
        if (temporary == nullptr) return 1;
        const auto current_slots = hyprimate::writable_plugin_imports(temporary, NAMES);
        CHECK(current_slots == slots);
        ownership.restore(current_slots, owned);
        CHECK(*current_slots[0] == originals[0]);
        CHECK(*current_slots[1] == originals[1]);
        CHECK(dlclose(temporary) == 0);
        CHECK(dlclose(keeper) == 0);
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
