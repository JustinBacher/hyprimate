#pragma once

#include <cstddef>

namespace liquid_glass {

// Paper crumples baked offline with Blender's cloth solver (tools/paper_bake) and embedded in the plugin. Each take
// is a flat sheet of PAPER_CELLS + 1 vertices per side crumpling into a ball; frame 0 is the flat sheet. Positions
// are in units of sqrt(W * H) of the take's own sheet, whose width / height is paperTakeAspect(take) >= 1.
int    paperTakeCount() noexcept;
int    paperTakeFrames() noexcept;
double paperTakeAspect(int take) noexcept;
// Position of `vertex` (index j * (PAPER_CELLS + 1) + i) at `frame` of `take`; arguments must be in range.
void   paperTakeVertex(int take, int frame, std::size_t vertex, double out[3]) noexcept;

} // namespace liquid_glass
