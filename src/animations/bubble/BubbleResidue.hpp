#pragma once

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <chrono>
#include <vector>

class CBubbleRenderer;

// Screen-space deposits retain neither the window nor its textures.
class CBubbleResidue {
  public:
    bool deposit(const PHLMONITOR &monitor, Vector2D position, double radius, double seed);
    void tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor);
    void queue(Render::CRenderContext &ctx, const PHLMONITOR &monitor, const SP<CBubbleRenderer> &renderer);
    bool active(const PHLMONITOR &monitor) const;
    void setEnabled(bool enabled);
    void clear();

  private:
    struct Deposit {
        PHLMONITORREF                         monitor;
        Vector2D                              position;
        double                                radius;
        double                                seed;
        std::chrono::steady_clock::time_point start;
        double                                age = 0.0;
    };
    bool                 m_enabled = true;
    std::vector<Deposit> m_deposits;
};
