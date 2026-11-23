#pragma once

#include <memory>

#include <hyprland/src/plugins/PluginAPI.hpp>

#include "HyprWindowAnimations.hpp"

class CHyprWindowPort;

// Optional adapter for Hyprglass's out-of-band window decoration pass.
class CHyprglassCompat {
  public:
    CHyprglassCompat(HANDLE owner, HyprWindowAnimations &animations, CHyprWindowPort &port);
    ~CHyprglassCompat();
    CHyprglassCompat(const CHyprglassCompat &) = delete;
    CHyprglassCompat(CHyprglassCompat &&) = delete;
    CHyprglassCompat &operator=(CHyprglassCompat &&) = delete;
    CHyprglassCompat &operator=(const CHyprglassCompat &) = delete;
    void refresh();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
