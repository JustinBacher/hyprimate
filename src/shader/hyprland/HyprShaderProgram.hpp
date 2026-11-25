#pragma once

#include <memory>
#include <string>

#include "gl/CompositorContext.hpp"
#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    // Shared prepared generation for window and scene instances. The existing
    // window-facing type name is also part of the style selection interface.
    class WindowProgram {
      public:
        ~WindowProgram();

        gl::CompositorContextOwner owner;
        Program program;
        Values values;
    };

    std::shared_ptr<WindowProgram> prepare_window_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error);
}  // namespace hyprimate::shader
