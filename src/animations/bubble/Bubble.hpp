#pragma once

#include "animations/shared/Core.hpp"

#include <cstdint>

namespace liquid_glass {

struct BubbleFrame {
    Vec2   size{};
    Vec2   halfSize{};
    double corner = 0;
    double depth = 0;
    double content = 1;
    double windowMix = 1; // wallpaper first; window imagery arrives as the membrane flattens
    double film = 0;
    double visibility = 1;
    double ripple = 0;
    double time = 0;
    Vec2   rupture{};
    double tear = 0;
    double retract = 0;
    double remnant = 0;
    double seed = 0;
    bool   closing = false;
};

class Bubble {
  public:
    void        begin(Transition direction, double duration, std::uint64_t seed, Vec2 size);
    void        advance(double elapsed);
    void        reset();
    bool        active() const;
    bool        opening() const;
    bool        closed() const;
    BubbleFrame frame(Vec2 size) const;
    Box         bounds(Vec2 size) const;

  private:
    BubbleFrame initial_{};
    Vec2        size_{};
    double      duration_ = 0;
    double      elapsed_ = 0;
    double      seed_ = 0;
    Vec2        rupture_{};
    bool        active_ = false;
    bool        opening_ = false;
    bool        closed_ = false;
};

} // namespace liquid_glass
