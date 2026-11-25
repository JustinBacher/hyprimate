#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>

namespace hyprimate {

    // Tracks only slots this instance installed. A foreign replacement is an
    // ownership conflict; stop managing that entry rather than chaining through it.
    template <std::size_t N>
    class PluginImportOwnership {
      public:
        void install(const std::array<void **, N> &slots,
            const std::array<void *, N> &replacements,
            const void *generation) noexcept {
            m_slots = slots;
            m_generation = generation;
            m_suspended = false;
            for (std::size_t index = 0; index < N; ++index) {
                if (slots[index] == nullptr) continue;
                m_previous[index] = *slots[index];
                *slots[index] = replacements[index];
                m_owned[index] = true;
            }
        }

        void suspend() noexcept {
            m_suspended = true;
        }

        bool suspended() const noexcept {
            return m_suspended;
        }

        bool observe(const std::array<void **, N> &slots,
            const std::array<void *, N> &replacements,
            const void *generation) noexcept {
            m_suspended = false;
            if (generation != m_generation) {
                bool callback_slots_remain = false;
                for (std::size_t index = 0; index < N; ++index) {
                    callback_slots_remain |= m_owned[index] && slots[index] != nullptr &&
                        slots[index] == m_slots[index] && *slots[index] == replacements[index];
                }
                if (!callback_slots_remain) {
                    clear();
                    return false;
                }
                // The DSO remained mapped across a registry-record change.
                // Keep each predecessor and ownership bit for slots still ours.
                m_generation = generation;
            }
            bool conflict = false;
            for (std::size_t index = 0; index < N; ++index) {
                if (!m_owned[index]) continue;
                if (slots[index] != m_slots[index] || slots[index] == nullptr ||
                    *slots[index] != replacements[index]) {
                    m_owned[index] = false;
                    conflict = true;
                }
            }
            if (!conflict) return false;
            m_conflicted = true;
            if (m_reported) return false;
            m_reported = true;
            return true;
        }

        bool conflicted() const noexcept {
            return m_conflicted;
        }

        void restore(const std::array<void **, N> &slots,
            const std::array<void *, N> &replacements) noexcept {
            for (std::size_t index = 0; index < N; ++index) {
                if (m_owned[index] && slots[index] != nullptr && slots[index] == m_slots[index] &&
                    *slots[index] == replacements[index]) {
                    *slots[index] = m_previous[index];
                }
            }
            clear();
        }

        void clear() noexcept {
            m_slots = {};
            m_previous = {};
            m_owned = {};
            m_conflicted = false;
            m_reported = false;
            m_generation = nullptr;
            m_suspended = false;
        }

      private:
        std::array<void **, N> m_slots{};
        std::array<void *, N> m_previous{};
        std::array<bool, N> m_owned{};
        bool m_conflicted = false;
        bool m_reported = false;
        const void *m_generation = nullptr;
        bool m_suspended = false;
    };
    // Locate this loaded DSO's own writable PLT slots. Never change executable
    // code or relax RELRO protections; unsupported layouts return empty slots.
    template <std::size_t N>
    std::array<void **, N> writable_plugin_imports(
        void *library, const std::array<const char *, N> &names) {
        std::array<void **, N> result{};
#if defined(__x86_64__)
        link_map *map = nullptr;
        if (library == nullptr || dlinfo(library, RTLD_DI_LINKMAP, &map) != 0) return result;
        const ElfW(Sym) *symbols = nullptr;
        const char *strings = nullptr;
        const ElfW(Rela) *relocations = nullptr;
        std::size_t count = 0;
        bool rela = false;
        for (const auto *dynamic = map->l_ld; dynamic->d_tag != DT_NULL; ++dynamic) {
            switch (dynamic->d_tag) {
                case DT_SYMTAB: symbols = reinterpret_cast<const ElfW(Sym) *>(dynamic->d_un.d_ptr); break;
                case DT_STRTAB: strings = reinterpret_cast<const char *>(dynamic->d_un.d_ptr); break;
                case DT_JMPREL: relocations = reinterpret_cast<const ElfW(Rela) *>(dynamic->d_un.d_ptr); break;
                case DT_PLTRELSZ: count = dynamic->d_un.d_val / sizeof(ElfW(Rela)); break;
                case DT_PLTREL: rela = dynamic->d_un.d_val == DT_RELA; break;
                default: break;
            }
        }
        if (!symbols || !strings || !relocations || !rela) return result;
        for (std::size_t index = 0; index < count; ++index) {
            const auto &relocation = relocations[index];
            if (ELF64_R_TYPE(relocation.r_info) != R_X86_64_JUMP_SLOT) continue;
            const auto *name = strings + symbols[ELF64_R_SYM(relocation.r_info)].st_name;
            for (std::size_t item = 0; item < N; ++item) {
                if (std::strcmp(name, names[item]) == 0)
                    result[item] = reinterpret_cast<void **>(map->l_addr + relocation.r_offset);
            }
        }
        struct Layout {
            ElfW(Addr) base;
            std::array<void **, N> *slots;
        } layout{map->l_addr, &result};
        dl_iterate_phdr([](dl_phdr_info *info, std::size_t, void *data) {
            auto &layout = *static_cast<Layout *>(data);
            const auto PAGE = static_cast<ElfW(Addr)>(getpagesize());
            if (info->dlpi_addr != layout.base) return 0;
            for (auto &slot : *layout.slots) {
                const auto address = reinterpret_cast<std::uintptr_t>(slot);
                bool writable = false, relro = false;
                for (std::size_t index = 0; index < info->dlpi_phnum; ++index) {
                    const auto &segment = info->dlpi_phdr[index];
                    if (segment.p_type == PT_GNU_RELRO) {
                        const auto BEGIN = (info->dlpi_addr + segment.p_vaddr) & ~(PAGE - 1);
                        const auto END = (info->dlpi_addr + segment.p_vaddr + segment.p_memsz) & ~(PAGE - 1);
                        relro |= address >= BEGIN && address < END;
                    }
                    const auto begin = info->dlpi_addr + segment.p_vaddr;
                    const auto end = begin + segment.p_memsz;
                    if (address >= begin && address + sizeof(void *) <= end) {
                        writable |= segment.p_type == PT_LOAD && (segment.p_flags & PF_W) != 0;
                    }
                }
                if (!writable || relro) slot = nullptr;
            }
            return 1;
        }, &layout);
#endif
        return result;
    }
}
