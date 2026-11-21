#pragma once

#include <cstddef>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include "gl/CompositorContext.hpp"


// Shade ramp for sheet lighting: a 256x1 premultiplied texture indexed by triangle shade. Texel 128
// is fully transparent (neutral); lower texels are black shadow, higher texels are white highlight.
// texture() creates GL objects, so call it only while rendering; the texture is built once and
// reused only in its creating compositor context.
class CSheetShade {
  public:
    SP<Render::ITexture> texture();

  private:
    hyprimate::gl::CompositorContextOwner m_owner;
    SP<Render::ITexture> m_texture;
};


// Six vertices covering a unit box with uv equal to position. Draws a full-canvas texture as a
// mesh.
const std::vector<SMeshRenderVertex> &sheet_canvas_quad();

struct SSheetDraw {
    SP<Render::ITexture> window;
    SP<Render::ITexture> shade;  // ramp texture from CSheetShade; null draws unlit
    Vector2D canvas;  // work buffer size in device pixels
    CBox mesh_box;  // box the meshes are normalised to, in canvas pixels
    const std::vector<SMeshRenderVertex> *window_vertices = nullptr;
    const std::vector<SMeshRenderVertex> *shade_vertices = nullptr;
    float alpha = 1.F;
};

// Renders the sheet into a work buffer and returns it, or nullptr when the work buffer or inputs
// are unavailable. Must be called during rendering; it binds and restores the framebuffer and draw
// state itself.
SP<Render::IFramebuffer> render_sheet(Render::CRenderContext &ctx, const SSheetDraw &draw);
