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
        : context(ctx), m_resources(resources) {}
    [[nodiscard]] hyprimate::animation::PassKind pass() const;
    bool submit_mesh(const hyprimate::animation::Mesh<Vertex> &mesh,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);
    static hyprimate::Vec2 image_size(const Image &image);
    void composite(const hyprimate::Box &box, float alpha = 1.F);

    Render::CRenderContext &context;
    Image layer;
  private:
    Resources &m_resources;

};
