#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "gl/CompositorContext.hpp"
#include "window/shared/SheetShadeCache.hpp"

// Shade ramp for sheet lighting: a 256x1 premultiplied texture indexed by triangle shade. Texel 128
// is fully transparent (neutral); lower texels are black shadow, higher texels are white highlight.
// texture() creates GL objects, so call it only while rendering; the texture is cached for its
// creating compositor context and rebuilt after a verified replacement.
class CSheetShade {
  public:
    SP<Render::ITexture> texture();

  private:
    hyprimate::gl::SheetShadeCache<Render::ITexture,
        hyprimate::gl::CompositorContextOwner,
        hyprimate::gl::CurrentCompositorContext,
        hyprimate::gl::ScopedCompositorContext> m_cache;
};

// Six vertices covering a unit box with uv equal to position. Draws a full-canvas texture as a
// mesh.
const std::vector<SMeshRenderVertex> &sheet_canvas_quad();

struct SSheetDraw {
    SP<Render::ITexture> window;
    SP<Render::ITexture> shade;  // ramp texture from CSheetShade; null draws unlit
    Vector2D canvas;  // work buffer size in device pixels
    CBox mesh_box;  // box the meshes are normalised to, in canvas pixels
    // With window_indices, window_vertices holds unique vertices and the window layer draws
    // indexed; the shade layer is always an expanded list.
    const std::vector<SMeshRenderVertex> *window_vertices = nullptr;
    const std::vector<std::uint16_t> *window_indices = nullptr;
    const std::vector<SMeshRenderVertex> *shade_vertices = nullptr;
    float alpha = 1.F;
};

// Renders the sheet into a work buffer and returns it, or nullptr when the work buffer or inputs
// are unavailable. Must be called during rendering; it binds and restores the framebuffer and draw
// state itself.
SP<Render::IFramebuffer> render_sheet(Render::CRenderContext &ctx, const SSheetDraw &draw);
