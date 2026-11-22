#pragma once

#include <memory>

#include "WindowAnimationFacts.hpp"

class CHyprAnimationCanvas;
namespace hyprimate { class ConstructionSite; }

class CConstructionRenderer {
  public:
    CConstructionRenderer();
    ~CConstructionRenderer();
    CConstructionRenderer(const CConstructionRenderer &) = delete;
    CConstructionRenderer &operator=(const CConstructionRenderer &) = delete;
    CConstructionRenderer(CConstructionRenderer &&) = delete;
    CConstructionRenderer &operator=(CConstructionRenderer &&) = delete;

    [[nodiscard]] bool draw(const hyprimate::ConstructionSite &site,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
