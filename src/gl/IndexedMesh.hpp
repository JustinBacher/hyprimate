#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "gl/GotRedirect.hpp"

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

    // Owns the process-wide redirect and its compositor-context element buffer.
    class IndexedMeshRedirect {
      public:
        ~IndexedMeshRedirect();
        [[nodiscard]] bool install(std::string &error);
      private:
        GotRedirect m_redirect;
    };

    // Unset/on/1 enables indexed meshes; off/0 disables them. Compare requires diagnostics.
    [[nodiscard]] IndexedMeshMode indexed_mesh_mode() noexcept;
    [[nodiscard]] bool indexed_meshes_enabled() noexcept;

    // Gathers an indexed mesh into the expanded triangle list stock draws. Used when the redirect
    // is unavailable at draw time and as the compare-mode reference.
    void expand_indexed_mesh(std::span<const SMeshRenderVertex> vertices,
        std::span<const std::uint16_t> indices,
        std::vector<SMeshRenderVertex> &out);

    // Same contract as CHyprOpenGLImpl::renderTextureMesh but with indices into `vertices`.
    // Returns true only if the redirect issued at least one indexed draw. Otherwise the caller
    // clears the target and replays an expanded list.
    // Every index must be < vertices.size().
    [[nodiscard]] bool render_indexed_mesh(Render::CRenderContext &ctx,
        const SP<Render::ITexture> &texture,
        const CBox &box,
        const std::vector<SMeshRenderVertex> &vertices,
        std::span<const std::uint16_t> indices,
        const Render::GL::CHyprOpenGLImpl::STextureRenderData &data);

#ifdef HYPRIMATE_DIAGNOSTICS
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
#endif
}  // namespace hyprimate::gl
