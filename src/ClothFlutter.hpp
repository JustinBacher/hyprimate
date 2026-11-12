#pragma once

#include "EdgeTransition.hpp"
#include "SheetFacets.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace liquid_glass {

struct ClothParams {
    double duration = 1.0, flutter = 0.5, shading = 0.7, variation = 0.5;
    bool   operator==(const ClothParams &) const = default;
};

// Keeps Jolt's process-wide state (allocator hooks, factory, type registry) alive. Construct one before any cloth
// begins and destroy it after every ClothFlutter is gone; at most one may exist at a time. Without one, begin()
// leaves the cloth idle.
class ClothPhysics {
  public:
    ClothPhysics();
    ~ClothPhysics();
    ClothPhysics(const ClothPhysics &) = delete;
    ClothPhysics &operator=(const ClothPhysics &) = delete;
};

// The window as a cloth hanging from its top edge. Opening drops it from a roll at the top: it unrolls downward,
// flutters and settles flat. Closing winds it back up from the bottom faster than it takes up the cloth, so the
// slack billows out, then fades the roll. The desktop is a wall behind the cloth and other windows are slabs on it
// that the cloth collides with.
//
// Each animation runs a Jolt simulation on its own worker thread, which records frames ahead of playback; advance()
// never plays past the recorded frames, so a slow worker delays the cloth instead of skipping it. Copies share the
// recording.
class ClothFlutter {
  public:
    // `window` is the rest capture box and `obstacles` the boxes of other windows, all logical pixels in one space.
    // Obstacles that overlap the window are ignored. Open always starts from the roll; Close continues an opening
    // from its current shape, otherwise starts flat. Invalid params or no live ClothPhysics leave the cloth idle.
    void   begin(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
                 std::span<const Box> obstacles);
    bool   advance(double elapsedSeconds) noexcept; // returns active()
    void   reset() noexcept;                        // -> Idle
    bool   active() const noexcept;                 // Opening or Closing
    bool   closed() const noexcept;                 // Closed
    double opacity() const noexcept;
    bool   shaded() const noexcept;                            // params.shading > 0
    void   facets(Vec2 size, PaperFacets &out) const noexcept; // back-to-front

    struct Run; // one recorded simulation; defined in ClothFlutter.cpp

  private:
    enum class Phase { Idle, Opening, Closing, Closed };

    Phase                m_phase = Phase::Idle;
    ClothParams          m_params{};
    double               m_t = 0;    // simulated seconds played
    double               m_rate = 0; // simulated seconds per real second
    std::shared_ptr<Run> m_run;
};

} // namespace liquid_glass
