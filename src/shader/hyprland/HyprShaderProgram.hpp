#pragma once

#include <memory>
#include <cstdint>
#include <string>

#include "compute/Compute.hpp"
#include "gl/CompositorContext.hpp"
#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    // Shared prepared generation for window and scene instances. The existing
    // window-facing type name is also part of the style selection interface.
    class WindowProgram {
      public:
        ~WindowProgram();

        std::shared_ptr<gl::CompositorContextOwner> owner =
            std::make_shared<gl::CompositorContextOwner>();
        Program program;
        Values values;
        compute::Backend compute_backend = compute::Backend::Auto;
        bool has_compute = false;

        void log_compute_instance_retirement(std::uint64_t instance_id, const compute::Statistics &stats,
            const char *kind) const noexcept;
    };
    std::uint64_t next_compute_instance_id() noexcept;

    std::shared_ptr<WindowProgram> prepare_window_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error);
}  // namespace hyprimate::shader
