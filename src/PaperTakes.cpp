#include "PaperTakes.hpp"

#include "PaperCrumple.hpp"

#include <bit>
#include <cstdint>

namespace liquid_glass {
namespace {

// Built by tools/paper_bake/pack.py; layout documented there.
alignas(4) constexpr unsigned char ASSET[] = {
#embed "../assets/paper_takes.bin"
};

constexpr std::uint32_t u32(std::size_t at) noexcept {
    return std::uint32_t(ASSET[at]) | std::uint32_t(ASSET[at + 1]) << 8 | std::uint32_t(ASSET[at + 2]) << 16 |
           std::uint32_t(ASSET[at + 3]) << 24;
}

constexpr double      POSITION_SCALE = 1.0 / 16384.0;
constexpr int         GRID = int(u32(8));
constexpr int         FRAMES = int(u32(12));
constexpr int         TAKES = int(u32(16));
constexpr std::size_t ASPECTS = 20;
constexpr std::size_t DATA = ASPECTS + 4 * std::size_t(TAKES);
constexpr std::size_t VERTICES = std::size_t(GRID) * GRID;
constexpr std::size_t TAKE_BYTES = std::size_t(FRAMES) * VERTICES * 3 * 2;

static_assert(ASSET[0] == 'P' && ASSET[1] == 'C' && ASSET[2] == 'R' && ASSET[3] == 'T', "not a paper take asset");
static_assert(u32(4) == 1, "unsupported paper take asset version");
static_assert(GRID == PAPER_CELLS + 1, "paper takes were baked for a different grid; rebake with tools/paper_bake");
static_assert(FRAMES >= 2 && TAKES >= 1, "paper take asset is empty");
static_assert(sizeof(ASSET) == DATA + std::size_t(TAKES) * TAKE_BYTES, "paper take asset is truncated");

double i16(std::size_t at) noexcept {
    return double(std::int16_t(std::uint16_t(ASSET[at]) | std::uint16_t(ASSET[at + 1]) << 8)) * POSITION_SCALE;
}

} // namespace

int paperTakeCount() noexcept { return TAKES; }

int paperTakeFrames() noexcept { return FRAMES; }

double paperTakeAspect(int take) noexcept { return std::bit_cast<float>(u32(ASPECTS + 4 * std::size_t(take))); }

void paperTakeVertex(int take, int frame, std::size_t vertex, double out[3]) noexcept {
    const std::size_t at = DATA + std::size_t(take) * TAKE_BYTES + (std::size_t(frame) * VERTICES + vertex) * 6;
    out[0] = i16(at);
    out[1] = i16(at + 2);
    out[2] = i16(at + 4);
}

} // namespace liquid_glass
