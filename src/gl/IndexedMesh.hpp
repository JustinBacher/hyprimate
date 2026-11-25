#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>

namespace Render {
    class ITexture;
}

// Indexed textured-mesh drawing on stock Hyprland (no upstream API).
//
// Stock CHyprOpenGLImpl::renderTextureMesh uploads the vertex vector it is given and issues
// glDrawArrays(GL_TRIANGLES, 0, vertices.size()) once per clip/damage rect. We call that stock
// function unchanged, but hand it the UNIQUE vertices, and a redirect of Hyprland's own
// glDrawArrays GOT slot turns exactly those draws into glDrawElements over a plugin-owned element
// buffer. Texture, shader, clip, damage and scissor behaviour is therefore stock by construction.
namespace hyprimate::gl {
    enum class IndexedMeshMode : std::uint8_t {
        Off,  // generators emit expanded triangle lists; no redirect is installed
        On,  // generators emit unique vertices + indices
        Compare,  // On, plus a per-frame readback diff against the expanded draw
    };

    struct IndexedMeshStats {
        std::uint64_t elements_draws = 0;  // glDrawElements issued by the redirect
        std::uint64_t passthrough_draws = 0;  // glDrawArrays forwarded untouched
        std::uint64_t vertices_expanded = 0;  // vertices a stock upload would have sent
        std::uint64_t vertices_uploaded = 0;  // vertices actually uploaded
        std::uint64_t index_bytes = 0;
        std::uint64_t compares = 0;
        std::uint64_t compare_mismatches = 0;
    };

    // Reads HYPRIMATE_INDEXED_MESH (unset/0/off, 1/on, compare). Cached on first use.
    [[nodiscard]] IndexedMeshMode indexed_mesh_mode() noexcept;

    // Installs the glDrawArrays redirect. Returns false with `error` set when it cannot be
    // installed; generators then keep emitting expanded meshes.
    [[nodiscard]] bool install_indexed_mesh(HANDLE handle, std::string &error);
    // Restores the GOT slot and frees the element buffer (the buffer name is deleted only when a
    // GL context is current; otherwise it is intentionally leaked).
    void uninstall_indexed_mesh() noexcept;
    [[nodiscard]] bool indexed_mesh_available() noexcept;
    // True when generators should emit indexed window meshes: mode is not Off and the
    // redirect is installed.
    [[nodiscard]] bool indexed_meshes_enabled() noexcept;
    [[nodiscard]] IndexedMeshStats indexed_mesh_stats() noexcept;

    // Gathers an indexed mesh into the expanded triangle list stock draws. Used when the redirect
    // is unavailable at draw time and as the compare-mode reference.
    void expand_indexed_mesh(std::span<const SMeshRenderVertex> vertices,
        std::span<const std::uint16_t> indices,
        std::vector<SMeshRenderVertex> &out);

    // Same contract as CHyprOpenGLImpl::renderTextureMesh but with indices into `vertices`.
    // Returns true when the draw was issued (or stock skipped it for empty damage). Returns false
    // without drawing when the redirect is unavailable or the mesh is not indexable (empty,
    // index count not a multiple of 3, nested use); the caller then draws an expanded list.
    // Every index must be < vertices.size().
    [[nodiscard]] bool render_indexed_mesh(Render::CRenderContext &ctx,
        const SP<Render::ITexture> &texture,
        const CBox &box,
        const std::vector<SMeshRenderVertex> &vertices,
        std::span<const std::uint16_t> indices,
        const Render::GL::CHyprOpenGLImpl::STextureRenderData &data);

    // Compare-mode helpers: RGBA8 readback of the currently bound framebuffer.
    struct FramebufferSnapshot {
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
        bool ok = false;
    };
    [[nodiscard]] FramebufferSnapshot capture_bound_framebuffer(int width, int height);
    void record_compare(const FramebufferSnapshot &expanded,
        const FramebufferSnapshot &indexed,
        std::size_t vertices,
        std::size_t indices);
}  // namespace hyprimate::gl
