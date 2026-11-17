#include <bit>
#include <cstddef>
#include <cstdint>

#include "animations/paper/PaperTakes.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {

        // Built by tools/pack.py; layout documented there.
        alignas(4) constexpr unsigned char ASSET[] = {
#embed "assets/paper_takes.bin"
        };

        constexpr std::uint32_t u32(std::size_t at) noexcept {
            return static_cast<std::uint32_t>(ASSET[at]) |
                   static_cast<std::uint32_t>(ASSET[at + 1]) << 8 |
                   static_cast<std::uint32_t>(ASSET[at + 2]) << 16 |
                   static_cast<std::uint32_t>(ASSET[at + 3]) << 24;
        }

        constexpr double POSITION_SCALE = 1.0 / 16384.0;
        constexpr int GRID = static_cast<int>(u32(8));
        constexpr int FRAMES = static_cast<int>(u32(12));
        constexpr int TAKES = static_cast<int>(u32(16));
        constexpr std::size_t ASPECTS = 20;
        constexpr std::size_t DATA = ASPECTS + (4 * static_cast<std::size_t>(TAKES));
        constexpr std::size_t VERTICES = static_cast<std::size_t>(GRID) * GRID;
        constexpr std::size_t TAKE_BYTES = static_cast<std::size_t>(FRAMES) * VERTICES * 3 * 2;

        static_assert(ASSET[0] == 'P' && ASSET[1] == 'C' && ASSET[2] == 'R' && ASSET[3] == 'T',
            "not a paper take asset");
        static_assert(u32(4) == 1, "unsupported paper take asset version");
        static_assert(GRID == PAPER_CELLS + 1,
            "paper takes were baked for a different grid; rebake with src/animations/paper/tools");
        static_assert(FRAMES >= 2 && TAKES >= 1, "paper take asset is empty");
        static_assert(sizeof(ASSET) == DATA + (static_cast<std::size_t>(TAKES) * TAKE_BYTES),
            "paper take asset is truncated");

        double i16(std::size_t at) noexcept {
            return static_cast<double>(
                       static_cast<std::int16_t>(static_cast<std::uint16_t>(ASSET[at]) |
                                                 static_cast<std::uint16_t>(ASSET[at + 1]) << 8)) *
                   POSITION_SCALE;
        }

    }  // namespace

    int paperTakeCount() noexcept {
        return TAKES;
    }

    int paperTakeFrames() noexcept {
        return FRAMES;
    }

    double paperTakeAspect(int take) noexcept {
        return std::bit_cast<float>(u32(ASPECTS + (4 * static_cast<std::size_t>(take))));
    }

    void paperTakeVertex(int take, int frame, std::size_t vertex, double out[3]) noexcept {
        const std::size_t OFFSET = DATA + (static_cast<std::size_t>(take) * TAKE_BYTES) +
                                   (((static_cast<std::size_t>(frame) * VERTICES) + vertex) * 6);
        out[0] = i16(OFFSET);
        out[1] = i16(OFFSET + 2);
        out[2] = i16(OFFSET + 4);
    }

}  // namespace hyprimate
