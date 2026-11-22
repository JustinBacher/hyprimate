#pragma once

#include <memory>

#include "WindowAnimationFacts.hpp"

class CHyprAnimationCanvas;
namespace hyprimate { struct ConstructionBatch; }

class CConstructionRenderer {
  public:
    CConstructionRenderer();
    ~CConstructionRenderer();
    CConstructionRenderer(const CConstructionRenderer &) = delete;
    CConstructionRenderer &operator=(const CConstructionRenderer &) = delete;
    CConstructionRenderer(CConstructionRenderer &&) = delete;
    CConstructionRenderer &operator=(CConstructionRenderer &&) = delete;

    [[nodiscard]] bool draw(const hyprimate::ConstructionBatch &batch, hyprimate::Box body,
        hyprimate::Box capture, const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
