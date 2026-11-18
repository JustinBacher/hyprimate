#pragma once

#include <cstddef>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "animations/shared/SheetFrame.hpp"

// Vertices per sheet mesh: three per triangle, SHEET_TRIANGLES triangles.
inline constexpr std::size_t SHEET_VERTEX_COUNT = hyprimate::SHEET_TRIANGLES * 3;

// Shade ramp for sheet lighting: a 256x1 premultiplied texture indexed by triangle shade. Texel 128
// is fully transparent (neutral); lower texels are black shadow, higher texels are white highlight.
// texture() creates GL objects, so call it only while rendering; the texture is built once and
// reused.
class CSheetShade {
  public:
    SP<Render::ITexture> texture();

  private:
    SP<Render::ITexture> m_texture;
};

// Logical box that bounds every displayed sheet vertex. `logical_box` is the rest window box in
// logical pixels; the result is in the same space. Width and height are at least 1.
CBox sheet_extents(const hyprimate::SheetFrame &frame, const CBox &logical_box);

// Fills the window mesh (triangles in frame.order, uv into the source texture) and the shade mesh
// (same positions, uv into the shade ramp). `source_device` is the rest window box in device pixels
// and `output_device` is the output box in device pixels. Positions are normalised to the rest
// size, so no scale is needed. Both vectors are resized to SHEET_VERTEX_COUNT only when their size
// differs.
void fill_sheet_meshes(const hyprimate::SheetFrame &frame,
    const CBox &source_device,
    const CBox &output_device,
    hyprimate::Vec2 texture_origin,
    hyprimate::Vec2 texture_size,
    std::vector<SMeshRenderVertex> &window,
    std::vector<SMeshRenderVertex> &shade);

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
