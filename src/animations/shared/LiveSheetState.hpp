#pragma once

#include <memory>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>

#include "animations/shared/SheetFrame.hpp"

// Reused by each sheet style; liquid states do not reserve space for a sheet frame.
struct SSheetBuffers {
    std::unique_ptr<hyprimate::SheetFrame> frame = std::make_unique<hyprimate::SheetFrame>();
    hyprimate::Vec2 captureSize{};
    std::vector<SMeshRenderVertex> window{};
    std::vector<SMeshRenderVertex> shade{};
};
