#pragma once

#include <memory>
#include <string>

namespace hyprimate::shader {
    // Owns the monitor listeners and native background passes for one plugin runtime.
    class ShaderWallpaper {
      public:
        ShaderWallpaper();
        ~ShaderWallpaper();
        ShaderWallpaper(const ShaderWallpaper &) = delete;
        ShaderWallpaper &operator=(const ShaderWallpaper &) = delete;
        ShaderWallpaper(ShaderWallpaper &&) = delete;
        ShaderWallpaper &operator=(ShaderWallpaper &&) = delete;

        // Empty path restores native wallpaper. Zero duration runs continuously.
        // A failed replacement leaves the current program and output states intact.
        void configure(const std::string &path, const std::string &parameters, int duration_ms);
        void shutdown();

      private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace hyprimate::shader
