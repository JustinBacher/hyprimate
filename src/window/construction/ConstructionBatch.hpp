#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hyprimate {

enum class ConstructionSurface : std::uint32_t { Solid, Client, Atlas, Shadow };

struct ConstructionQuad {
    std::array<float, 3> origin, u, v;  // Capture-local logical pixels, positive z toward viewer.
    std::array<float, 4> uv;            // xywh; logical pixels for Client, normalized for Atlas.
    std::array<unsigned char, 4> tint;  // Premultiplied RGBA.
    ConstructionSurface surface;
};
static_assert(std::is_trivially_copyable_v<ConstructionQuad> && sizeof(ConstructionQuad) <= 64);

struct ConstructionBatch {
    std::array<ConstructionQuad, 512> quads;
    std::size_t count = 0;
    bool overflow = false;

    void add(const ConstructionQuad &quad) noexcept {
        if (count == quads.size()) {
            overflow = true;
            return;
        }
        quads[count++] = quad;
    }
};

}  // namespace hyprimate
