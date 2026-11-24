#pragma once

#include <memory>
#include <string>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    class SceneProgram;
    std::shared_ptr<SceneProgram> prepare_scene_program(
        Event event, const std::string &path, const std::string &parameters, std::string &error);

    // The caller owns and revalidates the provenance of both endpoint leases.
    // A wallpaper has no endpoint leases; it receives only transparent black.
    class SceneShader {
      public:
        explicit SceneShader(std::shared_ptr<SceneProgram> program);
        ~SceneShader();
        SceneShader(SceneShader &&) = delete;
        SceneShader &operator=(SceneShader &&) = delete;
        SceneShader(const SceneShader &) = delete;
        SceneShader &operator=(const SceneShader &) = delete;
        void advance(const Frame &frame) noexcept;
        bool draw(Render::CRenderContext &ctx,
            const SP<Render::IFramebuffer> &source,
            const SP<Render::IFramebuffer> &destination,
            std::string &error,
            bool authoritative = true);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace hyprimate::shader
