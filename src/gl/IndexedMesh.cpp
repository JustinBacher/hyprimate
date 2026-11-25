#include "gl/IndexedMesh.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

#include <GLES3/gl32.h>
#include <hyprland/src/render/Texture.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/IndexedDrawContract.hpp"

namespace hyprimate::gl {
    namespace {
        static_assert(sizeof(SMeshRenderVertex) == 16);
        using DrawArraysFn = void (*)(GLenum, GLint, GLsizei);

        struct Pending {
            IndexedDrawContract contract;
            GLuint buffer = 0;
            GLsizei index_count = 0;
        };
        // Hyprland renders and unloads on its event-loop thread; TLS prevents unrelated threads'
        // stock draws from inheriting the pending indexed submission.
        thread_local Pending t_pending;
        DrawArraysFn g_original = nullptr;
        bool g_installed = false;
        bool g_contract_broken = false;
        GLuint g_elements = 0;
        CompositorContextOwner g_owner;

#ifdef HYPRIMATE_DIAGNOSTICS
        std::uint64_t g_elements_draws = 0, g_passthrough_draws = 0;
        std::uint64_t g_vertices_expanded = 0, g_vertices_uploaded = 0, g_index_bytes = 0;
        std::uint64_t g_compares = 0, g_compare_mismatches = 0, g_readback_failures = 0;
        bool selftest_enabled() noexcept {
            static const bool enabled = [] {
                const char *value = std::getenv("HYPRIMATE_INDEXED_MESH_SELFTEST");
                return value && std::strcmp(value, "1") == 0;
            }();
            return enabled;
        }
#endif

        void hooked_draw_arrays(GLenum mode, GLint first, GLsizei count) {
            auto &pending = t_pending;
            switch (pending.contract.dispatch(mode, first, count)) {
                case IndexedDrawContract::Decision::Suppress:
                    g_contract_broken = true;
                    return;
                case IndexedDrawContract::Decision::Elements: {
                    GLint previous = 0;
                    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pending.buffer);
                    glDrawElements(GL_TRIANGLES, pending.index_count, GL_UNSIGNED_SHORT, nullptr);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previous));
                    pending.contract.consumed = true;
#ifdef HYPRIMATE_DIAGNOSTICS
                    ++g_elements_draws;
#endif
                    return;
                }
                case IndexedDrawContract::Decision::Forward:
#ifdef HYPRIMATE_DIAGNOSTICS
                    ++g_passthrough_draws;
#endif
                    g_original(mode, first, count);
            }
        }

        bool upload_indices(std::span<const std::uint16_t> indices, Pending &out) {
            if (!g_owner.ensure_current()) {
                // A replaced backend/context does not own the old name. Abandon it.
                g_elements = 0;
                g_owner.reset();
                if (!g_owner.ensure_current()) return false;
            }
            if (!g_elements) glGenBuffers(1, &g_elements);
            if (!g_elements) return false;
            GLint previous = 0;
            glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &previous);
            glBindBuffer(GL_COPY_WRITE_BUFFER, g_elements);
            const auto bytes = static_cast<GLsizeiptr>(indices.size_bytes());
            glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_COPY_WRITE_BUFFER, 0, bytes, indices.data());
            glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<GLuint>(previous));
            out.buffer = g_elements;
            return true;
        }
    }  // namespace

    IndexedMeshMode indexed_mesh_mode() noexcept {
        static const IndexedMeshMode mode = [] {
            const char *value = std::getenv("HYPRIMATE_INDEXED_MESH");
            if (value && (!std::strcmp(value, "off") || !std::strcmp(value, "0")))
                return IndexedMeshMode::Off;
#ifdef HYPRIMATE_DIAGNOSTICS
            if (value && !std::strcmp(value, "compare")) return IndexedMeshMode::Compare;
#endif
            return IndexedMeshMode::On;
        }();
        return mode;
    }

    bool IndexedMeshRedirect::install(std::string &error) {
        g_original = reinterpret_cast<DrawArraysFn>(dlsym(RTLD_DEFAULT, "glDrawArrays"));
        if (!g_original) {
            error = "glDrawArrays could not be resolved";
            return false;
        }
        if (!m_redirect.install("glDrawArrays", reinterpret_cast<void *>(&hooked_draw_arrays),
                error, reinterpret_cast<void *>(g_original))) return false;
        g_installed = true;
        return true;
    }

    IndexedMeshRedirect::~IndexedMeshRedirect() {
        g_installed = false;
        m_redirect.reset();
#ifdef HYPRIMATE_DIAGNOSTICS
        std::fprintf(stderr,
            "[hyprimate] indexed-mesh: uninstall; elements_draws=%llu passthrough=%llu uploaded=%llu "
            "expanded=%llu index_bytes=%llu compares=%llu mismatches=%llu bytes_indexed=%llu "
            "bytes_expanded=%llu contract_broken=%d selftest=%d readback_failures=%llu\n",
            static_cast<unsigned long long>(g_elements_draws),
            static_cast<unsigned long long>(g_passthrough_draws),
            static_cast<unsigned long long>(g_vertices_uploaded),
            static_cast<unsigned long long>(g_vertices_expanded),
            static_cast<unsigned long long>(g_index_bytes),
            static_cast<unsigned long long>(g_compares),
            static_cast<unsigned long long>(g_compare_mismatches),
            static_cast<unsigned long long>(g_vertices_uploaded * sizeof(SMeshRenderVertex) + g_index_bytes),
            static_cast<unsigned long long>(g_vertices_expanded * sizeof(SMeshRenderVertex)),
            g_contract_broken ? 1 : 0, selftest_enabled() ? 1 : 0,
            static_cast<unsigned long long>(g_readback_failures));
#endif
        if (g_elements) {
            const ScopedCompositorContext context{g_owner};
            if (context) glDeleteBuffers(1, &g_elements);
            g_elements = 0;
            g_owner.reset();
        }
        // g_original stays live if another patcher retained our hook. GotRedirect pins the DSO
        // when it cannot prove that no such chained call can reach it.
    }

    bool indexed_meshes_enabled() noexcept {
        return g_installed && !g_contract_broken;
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
        if (!indexed_meshes_enabled() || t_pending.contract.active || vertices.empty() || indices.empty() ||
            indices.size() % 3 != 0 || vertices.size() > 0xFFFFU ||
            indices.size() > static_cast<std::size_t>(INT32_MAX)) return false;
        Pending pending;
        pending.contract.active = true;
        pending.contract.vertex_count = static_cast<int>(vertices.size());
        pending.index_count = static_cast<GLsizei>(indices.size());
#ifdef HYPRIMATE_DIAGNOSTICS
        std::vector<std::uint16_t> damaged;
        if (indexed_mesh_mode() == IndexedMeshMode::Compare && selftest_enabled() && indices.size() > 3) {
            damaged.assign(indices.begin(), indices.end());
            const auto other = std::find_if(damaged.rbegin(), damaged.rend(), [&](std::uint16_t value) {
                return value != damaged[0] && value != damaged[1] && value != damaged[2];
            });
            if (other != damaged.rend()) std::iter_swap(damaged.begin(), other);
            indices = damaged;
        }
#endif
        if (!upload_indices(indices, pending)) return false;
        t_pending = pending;
        struct ResetPending {
            ~ResetPending() { t_pending = {}; }
        } reset_pending;
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, texture, box, vertices, data);
        const bool consumed = t_pending.contract.consumed && !t_pending.contract.broken;
#ifdef HYPRIMATE_DIAGNOSTICS
        if (consumed) {
            g_vertices_uploaded += vertices.size();
            g_vertices_expanded += indices.size();
            g_index_bytes += indices.size_bytes();
        }
#endif
        return consumed;
    }

#ifdef HYPRIMATE_DIAGNOSTICS
    FramebufferSnapshot capture_bound_framebuffer(int width, int height) {
        FramebufferSnapshot snapshot;
        if (width <= 0 || height <= 0) return snapshot;
        // A lost context may report errors indefinitely; never hang establishing a baseline.
        int remaining_errors = 16;
        while (remaining_errors > 0 && glGetError() != GL_NO_ERROR) --remaining_errors;
        if (remaining_errors == 0) return snapshot;
        GLint draw = 0, read = 0, pack = 0, alignment = 0, row = 0, skip_rows = 0, skip_pixels = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
        if (pack != 0) return snapshot;
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &row);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
        if (glGetError() != GL_NO_ERROR) return snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(draw));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, snapshot.pixels.data());
        snapshot.ok = glGetError() == GL_NO_ERROR;
        glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, row);
        glPixelStorei(GL_PACK_SKIP_ROWS, skip_rows);
        glPixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        if (glGetError() != GL_NO_ERROR) snapshot.ok = false;
        return snapshot;
    }

    void record_compare(const FramebufferSnapshot &expanded,
        const FramebufferSnapshot &indexed,
        std::size_t vertices,
        std::size_t indices) {
        const auto N = ++g_compares;
        if (!expanded.ok || !indexed.ok || expanded.pixels.size() != indexed.pixels.size()) {
            ++g_compare_mismatches;
            ++g_readback_failures;
            std::fprintf(stderr, "[hyprimate] indexed-mesh compare: readback unavailable (MISMATCH)\n");
            return;
        }
        std::size_t differing = 0;
        int max_delta = 0;
        for (std::size_t i = 0; i < expanded.pixels.size(); ++i) {
            const int DELTA = std::abs(int{expanded.pixels[i]} - int{indexed.pixels[i]});
            if (DELTA != 0) ++differing;
            max_delta = std::max(max_delta, DELTA);
        }
        if (differing != 0) ++g_compare_mismatches;
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
#endif
}  // namespace hyprimate::gl
