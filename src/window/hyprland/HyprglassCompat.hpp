#pragma once

#include <memory>

#include <hyprland/src/plugins/PluginAPI.hpp>

// Optional adapter for Hyprglass's out-of-band window decoration pass.
class CHyprglassCompat {
  public:
    explicit CHyprglassCompat(HANDLE owner);
    ~CHyprglassCompat();
    CHyprglassCompat(const CHyprglassCompat &) = delete;
    CHyprglassCompat(CHyprglassCompat &&) = delete;
    CHyprglassCompat &operator=(CHyprglassCompat &&) = delete;
    CHyprglassCompat &operator=(const CHyprglassCompat &) = delete;
    void refresh();
    [[nodiscard]] bool available() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
