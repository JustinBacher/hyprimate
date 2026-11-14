#pragma once

#include "animations/shared/SheetFrame.hpp"
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <memory>
#include <vector>

// Reused by each sheet style; liquid states do not reserve space for a sheet frame.
struct SSheetBuffers {
    std::unique_ptr<liquid_glass::SheetFrame> frame = std::make_unique<liquid_glass::SheetFrame>();
    liquid_glass::Vec2 captureSize{};
    std::vector<SMeshRenderVertex> window{};
    std::vector<SMeshRenderVertex> shade{};
};
