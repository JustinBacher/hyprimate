#pragma once

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Texture.hpp>

// Edge liquid colour and its 4x4 texture. The texture is premultiplied DRM_FORMAT_ARGB8888 and is rebuilt only when
// the colour changes. texture() creates GL objects, so call it only while rendering.
class CSolidFill {
  public:
    // Refreshed from config before every frame; cheap when unchanged.
    void                    setColor(const CHyprColor& color) noexcept { m_color = color; }
    const CHyprColor&       color() const noexcept { return m_color; }

    // nullptr when alpha is 0: no liquid to draw.
    SP<Render::ITexture>    texture();

  private:
    CHyprColor              m_color{0.F, 0.F, 0.F, 1.F};
    CHyprColor              m_built{0.F, 0.F, 0.F, 0.F}; // colour of m_texture
    SP<Render::ITexture>    m_texture;
};
