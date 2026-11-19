#pragma once

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "WindowAnimationFacts.hpp"
#include "animations/bubble/BubbleWallpaper.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "animations/shared/SolidFill.hpp"

class CHyprAnimationCanvas {
  public:
    using Image = SP<Render::IFramebuffer>;
    using Backdrop = SP<SBubbleWallpaper>;
    using Vertex = SMeshRenderVertex;

    struct Resources {
        CSheetShade shade;
        CSolidFill solid;
    };
    CHyprAnimationCanvas(Render::CRenderContext &ctx, Resources &resources)
        : m_context(&ctx), m_resources(&resources) {}
    [[nodiscard]] hyprimate::animation::PassKind pass() const;
    bool submit_mesh(const hyprimate::animation::Mesh<Vertex> &mesh,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);
    static hyprimate::Vec2 image_size(const Image &image);
    void composite(const hyprimate::Box &box, float alpha = 1.F);

    [[nodiscard]] Render::CRenderContext &context() const { return *m_context; }
    [[nodiscard]] Image &layer() { return m_layer; }
    [[nodiscard]] const Image &layer() const { return m_layer; }
  private:
    // Non-null borrows; the render context and port resources outlive this canvas.
    Render::CRenderContext *m_context;
    Resources *m_resources;
    Image m_layer;

};
