#pragma once

#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "window/WindowAnimationFacts.hpp"
#include "window/bubble/BubbleWallpaper.hpp"
#include "window/shared/SheetRenderer.hpp"
#include "window/shared/SolidFill.hpp"

class CHyprAnimationCanvas {
  public:
    using Image = SP<Render::IFramebuffer>;
    using Backdrop = SP<SBubbleWallpaper>;
    using Vertex = SMeshRenderVertex;

    struct Resources {
        CSheetShade shade;
        CSolidFill solid;
        struct WorkspaceSwitch {
            PHLMONITORREF monitor;
            bool active = false;
        };
        std::vector<WorkspaceSwitch> workspace_switches;
    };
    CHyprAnimationCanvas(Render::CRenderContext &ctx, Resources &resources,
        bool blur_mask = false, double mask_rounding = 0, double mask_rounding_power = 2)
        : m_context(&ctx), m_resources(&resources), m_blur_mask(blur_mask),
          m_mask_rounding(mask_rounding), m_mask_rounding_power(mask_rounding_power) {
    }
    [[nodiscard]] hyprimate::animation::PassKind pass() const;
    [[nodiscard]] bool captures_pixels() const;
    [[nodiscard]] static bool exports_output(const Render::CRenderContext &);
    [[nodiscard]] bool workspace_switch() const;
    [[nodiscard]] bool blur_mask() const { return m_blur_mask; }
    [[nodiscard]] double mask_rounding() const { return m_mask_rounding; }
    [[nodiscard]] double mask_rounding_power() const { return m_mask_rounding_power; }
    void allow_direct() { m_allow_direct = true; }
    [[nodiscard]] bool direct_allowed() const { return m_allow_direct; }
    void direct_presented() { m_direct_presented = true; }
    [[nodiscard]] bool presented_directly() const { return m_direct_presented; }
    void set_blur_box(const CBox &box) { m_blur_box = box; }
    [[nodiscard]] const CBox &blur_box() const { return m_blur_box; }
    // True when mesh generators should emit indexed window meshes for this process.
    [[nodiscard]] static bool indexed_meshes();
    bool submit_mesh(const hyprimate::animation::Mesh<Vertex> &mesh,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);
    static hyprimate::Vec2 image_size(const Image &image);
    [[nodiscard]] static bool same_image(const Image &left, const Image &right) {
        return left == right;
    }
    void composite(const hyprimate::Box &box, float alpha = 1.F);
    [[nodiscard]] Backdrop capture_wallpaper(const CBox &source, const CBox &bounds);

    [[nodiscard]] Render::CRenderContext &context() const {
        return *m_context;
    }
    [[nodiscard]] Image &layer() {
        return m_layer;
    }
    [[nodiscard]] const Image &layer() const {
        return m_layer;
    }

  private:
    // Non-null borrows; the render context and port resources outlive this canvas.
    Render::CRenderContext *m_context;
    Resources *m_resources;
    Image m_layer;
    bool m_blur_mask = false;
    double m_mask_rounding = 0;
    double m_mask_rounding_power = 2;
    CBox m_blur_box{};
    bool m_allow_direct = false, m_direct_presented = false;
};
