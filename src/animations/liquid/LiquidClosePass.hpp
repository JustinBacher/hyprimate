#pragma once

#include "../../AnimationCloseRenderer.hpp"
// Draws a deformed closing fadeout. Borrows vertices from its record; records are only removed from
// tick/clear, which run before a frame's render pass begins, so the borrowed pointer is valid for
// the whole frame. The host destroys every pass element when the frame ends, so no element of this
// type survives between frames.
class CLiquidClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<Render::ITexture> tex;
        CBox box;  // output box, monitor-local device px
        const std::vector<SMeshRenderVertex> *vertices = nullptr;
        float alpha = 1.F;
        double scale = 1.0;
        CRegion damage;
        // Edge liquid: drawn before the window mesh, sampling `solid` over `liquid` vertices in
        // `liquid_box`.
        SP<Render::ITexture> solid;
        const std::vector<SMeshRenderVertex> *liquid = nullptr;
        CBox liquid_box;
    };

    explicit CLiquidClosePassElement(SData data);
    ~CLiquidClosePassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override;
    bool needsLiveBlur(Render::CRenderContext &ctx) override;
    bool needsPrecomputeBlur(Render::CRenderContext &ctx) override;
    const char *passName() override;
    ePassElementType type() override;
    std::optional<CBox> boundingBox(Render::CRenderContext &ctx) override;
    CRegion opaqueRegion(Render::CRenderContext &ctx) override;

  private:
    SData m_data;
};
