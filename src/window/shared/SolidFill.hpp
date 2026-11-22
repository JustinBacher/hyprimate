#pragma once

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "gl/CompositorContext.hpp"

// Edge liquid colour and its 4x4 texture. The texture is premultiplied DRM_FORMAT_ARGB8888 and is
// rebuilt only when the colour changes. texture() creates GL objects, so call it only while
// rendering; a cached texture is exposed only in its creating compositor context.
class CSolidFill {
  public:
    // Refreshed when the style settings change.
    void set_color(const CHyprColor &color) noexcept {
        m_color = color;
    }

    // nullptr when alpha is 0: no liquid to draw.
    SP<Render::ITexture> texture();

  private:
    hyprimate::gl::CompositorContextOwner m_owner;
    CHyprColor m_color{0.F, 0.F, 0.F, 1.F};
    CHyprColor m_built{0.F, 0.F, 0.F, 0.F};  // colour of m_texture
    SP<Render::ITexture> m_texture;
};
