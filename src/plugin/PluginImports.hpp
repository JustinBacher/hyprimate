#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>

namespace hyprimate {
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
