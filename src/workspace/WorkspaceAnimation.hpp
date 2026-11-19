#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "workspace/rotation/GapFill.hpp"

class CWorkspaceAnimation {
  public:
    explicit CWorkspaceAnimation(HANDLE handle);
    ~CWorkspaceAnimation();

    CWorkspaceAnimation(const CWorkspaceAnimation &) = delete;
    CWorkspaceAnimation &operator=(const CWorkspaceAnimation &) = delete;
    CWorkspaceAnimation(CWorkspaceAnimation &&) = delete;
    CWorkspaceAnimation &operator=(CWorkspaceAnimation &&) = delete;

    bool install(std::string &error);
    void shutdown();
    void configure(bool enabled, std::string_view style, double duration_seconds,
        const hyprimate::workspace::GapFill &gap, std::string_view curve_name);
    void on_render_pre_checks(const PHLMONITOR &monitor);
    void on_render_pre(const PHLMONITOR &monitor);
    void remove_monitor(const PHLMONITOR &monitor);
    void clear();

    class Impl;

  private:
    std::unique_ptr<Impl> m_impl;
};
