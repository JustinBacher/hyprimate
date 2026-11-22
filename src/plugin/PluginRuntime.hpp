#pragma once

#include <memory>

#include <hyprland/src/plugins/PluginAPI.hpp>

class CPluginRuntime {
  public:
    explicit CPluginRuntime(HANDLE handle);
    ~CPluginRuntime();
    CPluginRuntime(const CPluginRuntime &) = delete;
    CPluginRuntime &operator=(const CPluginRuntime &) = delete;
    CPluginRuntime(CPluginRuntime &&) = delete;
    CPluginRuntime &operator=(CPluginRuntime &&) = delete;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
