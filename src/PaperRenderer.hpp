#pragma once

#include "PaperCrumple.hpp"

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <cstddef>
#include <vector>

// Vertices per paper mesh: six per facet cell, three per facet triangle.
inline constexpr std::size_t PAPER_VERTEX_COUNT = liquid_glass::PAPER_TRIANGLES * 3;

// Shade ramp for facet lighting: a 256x1 premultiplied texture indexed by facet shade. Texel 128 is fully transparent
// (neutral); lower texels are black shadow, higher texels are white highlight. texture() creates GL objects, so call it
// only while rendering; the texture is built once and reused.
class CPaperShade {
  public:
    SP<Render::ITexture> texture();

  private:
    SP<Render::ITexture> m_texture;
};

// Logical box that bounds every displayed facet vertex. `logicalBox` is the rest window box in logical pixels; the
// result is in the same space. Width and height are at least 1.
CBox paperExtents(const liquid_glass::PaperFacets &facets, const CBox &logicalBox);

// Fills the window mesh (facet uv into the source texture) and the shade mesh (same positions, uv into the shade ramp).
// `sourceDevice` is the rest window box in device pixels and `outputDevice` is the output box in device pixels.
// Facet positions are normalised to the rest size, so `scale` does not affect the mapping. Both vectors are resized to
// PAPER_VERTEX_COUNT only when their size differs.
void fillPaperMeshes(const liquid_glass::PaperFacets &facets, const CBox &sourceDevice, double scale,
                     const CBox &outputDevice, liquid_glass::Vec2 textureOrigin, liquid_glass::Vec2 textureSize,
                     std::vector<SMeshRenderVertex> &window, std::vector<SMeshRenderVertex> &shade);

// Six vertices covering a unit box with uv equal to position. Draws a full-canvas texture as a mesh.
const std::vector<SMeshRenderVertex> &paperCanvasQuad();

struct SPaperDraw {
    SP<Render::ITexture>                  window;
    SP<Render::ITexture>                  shade;   // ramp texture from CPaperShade; null draws unlit
    Vector2D                              canvas;  // work buffer size in device pixels
    CBox                                  meshBox; // box the meshes are normalised to, in canvas pixels
    const std::vector<SMeshRenderVertex> *windowVertices = nullptr;
    const std::vector<SMeshRenderVertex> *shadeVertices = nullptr;
    float                                 alpha = 1.F;
};

// Renders the paper into a work buffer and returns it, or nullptr when the work buffer or inputs are unavailable.
// Must be called during rendering; it binds and restores the framebuffer and draw state itself.
SP<Render::IFramebuffer> renderPaper(Render::CRenderContext &ctx, const SPaperDraw &d);
