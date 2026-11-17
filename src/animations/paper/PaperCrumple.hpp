#pragma once

#include <cstdint>

#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {

    struct PaperParams {
        double duration = 0.5, crumpliness = 0.6, shading = 0.7, variation = 0.5;
        bool operator==(const PaperParams &) const = default;
    };

    // Logical-pixel crumple state; no compositor or graphics dependency.
    class PaperCrumple {
      public:
        void begin(Transition kind, const PaperParams &params, std::uint64_t seed) noexcept;
        bool advance(double elapsed_seconds) noexcept;  // returns active()
        void reset() noexcept;  // -> Idle, s = 0
        bool active() const noexcept;  // Opening or Closing
        bool closed() const noexcept;  // Closed
        double opacity() const noexcept;
        bool shaded() const noexcept;  // params.shading > 0
        void frame(Vec2 size, SheetFrame &out) const noexcept;  // back-to-front order in out.order

      private:
        enum class Phase : std::uint8_t { Idle, Opening, Closing, Closed };

        // Draws the seed-dependent choices (take and mirroring) from the current params.
        void roll_shape(std::uint64_t seed) noexcept;
        // Picks the take for this window's aspect on first use; later calls keep it so playback
        // never switches takes.
        void choose_take(double aspect) const noexcept;

        Phase m_phase = Phase::Idle;
        double m_s = 0;  // 0 = flat window, 1 = gone
        double m_rate = 0;  // 1 / duration
        PaperParams m_params{};
        double m_take_pick = 0;  // [0,1) chooses among the takes baked for the window's aspect
        bool m_mirror_x = false;
        bool m_mirror_y = false;

        mutable int m_take = -1;  // chosen take, -1 until the first frame() call
        mutable bool m_transpose =
            false;  // portrait window: the landscape take is played with its axes swapped
    };

}  // namespace hyprimate
