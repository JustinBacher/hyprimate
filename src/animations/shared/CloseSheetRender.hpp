#pragma once

#include "../../CloseStyleAdapter.hpp"
#include "SheetClosePass.hpp"

namespace close_style {
    inline void fill_sheet(SSheetBuffers &sheet, Record &record) {
        const CBox LOGICAL = record.source_device.copy().scale(1.0 / record.scale);
        CBox extents = sheet_extents(*sheet.frame, LOGICAL);
        extents.expand(16.0);
        record.extents_device = extents.copy().scale(record.scale);
        fill_sheet_meshes(*sheet.frame,
            record.source_device,
            record.extents_device,
            hyprimate::Vec2{.x = 0.0, .y = 0.0},
            hyprimate::Vec2{.x = record.snapshot_size.x, .y = record.snapshot_size.y},
            sheet.window,
            sheet.shade);
    }

    inline void render_sheet(const SSheetBuffers &sheet,
        bool shaded,
        double opacity,
        const Record &record,
        Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<Render::ITexture> &tex,
        const SP<CPaperShade> &shade) {
        const float ALPHA = record.source_alpha * static_cast<float>(opacity);
        if (ALPHA <= 0.F) {
            return;
        }
        CPaperClosePassElement::SData data;
        data.tex = tex;
        data.shade = shaded && shade ? shade->texture() : nullptr;
        data.box = project(record.extents_device, record.render_box, record.snapshot_size);
        data.vertices = &sheet.window;
        data.shade_vertices = &sheet.shade;
        data.alpha = ALPHA;
        data.scale = record.scale;
        data.damage = CRegion{
            CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y}.intersection(
                data.box)};
        Render::IHyprRenderer::addPassElement(
            ctx, makeUnique<CPaperClosePassElement>(std::move(data)));
    }
}  // namespace close_style
