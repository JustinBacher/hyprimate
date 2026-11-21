#pragma once

#include <memory>

#include "BurnGround.hpp"

class CHyprAnimationCanvas;
class CBurnGroundDraw {
  public:
    CBurnGroundDraw();
    ~CBurnGroundDraw();
    CBurnGroundDraw(const CBurnGroundDraw &) = delete;
    CBurnGroundDraw &operator=(const CBurnGroundDraw &) = delete;
    bool warm_up();
    void queue(CHyprAnimationCanvas &canvas,
        const CBurnGround &ground,
        const hyprimate::animation::ScreenFacts &screen);

  private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
