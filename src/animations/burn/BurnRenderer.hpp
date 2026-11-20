#pragma once

#include <memory>

#include "WindowAnimationFacts.hpp"
#include "animations/burn/BurnDynamics.hpp"

class CHyprAnimationCanvas;
class CBurnRenderer {
  public:
    CBurnRenderer();
    ~CBurnRenderer();
    CBurnRenderer(const CBurnRenderer &) = delete;
    CBurnRenderer &operator=(const CBurnRenderer &) = delete;
    CBurnRenderer(CBurnRenderer &&) = delete;
    CBurnRenderer &operator=(CBurnRenderer &&) = delete;
    void warm_up();
    bool draw(const hyprimate::BurnSimulation &simulation,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
