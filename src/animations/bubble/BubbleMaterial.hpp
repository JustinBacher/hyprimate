#pragma once

#include <array>
#include <span>

#include <GLES3/gl32.h>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleWake.hpp"

// The bubble's GLES 3 material, free of compositor types so offscreen previews render exactly what
// the plugin draws. Every position below is in device pixels of one "source space" (the
// compositor's output space for the frame).

// Where the bound framebuffer sits relative to the frame's undeformed box.
struct SBubbleTarget {
    hyprimate::Vec2 canvas;  // framebuffer size
    hyprimate::Vec2
        origin;  // source-space position of framebuffer pixel (0, 0) (gl_FragCoord origin)
    hyprimate::Vec2 sourcePos;  // source-space box of the frame (BubbleFrame::size logical px)
    hyprimate::Vec2 sourceSize;
};

struct SBubbleImages {
    GLuint window = 0;  // RGBA window imagery; 0 when absent
    hyprimate::Vec2 textureOrigin,
        textureSize;  // source-space placement of the whole window texture
    GLuint wallpaper = 0;  // background-only snapshot; 0 when absent
    hyprimate::Vec2 wallpaperOrigin, wallpaperX,
        wallpaperY;  // unit frame box -> wallpaper UV (affine)
    hyprimate::Vec2 wallpaperPixel;  // 1 / wallpaper texture size
};

class CBubbleMaterial {
  public:
    // Compiles the programs and builds lookup textures and instance buffers; idempotent. False when
    // unavailable.
    bool initialize();
    // Deletes GL objects; the creating context must be current.
    void release();

    // Replaces the bound framebuffer's pixels with both membranes, premultiplied. Requires blending
    // disabled.
    void drawMembranes(
        const hyprimate::BubbleFrame &, const SBubbleTarget &, const SBubbleImages &);
    // Draws debris (logical px from the frame centre) over the bound framebuffer. Requires
    // premultiplied blending (ONE, ONE_MINUS_SRC_ALPHA). onGlass shades beads resting on the screen
    // glass instead of droplets in flight.
    void drawParticles(std::span<const hyprimate::BubbleParticle>,
        hyprimate::Vec2 frameSize,
        const SBubbleTarget &,
        bool onGlass);
    // Draws soap microbubbles (logical px from the frame centre) over the bound framebuffer.
    // Requires premultiplied blending.
    void drawMicrobubbles(
        std::span<const hyprimate::BubbleMicro>, hyprimate::Vec2 frameSize, const SBubbleTarget &);
    // Draws a wake's film ribbon and its free microbubbles, the frame being the wake's bounds.
    // Requires premultiplied blending.
    void drawWake(const hyprimate::BubbleWakeView &, const SBubbleTarget &);

    // Draw calls change only: current program, vertex array, ARRAY_BUFFER binding, active texture
    // unit, and 2D/3D texture bindings of units 0-3. Callers own every other state and restore
    // these afterwards.
  private:
    GLuint m_membrane = 0, m_curl = 0, m_particles = 0, m_film = 0, m_micro = 0, m_vao = 0,
           m_particleVao = 0, m_microVao = 0, m_instance = 0, m_microInstance = 0, m_lut = 0;
    // Per membrane: 13 fields, 4 holes of 6, then the movement's 7.
    static constexpr std::size_t kSurfaceUniforms = 13 + (4 * 6) + 7;
    std::array<GLint, 14> m_membraneUniforms{};
    std::array<std::array<GLint, kSurfaceUniforms>, 2> m_surfaceUniforms{};
    std::array<GLint, kSurfaceUniforms> m_curlSurfaceUniforms{};
    std::array<GLint, 15> m_curlUniforms{};
    std::array<GLint, 10> m_sheetUniforms{};
    std::array<std::array<GLint, 3>, 7> m_sheetMaskUniforms{};
    std::array<GLint, 6> m_particleUniforms{};
    std::array<GLint, 10> m_filmUniforms{};
    std::array<GLint, 6> m_microUniforms{};
    bool m_tried = false;
};
