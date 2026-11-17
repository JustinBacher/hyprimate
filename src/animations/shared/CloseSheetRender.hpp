#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "SheetClosePass.hpp"

namespace close_style {
    inline void fillSheet(SSheetBuffers &sheet, Record &record) {
        const CBox LOGICAL = record.sourceDevice.copy().scale(1.0 / record.scale);
        CBox extents = sheetExtents(*sheet.frame, LOGICAL);
        extents.expand(16.0);
        record.extentsDevice = extents.copy().scale(record.scale);
        fillSheetMeshes(*sheet.frame,
            record.sourceDevice,
            record.extentsDevice,
            hyprimate::Vec2{.x = 0.0, .y = 0.0},
            hyprimate::Vec2{.x = record.snapshotSize.x, .y = record.snapshotSize.y},
            sheet.window,
            sheet.shade);
    }

    inline void renderSheet(const SSheetBuffers &sheet,
        bool shaded,
        double opacity,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CPaperShade> &shade) {
        const float ALPHA = record.sourceAlpha * static_cast<float>(opacity);
        if (ALPHA <= 0.F) {
            return;
        }
        CPaperClosePassElement::SData data;
        data.tex = tex;
        data.shade = shaded && shade ? shade->texture() : nullptr;
        data.box = project(record.extentsDevice, record.renderBox, record.snapshotSize);
        data.vertices = &sheet.window;
        data.shadeVertices = &sheet.shade;
        data.alpha = ALPHA;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CPaperClosePassElement>(std::move(data)));
    }
}  // namespace close_style
