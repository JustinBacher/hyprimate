#include <algorithm>

#include "HyprWindowPort.hpp"

using namespace hyprimate::animation;

WindowId CHyprWindowPort::identity(const WindowRef &ref) const {
    const auto WINDOW = ref.lock();
    if (!WINDOW) {
        return 0;
    }
    for (const auto &item : m_windows) {
        if (item.object.lock() == WINDOW) {
            return item.id;
        }
    }
    std::erase_if(m_windows, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_window++;
    m_windows.push_back({.id = IDENTITY, .object = ref});
    return IDENTITY;
}
MonitorId CHyprWindowPort::monitor_identity(const PHLMONITOR &monitor) const {
    if (!monitor) {
        return 0;
    }
    for (const auto &item : m_monitors) {
        if (item.object.lock() == monitor) {
            return item.id;
        }
    }
    std::erase_if(m_monitors, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_monitor++;
    m_monitors.push_back({.id = IDENTITY, .object = PHLMONITORREF{monitor}});
    return IDENTITY;
}
WorkspaceId CHyprWindowPort::workspace_identity(const PHLWORKSPACE &workspace) const {
    if (!workspace) {
        return 0;
    }
    for (const auto &item : m_workspaces) {
        if (item.object.lock() == workspace) {
            return item.id;
        }
    }
    std::erase_if(m_workspaces, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_workspace++;
    m_workspaces.push_back({.id = IDENTITY, .object = PHLWORKSPACEREF{workspace}});
    return IDENTITY;
}
std::uint64_t CHyprWindowPort::register_fadeout(const SP<Desktop::CWindowFadeout> &fadeout) {
    if (!fadeout) {
        return 0;
    }
    const SP<Desktop::IFadeout> BASE = fadeout;
    if (const auto IDENTITY = fadeout_identity(BASE)) {
        return IDENTITY;
    }
    std::erase_if(m_fadeouts, [](const auto &item) { return item.object.expired(); });
    const auto IDENTITY = m_next_fadeout++;
    m_fadeouts.push_back({.id = IDENTITY, .object = WP<Desktop::IFadeout>{BASE}});
    return IDENTITY;
}
std::uint64_t CHyprWindowPort::fadeout_identity(const SP<Desktop::IFadeout> &fadeout) const {
    if (!fadeout) {
        return 0;
    }
    for (const auto &item : m_fadeouts) {
        if (item.object.lock() == fadeout) {
            return item.id;
        }
    }
    return 0;
}
PHLMONITOR CHyprWindowPort::monitor_for(MonitorId monitor_id) const {
    for (const auto &item : m_monitors) {
        if (item.id == monitor_id) {
            return item.object.lock();
        }
    }
    return {};
}
void CHyprWindowPort::forget_window_reference([[maybe_unused]] WindowId window) {
    // Keep live weak identities stable if a later host event reports the same object.
    std::erase_if(m_windows, [](const auto &item) { return item.object.expired(); });
}
