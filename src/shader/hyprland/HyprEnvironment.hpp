#pragma once

#include <cstdint>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    // Borrowed only while the enclosing native draw is executing.
    struct WindowEnvironment {
        EnvironmentInputs inputs;
        SP<Render::ITexture> original;
    };
    WindowEnvironment window_environment(Render::CRenderContext &ctx,
        const Environment &request,
        const std::array<float, 4> &physical_box);
    void emit_environment(Render::CRenderContext &ctx,
        const Environment &request,
        std::uint64_t window,
        GLuint texture);
}  // namespace hyprimate::shader
