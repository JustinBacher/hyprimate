#include "gl/IndexedMesh.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <GLES3/gl32.h>
#include <hyprland/src/render/Texture.hpp>

namespace hyprimate::gl {
    namespace {
        using DrawArraysFn = void (*)(GLenum, GLint, GLsizei);

        // What the next stock glDrawArrays(GL_TRIANGLES, 0, vertex_count) on this thread becomes.
        struct Pending {
            bool active = false;
            GLuint buffer = 0;
            GLsizei vertex_count = 0;
            GLsizei index_count = 0;
            GLenum index_type = GL_UNSIGNED_SHORT;
            bool consumed = false;
        };

        thread_local Pending t_pending;

        // Hyprland's own GOT slot for glDrawArrays (BIND_NOW + RELRO executable) and the libGLESv2
        // entry it held. Patching the slot, not the shared stub, leaves every other caller in the
        // process (this plugin, other plugins) on the real function, and needs no instruction
        // relocation: the glvnd stub begins with RIP-relative loads the host's inline hooker
        // rejects.
        void **g_slot = nullptr;
        DrawArraysFn g_original = nullptr;
        GLuint g_elements = 0;

        std::atomic<std::uint64_t> g_elements_draws{0};
        std::atomic<std::uint64_t> g_passthrough_draws{0};
        std::atomic<std::uint64_t> g_vertices_expanded{0};
        std::atomic<std::uint64_t> g_vertices_uploaded{0};
        std::atomic<std::uint64_t> g_index_bytes{0};
        std::atomic<std::uint64_t> g_compares{0};
        std::atomic<std::uint64_t> g_compare_mismatches{0};

        void hooked_draw_arrays(GLenum mode, GLint first, GLsizei count) {
            const auto &p = t_pending;
            if (p.active && mode == GL_TRIANGLES && first == 0 && count == p.vertex_count) {
                // Stock bound the shader's VAO just before this draw. GL_ELEMENT_ARRAY_BUFFER is
                // VAO state, so detach it again to leave the shared VAO as stock left it.
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.buffer);
                glDrawElements(GL_TRIANGLES, p.index_count, p.index_type, nullptr);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                t_pending.consumed = true;
                g_elements_draws.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            g_passthrough_draws.fetch_add(1, std::memory_order_relaxed);
            g_original(mode, first, count);
        }

        void upload_indices(std::span<const std::uint16_t> indices, Pending &out) {
            if (g_elements == 0) {
                glGenBuffers(1, &g_elements);
            }
            // COPY_WRITE, not ELEMENT_ARRAY: whichever VAO is bound right now must not capture it.
            glBindBuffer(GL_COPY_WRITE_BUFFER, g_elements);
            const auto BYTES = static_cast<GLsizeiptr>(indices.size_bytes());
            // Orphan, then fill: the previous draw may still be reading the old block.
            glBufferData(GL_COPY_WRITE_BUFFER, BYTES, nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_COPY_WRITE_BUFFER, 0, BYTES, indices.data());
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            out.index_type = GL_UNSIGNED_SHORT;
            out.buffer = g_elements;
            g_index_bytes.fetch_add(static_cast<std::uint64_t>(BYTES), std::memory_order_relaxed);
        }
    }  // namespace

    IndexedMeshMode indexed_mesh_mode() noexcept {
        static const IndexedMeshMode MODE = [] {
            const char *value = std::getenv("HYPRIMATE_INDEXED_MESH");
            if (value == nullptr) return IndexedMeshMode::Off;
            if (std::strcmp(value, "compare") == 0) return IndexedMeshMode::Compare;
            if (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0) {
                return IndexedMeshMode::On;
            }
            return IndexedMeshMode::Off;
        }();
        return MODE;
    }

    namespace {
        struct SlotSearch {
            const char *symbol;
            void **slot;
        };

        int find_slot(dl_phdr_info *info, std::size_t, void *arg) {
            auto *search = static_cast<SlotSearch *>(arg);
            // The first object reported is the main executable (empty name).
            if (info->dlpi_name != nullptr && info->dlpi_name[0] != '\0') return 1;
            const ElfW(Dyn) *dyn = nullptr;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
                    dyn = reinterpret_cast<const ElfW(Dyn) *>(
                        info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
                }
            }
            if (dyn == nullptr) return 1;
            const auto ABSOLUTE = [&](ElfW(Addr) ptr) {
                // glibc relocates these in place on most targets; accept either form.
                return ptr >= info->dlpi_addr ? ptr : ptr + info->dlpi_addr;
            };
            const ElfW(Sym) *symtab = nullptr;
            const char *strtab = nullptr;
            const ElfW(Rela) *rela = nullptr;
            const ElfW(Rela) *jmprel = nullptr;
            std::size_t rela_size = 0;
            std::size_t jmprel_size = 0;
            for (; dyn->d_tag != DT_NULL; ++dyn) {
                switch (dyn->d_tag) {
                    case DT_SYMTAB: symtab = reinterpret_cast<const ElfW(Sym) *>(ABSOLUTE(dyn->d_un.d_ptr)); break;
                    case DT_STRTAB: strtab = reinterpret_cast<const char *>(ABSOLUTE(dyn->d_un.d_ptr)); break;
                    case DT_RELA: rela = reinterpret_cast<const ElfW(Rela) *>(ABSOLUTE(dyn->d_un.d_ptr)); break;
                    case DT_RELASZ: rela_size = dyn->d_un.d_val; break;
                    case DT_JMPREL: jmprel = reinterpret_cast<const ElfW(Rela) *>(ABSOLUTE(dyn->d_un.d_ptr)); break;
                    case DT_PLTRELSZ: jmprel_size = dyn->d_un.d_val; break;
                    default: break;
                }
            }
            if (symtab == nullptr || strtab == nullptr) return 1;
            const auto SCAN = [&](const ElfW(Rela) *table, std::size_t bytes) {
                for (std::size_t i = 0; table != nullptr && i < bytes / sizeof(ElfW(Rela)); ++i) {
                    const auto TYPE = ELF64_R_TYPE(table[i].r_info);
                    if (TYPE != R_X86_64_GLOB_DAT && TYPE != R_X86_64_JUMP_SLOT) continue;
                    const char *name = strtab + symtab[ELF64_R_SYM(table[i].r_info)].st_name;
                    if (std::strcmp(name, search->symbol) == 0) {
                        search->slot = reinterpret_cast<void **>(info->dlpi_addr + table[i].r_offset);
                        return;
                    }
                }
            };
            SCAN(rela, rela_size);
            if (search->slot == nullptr) SCAN(jmprel, jmprel_size);
            return 1;
        }

        // The slot lives in RELRO (read-only after startup). Toggle write access around a store.
        bool store_slot(void **slot, void *value) {
            const auto PAGE = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
            void *page = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(slot) & ~(PAGE - 1));
            if (mprotect(page, PAGE, PROT_READ | PROT_WRITE) != 0) return false;
            __atomic_store_n(slot, value, __ATOMIC_RELEASE);
            mprotect(page, PAGE, PROT_READ);
            return true;
        }
    }  // namespace

    bool install_indexed_mesh(HANDLE, std::string &error) {
        if (g_slot != nullptr) return true;
        SlotSearch search{"glDrawArrays", nullptr};
        dl_iterate_phdr(find_slot, &search);
        if (search.slot == nullptr || *search.slot == nullptr) {
            error = "Hyprland's glDrawArrays GOT slot not found";
            std::fprintf(stderr, "[hyprimate] indexed-mesh: %s\n", error.c_str());
            return false;
        }
        g_original = reinterpret_cast<DrawArraysFn>(*search.slot);
        if (!store_slot(search.slot, reinterpret_cast<void *>(&hooked_draw_arrays))) {
            g_original = nullptr;
            error = "could not make Hyprland's GOT slot writable";
            std::fprintf(stderr, "[hyprimate] indexed-mesh: %s\n", error.c_str());
            return false;
        }
        g_slot = search.slot;
        std::fprintf(stderr,
            "[hyprimate] indexed-mesh: GOT slot %p redirected (original %p)\n",
            static_cast<void *>(g_slot),
            reinterpret_cast<void *>(g_original));
        return true;
    }

    void uninstall_indexed_mesh() noexcept {
        if (g_slot != nullptr) {
            std::fprintf(stderr,
                "[hyprimate] indexed-mesh: uninstall; elements_draws=%llu passthrough=%llu uploaded=%llu "
                "expanded=%llu index_bytes=%llu compares=%llu mismatches=%llu\n",
                static_cast<unsigned long long>(g_elements_draws.load()),
                static_cast<unsigned long long>(g_passthrough_draws.load()),
                static_cast<unsigned long long>(g_vertices_uploaded.load()),
                static_cast<unsigned long long>(g_vertices_expanded.load()),
                static_cast<unsigned long long>(g_index_bytes.load()),
                static_cast<unsigned long long>(g_compares.load()),
                static_cast<unsigned long long>(g_compare_mismatches.load()));
            store_slot(g_slot, reinterpret_cast<void *>(g_original));
            g_slot = nullptr;
            g_original = nullptr;
        }
        // The buffer name is only valid in the compositor context; delete it only if one is
        // current (the runtime destructor runs on the render thread with Hyprland's context).
        if (g_elements != 0 && eglGetCurrentContext() != EGL_NO_CONTEXT) {
            glDeleteBuffers(1, &g_elements);
        }
        g_elements = 0;
    }

    bool indexed_mesh_available() noexcept {
        return g_slot != nullptr;
    }

    IndexedMeshStats indexed_mesh_stats() noexcept {
        return {g_elements_draws.load(),
            g_passthrough_draws.load(),
            g_vertices_expanded.load(),
            g_vertices_uploaded.load(),
            g_index_bytes.load(),
            g_compares.load(),
            g_compare_mismatches.load()};
    }

    bool indexed_meshes_enabled() noexcept {
        return g_slot != nullptr && indexed_mesh_mode() != IndexedMeshMode::Off;
    }

    void expand_indexed_mesh(std::span<const SMeshRenderVertex> vertices,
        std::span<const std::uint16_t> indices,
        std::vector<SMeshRenderVertex> &out) {
        out.resize(indices.size());
        std::ranges::transform(
            indices, out.begin(), [&](std::uint16_t index) { return vertices[index]; });
    }

    bool render_indexed_mesh(Render::CRenderContext &ctx,
        const SP<Render::ITexture> &texture,
        const CBox &box,
        const std::vector<SMeshRenderVertex> &vertices,
        std::span<const std::uint16_t> indices,
        const Render::GL::CHyprOpenGLImpl::STextureRenderData &data) {
        if (g_slot == nullptr || t_pending.active || vertices.empty() || indices.empty() ||
            indices.size() % 3 != 0 || vertices.size() > 0xFFFFU ||
            indices.size() > static_cast<std::size_t>(INT32_MAX)) {
            return false;
        }
        Pending p;
        p.active = true;
        p.vertex_count = static_cast<GLsizei>(vertices.size());
        p.index_count = static_cast<GLsizei>(indices.size());
        upload_indices(indices, p);
        t_pending = p;
        // Stock early-outs on empty damage without drawing; that is a successful no-op.
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, texture, box, vertices, data);
        const bool CONSUMED = t_pending.consumed;
        t_pending = {};
        const bool EMPTY_DAMAGE = data.damage == nullptr || data.damage->empty();
        if (CONSUMED) {
            g_vertices_uploaded.fetch_add(vertices.size(), std::memory_order_relaxed);
            g_vertices_expanded.fetch_add(indices.size(), std::memory_order_relaxed);
        }
        return CONSUMED || EMPTY_DAMAGE;
    }

    FramebufferSnapshot capture_bound_framebuffer(int width, int height) {
        FramebufferSnapshot snapshot;
        if (width <= 0 || height <= 0) return snapshot;
        while (glGetError() != GL_NO_ERROR) {}
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, snapshot.pixels.data());
        snapshot.ok = glGetError() == GL_NO_ERROR;
        return snapshot;
    }

    void record_compare(const FramebufferSnapshot &expanded,
        const FramebufferSnapshot &indexed,
        std::size_t vertices,
        std::size_t indices) {
        if (!expanded.ok || !indexed.ok || expanded.pixels.size() != indexed.pixels.size()) {
            std::fprintf(stderr, "[hyprimate] indexed-mesh compare: readback unavailable\n");
            return;
        }
        std::size_t differing = 0;
        int max_delta = 0;
        for (std::size_t i = 0; i < expanded.pixels.size(); ++i) {
            const int DELTA = std::abs(int{expanded.pixels[i]} - int{indexed.pixels[i]});
            if (DELTA != 0) ++differing;
            max_delta = std::max(max_delta, DELTA);
        }
        const auto N = g_compares.fetch_add(1, std::memory_order_relaxed) + 1;
        if (differing != 0) g_compare_mismatches.fetch_add(1, std::memory_order_relaxed);
        // Log the first few and every 120th so a long session stays readable.
        if (differing != 0 || N <= 3 || N % 120 == 0) {
            std::fprintf(stderr,
                "[hyprimate] indexed-mesh compare #%llu %dx%d: %zu differing channel bytes, max "
                "delta %d; %zu unique vertices + %zu indices vs %zu expanded vertices; %s\n",
                static_cast<unsigned long long>(N),
                expanded.width,
                expanded.height,
                differing,
                max_delta,
                vertices,
                indices,
                indices,
                differing == 0 ? "IDENTICAL" : "MISMATCH");
        }
    }
}  // namespace hyprimate::gl
