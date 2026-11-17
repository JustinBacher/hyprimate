#pragma once

#include "../../AnimationCloseRenderer.hpp"

// Draws a paper or cloth close, borrowing its vertices from the retained record.
class CPaperClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<Render::ITexture> tex;
        SP<Render::ITexture> shade;  // lit tint layer; null draws unlit
        CBox box;  // output box, monitor-local device px
        const std::vector<SMeshRenderVertex> *vertices = nullptr;
        const std::vector<SMeshRenderVertex> *shade_vertices = nullptr;
        float alpha = 1.F;
        double scale = 1.0;
        CRegion damage;
    };

    explicit CPaperClosePassElement(SData data);
    ~CPaperClosePassElement() override = default;

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
