#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <hyprland/src/plugins/PluginAPI.hpp>

class CHyprWindowPort;

namespace hyprimate::perf {
    class RuntimeProfiler {
      public:
        RuntimeProfiler(HANDLE handle, CHyprWindowPort &port,
            std::function<void(std::uint64_t)> presented);
        ~RuntimeProfiler();
        RuntimeProfiler(const RuntimeProfiler &) = delete;
        RuntimeProfiler &operator=(const RuntimeProfiler &) = delete;
        RuntimeProfiler(RuntimeProfiler &&) = delete;
        RuntimeProfiler &operator=(RuntimeProfiler &&) = delete;
      private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
