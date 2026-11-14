#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "SheetClosePass.hpp"

namespace close_style {
inline void fillSheet(SSheetBuffers &sheet, Record &record) {
    const CBox logical = record.sourceDevice.copy().scale(1.0 / record.scale);
    CBox extents = sheetExtents(*sheet.frame, logical);
    extents.expand(16.0);
    record.extentsDevice = extents.copy().scale(record.scale);
    fillSheetMeshes(*sheet.frame, record.sourceDevice, record.extentsDevice, liquid_glass::Vec2{0.0, 0.0},
                    liquid_glass::Vec2{record.snapshotSize.x, record.snapshotSize.y}, sheet.window, sheet.shade);
}

inline void renderSheet(SSheetBuffers &sheet, bool shaded, double opacity, Record &record,
                        Render::CRenderContext &ctx, const PHLMONITOR &monitor, const SP<Render::ITexture> &tex,
                        const SP<CPaperShade> &shade) {
    const float alpha = record.sourceAlpha * float(opacity);
    if (alpha <= 0.F)
        return;
    CPaperClosePassElement::SData data;
    data.tex = tex;
    data.shade = shaded && shade ? shade->texture() : nullptr;
    data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
    data.vertices = &sheet.window;
    data.shadeVertices = &sheet.shade;
    data.alpha = alpha;
    data.scale = record.scale;
    data.damage = CRegion{CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(data.box)};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CPaperClosePassElement>(std::move(data)));
}
}
