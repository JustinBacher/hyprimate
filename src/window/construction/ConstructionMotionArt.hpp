#pragma once

#include "ConstructionBatch.hpp"
#include "shared/Core.hpp"

namespace hyprimate::construction {
namespace studies {
class EdgeStretch;
class IntactTransport;
class PieceRelocation;
}  // namespace studies

// Publish one accepted motion pose. Model points and both boxes are world-logical;
// the batch and projected bounds are capture-local. No active pose (or an invalid
// publication) clears the batch and returns empty bounds for native presentation.
[[nodiscard]] Box emit_motion_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::IntactTransport *carry, const studies::EdgeStretch *stretch) noexcept;
[[nodiscard]] Box emit_crane_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::IntactTransport &crane) noexcept;
[[nodiscard]] Box emit_piece_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::PieceRelocation &pieces) noexcept;
}  // namespace hyprimate::construction
