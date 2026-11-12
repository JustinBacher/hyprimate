#pragma once

#include "EdgeTransition.hpp"
#include "LiquidMesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace liquid_glass {

struct PaperParams {
    double duration = 0.5, crumpliness = 0.6, shading = 0.7, variation = 0.5;
    bool   operator==(const PaperParams &) const = default;
};

inline constexpr int         PAPER_CELLS = 56;
inline constexpr std::size_t PAPER_TRIANGLES = std::size_t(PAPER_CELLS) * PAPER_CELLS * 2; // 6272
inline constexpr std::size_t PAPER_GRID_VERTICES = std::size_t(PAPER_CELLS + 1) * (PAPER_CELLS + 1);

struct PaperFacet {
    std::array<Vec2, 3>
        position{};             // displayed, relative to the rest capture centre, in units of the rest size (x/W, y/H)
    std::array<Vec2, 3>   uv{}; // [0,1] over the rest capture
    std::array<double, 3> shade{}; // per corner, [-1,1]; <0 darker, >0 lighter, 0 unlit
};
using PaperFacets = std::array<PaperFacet, PAPER_TRIANGLES>;

// Logical-pixel crumple state; no compositor or graphics dependency.
class PaperCrumple {
  public:
    void   begin(Transition kind, const PaperParams &params, std::uint64_t seed) noexcept;
    bool   advance(double elapsedSeconds) noexcept; // returns active()
    void   reset() noexcept;                        // -> Idle, s = 0
    bool   active() const noexcept;                 // Opening or Closing
    bool   closed() const noexcept;                 // Closed
    double opacity() const noexcept;
    bool   shaded() const noexcept;                            // params.shading > 0
    void   facets(Vec2 size, PaperFacets &out) const noexcept; // back-to-front

  private:
    enum class Phase { Idle, Opening, Closing, Closed };

    // Draws the seed-dependent choices (take and mirroring) from the current params.
    void rollShape(std::uint64_t seed) noexcept;
    // Picks the take for this window's aspect on first use; later calls keep it so playback never switches takes.
    void chooseTake(double aspect) const noexcept;

    Phase       m_phase = Phase::Idle;
    double      m_s = 0;    // 0 = flat window, 1 = gone
    double      m_rate = 0; // 1 / duration
    PaperParams m_params{};
    double      m_takePick = 0; // [0,1) chooses among the takes baked for the window's aspect
    bool        m_mirrorX = false;
    bool        m_mirrorY = false;

    mutable int  m_take = -1;         // chosen take, -1 until the first facets() call
    mutable bool m_transpose = false; // portrait window: the landscape take is played with its axes swapped
};

} // namespace liquid_glass
