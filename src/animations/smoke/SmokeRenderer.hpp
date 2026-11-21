#pragma once
#include <array>
#include <memory>
#include <optional>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <hyprland/src/render/Shader.hpp>
#include "animations/smoke/SmokeMaterial.hpp"

// One window's smoke fluid, released in the compositor's context when the window's smoke
// ends or its style is destroyed.
class CSmokeFieldHandle {
  public:
    CSmokeFieldHandle() = default;
    ~CSmokeFieldHandle();
    CSmokeFieldHandle(const CSmokeFieldHandle &) = delete;
    CSmokeFieldHandle &operator=(const CSmokeFieldHandle &) = delete;
    CSmokeField field;
};
struct SSmokeDraw {
    SP<Render::ITexture> window;
    std::shared_ptr<CSmokeFieldHandle> field;
    hyprimate::SmokeFrame frame;
    Vector2D canvas;
    CBox source;
    hyprimate::Vec2 texture_origin, texture_size, output_origin;
    bool simulate = true;
};
class CSmokeRenderer {
  public:
    CSmokeRenderer() = default;
    ~CSmokeRenderer();
    CSmokeRenderer(const CSmokeRenderer &) = delete;
    CSmokeRenderer &operator=(const CSmokeRenderer &) = delete;
    SP<Render::IFramebuffer> render(Render::CRenderContext &ctx, const SSmokeDraw &draw);
    bool warm_up();

  private:
    bool initialize();
    std::array<std::unique_ptr<CShader>, SSmokePrograms::ProgramCount> m_shaders;
    SSmokePrograms m_programs;
    bool m_tried = false;
    CSmokeMaterial m_material;
    bool m_warmed = false;
};
