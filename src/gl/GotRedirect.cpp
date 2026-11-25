#include "gl/GotRedirect.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

namespace hyprimate::gl {
    namespace {
        struct Search {
            const char *symbol;
            void **slot = nullptr;
            const char *error = "main executable or relocation not found";
        };

#if defined(__x86_64__) && __SIZEOF_POINTER__ == 8
        bool contains(const dl_phdr_info &info, std::uintptr_t address, std::size_t size) noexcept {
            for (std::size_t i = 0; i < info.dlpi_phnum; ++i) {
                const auto &ph = info.dlpi_phdr[i];
                if (ph.p_type != PT_LOAD) continue;
                const auto start = static_cast<std::uintptr_t>(info.dlpi_addr + ph.p_vaddr);
                if (address >= start && size <= ph.p_memsz && address - start <= ph.p_memsz - size)
                    return true;
            }
            return false;
        }

        int find_slot(dl_phdr_info *info, std::size_t, void *arg) {
            auto &search = *static_cast<Search *>(arg);
            if (info->dlpi_name && *info->dlpi_name) return 0;
            const Elf64_Ehdr *header = nullptr;
            for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
                const auto &ph = info->dlpi_phdr[i];
                if (ph.p_type == PT_LOAD && (ph.p_flags & PF_R) && ph.p_offset == 0 &&
                    ph.p_filesz >= sizeof(Elf64_Ehdr)) {
                    header = reinterpret_cast<const Elf64_Ehdr *>(info->dlpi_addr + ph.p_vaddr);
                    break;
                }
            }
            if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
                header->e_ident[EI_CLASS] != ELFCLASS64 ||
                header->e_ident[EI_DATA] != ELFDATA2LSB || header->e_machine != EM_X86_64 ||
                (header->e_type != ET_DYN && header->e_type != ET_EXEC)) {
                search.error = "main executable is not ELF64 x86-64";
                return 1;
            }
            const Elf64_Dyn *dynamic = nullptr;
            std::size_t dynamic_count = 0;
            for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
                const auto &ph = info->dlpi_phdr[i];
                const auto address = static_cast<std::uintptr_t>(info->dlpi_addr + ph.p_vaddr);
                if (ph.p_type == PT_DYNAMIC && contains(*info, address, ph.p_memsz)) {
                    dynamic = reinterpret_cast<const Elf64_Dyn *>(address);
                    dynamic_count = ph.p_memsz / sizeof(Elf64_Dyn);
                    break;
                }
            }
            if (!dynamic) return 1;
            const auto address = [&](Elf64_Addr value, std::size_t size) -> std::uintptr_t {
                if (contains(*info, value, size)) return value;
                const auto relocated = static_cast<std::uintptr_t>(info->dlpi_addr + value);
                return contains(*info, relocated, size) ? relocated : 0;
            };
            Elf64_Addr syms = 0, strings = 0, rela = 0, jump = 0;
            std::size_t strsize = 0, relasize = 0, jumpsize = 0;
            std::size_t relaent = sizeof(Elf64_Rela), syment = 0;
            Elf64_Xword pltrel = 0;
            bool unsupported_rel = false;
            bool terminated = false;
            for (std::size_t i = 0; i < dynamic_count; ++i) {
                const auto &d = dynamic[i];
                if (d.d_tag == DT_NULL) { terminated = true; break; }
                switch (d.d_tag) {
                    case DT_SYMTAB: syms = d.d_un.d_ptr; break;
                    case DT_SYMENT: syment = d.d_un.d_val; break;
                    case DT_STRTAB: strings = d.d_un.d_ptr; break;
                    case DT_STRSZ: strsize = d.d_un.d_val; break;
                    case DT_RELA: rela = d.d_un.d_ptr; break;
                    case DT_RELASZ: relasize = d.d_un.d_val; break;
                    case DT_RELAENT: relaent = d.d_un.d_val; break;
                    case DT_REL: unsupported_rel = d.d_un.d_ptr != 0; break;
                    case DT_JMPREL: jump = d.d_un.d_ptr; break;
                    case DT_PLTRELSZ: jumpsize = d.d_un.d_val; break;
                    case DT_PLTREL: pltrel = d.d_un.d_val; break;
                    default: break;
                }
            }
            if (!terminated || !syms || !strings || !strsize || unsupported_rel ||
                relaent != sizeof(Elf64_Rela) || syment != sizeof(Elf64_Sym) ||
                (jumpsize && pltrel != DT_RELA) || relasize % sizeof(Elf64_Rela) ||
                jumpsize % sizeof(Elf64_Rela)) {
                search.error = "unsupported or malformed ELF relocation tables";
                return 1;
            }
            const auto symbase = address(syms, sizeof(Elf64_Sym));
            const auto strbase = address(strings, strsize);
            if (!symbase || !strbase) { search.error = "ELF symbol tables outside executable"; return 1; }
            const auto scan = [&](Elf64_Addr ptr, std::size_t bytes) {
                if (!bytes) return true;
                const auto table = address(ptr, bytes);
                if (!table) return false;
                const auto *entries = reinterpret_cast<const Elf64_Rela *>(table);
                for (std::size_t i = 0; i < bytes / sizeof(Elf64_Rela); ++i) {
                    const auto type = ELF64_R_TYPE(entries[i].r_info);
                    if (type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT) continue;
                    const auto index = ELF64_R_SYM(entries[i].r_info);
                    const auto sym = symbase + index * sizeof(Elf64_Sym);
                    if (sym < symbase || !contains(*info, sym, sizeof(Elf64_Sym))) return false;
                    const auto name = reinterpret_cast<const Elf64_Sym *>(sym)->st_name;
                    if (name >= strsize || !std::memchr(reinterpret_cast<const char *>(strbase) + name, 0, strsize - name))
                        return false;
                    if (std::strcmp(reinterpret_cast<const char *>(strbase) + name, search.symbol)) continue;
                    const auto slot = static_cast<std::uintptr_t>(info->dlpi_addr + entries[i].r_offset);
                    if (!contains(*info, slot, sizeof(void *)) || slot % alignof(void *) || search.slot)
                        return false;
                    search.slot = reinterpret_cast<void **>(slot);
                }
                return true;
            };
            if (!scan(rela, relasize) || !scan(jump, jumpsize)) {
                search.slot = nullptr;
                search.error = "invalid or ambiguous executable relocation";
            }
            return 1;
        }
#endif

        bool protection(void *slot, void *&page, std::size_t &length, int &prot) noexcept {
            const long size = sysconf(_SC_PAGESIZE);
            if (size <= 0) return false;
            const auto addr = reinterpret_cast<std::uintptr_t>(slot);
            const auto start = addr - addr % static_cast<std::uintptr_t>(size);
            page = reinterpret_cast<void *>(start);
            length = static_cast<std::size_t>(size);
            FILE *maps = std::fopen("/proc/self/maps", "r");
            if (!maps) return false;
            char line[1024], mode[5];
            unsigned long begin = 0, end = 0;
            bool found = false;
            while (std::fgets(line, sizeof(line), maps)) {
                if (std::sscanf(line, "%lx-%lx %4s", &begin, &end, mode) == 3 &&
                    start >= begin && start + length <= end && addr + sizeof(void *) <= end) {
                    prot = (mode[0] == 'r' ? PROT_READ : 0) | (mode[1] == 'w' ? PROT_WRITE : 0) |
                        (mode[2] == 'x' ? PROT_EXEC : 0);
                    found = true;
                    break;
                }
            }
            std::fclose(maps);
            return found;
        }

        bool pin_library(void *hook) noexcept {
            Dl_info info{};
            return dladdr(hook, &info) && info.dli_fname &&
                dlopen(info.dli_fname, RTLD_NOW | RTLD_NODELETE) != nullptr;
        }
    }

    GotRedirect::~GotRedirect() { reset(); }

    bool GotRedirect::install(const char *symbol, void *hook, std::string &error, void *expected) {
        if (m_slot || !symbol || !hook) { error = "invalid or already installed redirect"; return false; }
#if !defined(__x86_64__) || __SIZEOF_POINTER__ != 8
        error = "redirect requires ELF64 x86-64";
        return false;
#else
        void *canonical = dlsym(RTLD_DEFAULT, symbol);
        if (!canonical || (expected && expected != canonical)) {
            error = "symbol does not resolve to expected canonical target";
            return false;
        }
        Search search{symbol};
        dl_iterate_phdr(find_slot, &search);
        if (!search.slot) { error = search.error; return false; }
        // Reserve a loader reference before publishing the hook. If NODELETE cannot later be
        // set, retaining this reference still prevents an unsafe dlclose.
        if (!m_pin) {
            Dl_info info{};
            if (!dladdr(hook, &info) || !info.dli_fname ||
                !(m_hold = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD))) {
                error = "cannot retain redirect library";
                return false;
            }
        }
        void *page = nullptr;
        std::size_t length = 0;
        int prot = 0;
        if (!protection(search.slot, page, length, prot) || mprotect(page, length, prot | PROT_WRITE)) {
            if (m_hold) { dlclose(m_hold); m_hold = nullptr; }
            error = "cannot make relocation page writable";
            return false;
        }
        void *previous = canonical;
        const bool patched = __atomic_compare_exchange_n(search.slot, &previous, hook, false,
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        if (patched) {
            m_slot = search.slot;
            m_original = canonical;
            m_hook = hook;
            m_protection = prot;
        }
        const bool protected_again = mprotect(page, length, prot) == 0;
        if (!patched) {
            if (m_hold) { dlclose(m_hold); m_hold = nullptr; }
            error = protected_again ? "relocation is unresolved or owned by another patcher" :
                "cannot restore relocation page protection";
            return false;
        }
        if (!protected_again) {
            error = "cannot restore relocation page protection";
            reset();
            return false;
        }
        return true;
#endif
    }

    void GotRedirect::reset() noexcept {
        if (!m_slot) {
            if (m_hold) { dlclose(m_hold); m_hold = nullptr; }
            return;
        }
        void *page = nullptr;
        std::size_t length = 0;
        int prot = 0;
        bool restored = false;
        if (protection(m_slot, page, length, prot) && !mprotect(page, length, prot | PROT_WRITE)) {
            void *expected = m_hook;
            restored = __atomic_compare_exchange_n(m_slot, &expected, m_original, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            if (mprotect(page, length, m_protection))
                std::fprintf(stderr, "[hyprimate] indexed-mesh: could not restore GOT page protection\n");
        }
        if (!restored) {
            const bool pinned = m_pin ? m_pin(m_hook) : pin_library(m_hook);
            std::fprintf(stderr, "[hyprimate] indexed-mesh: redirect retained; library pin %s\n",
                pinned ? "succeeded" : (m_hold ? "retained by loader reference" : "FAILED"));
            return;
        }
        m_slot = nullptr;
        m_original = nullptr;
        m_hook = nullptr;
        if (m_hold) { dlclose(m_hold); m_hold = nullptr; }
    }
}
