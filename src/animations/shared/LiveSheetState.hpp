#pragma once

#include <memory>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>

#include "animations/shared/SheetMesh.hpp"

// Reused by each sheet style; liquid states do not reserve space for a sheet frame.
struct SSheetBuffers {
    std::unique_ptr<hyprimate::SheetFrame> frame = std::make_unique<hyprimate::SheetFrame>();
    hyprimate::Vec2 capture_size{};
    hyprimate::animation::MeshBuffers<SMeshRenderVertex> buffers;
};
