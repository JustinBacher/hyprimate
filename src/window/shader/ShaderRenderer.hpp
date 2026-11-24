#pragma once

#include <memory>
#include <string>

#include "shader/Package.hpp"
#include "window/hyprland/HyprAnimationCanvas.hpp"

namespace hyprimate::shader {
    struct Frame;
    class WindowProgram;

    std::shared_ptr<WindowProgram> prepare_window_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error);
    bool window_program_supports_blur(const std::shared_ptr<WindowProgram> &program) noexcept;

    // One instance per window transition. The render callback borrows the mapped
    // framebuffer or an isolated copy of the authorized native close snapshot.
    class WindowRenderer {
      public:
        explicit WindowRenderer(std::shared_ptr<WindowProgram> program);
        ~WindowRenderer();
        WindowRenderer(const WindowRenderer &) = delete;
        WindowRenderer &operator=(const WindowRenderer &) = delete;

        Box bounds(const Box &capture) const noexcept;
        bool supports_blur() const noexcept;
        bool resize(const Box &capture, double scale, std::string &error);
        bool bind_close_source(const Box &source, const CHyprAnimationCanvas::Image &image,
            std::string &error);
        void advance(const Frame &frame) noexcept;
        bool draw(const animation::Draw<CHyprAnimationCanvas> &draw, bool closing, std::string &error);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace hyprimate::shader
