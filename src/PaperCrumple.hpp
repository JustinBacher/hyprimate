#pragma once

#include "EdgeTransition.hpp"
#include "SheetFacets.hpp"

#include <cstdint>

namespace liquid_glass {

struct PaperParams {
    double duration = 0.5, crumpliness = 0.6, shading = 0.7, variation = 0.5;
    bool   operator==(const PaperParams &) const = default;
};

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
