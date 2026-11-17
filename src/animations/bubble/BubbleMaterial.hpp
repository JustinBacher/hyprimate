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
    hyprimate::Vec2 source_pos;  // source-space box of the frame (BubbleFrame::size logical px)
    hyprimate::Vec2 source_size;
};

struct SBubbleImages {
    GLuint window = 0;  // RGBA window imagery; 0 when absent
    hyprimate::Vec2 texture_origin,
        texture_size;  // source-space placement of the whole window texture
    GLuint wallpaper = 0;  // background-only snapshot; 0 when absent
    hyprimate::Vec2 wallpaper_origin, wallpaper_x,
        wallpaper_y;  // unit frame box -> wallpaper UV (affine)
    hyprimate::Vec2 wallpaper_pixel;  // 1 / wallpaper texture size
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
    void draw_membranes(const hyprimate::BubbleFrame &frame,
        const SBubbleTarget &target,
        const SBubbleImages &images);
    // Draws debris (logical px from the frame centre) over the bound framebuffer. Requires
    // premultiplied blending (ONE, ONE_MINUS_SRC_ALPHA). onGlass shades beads resting on the screen
    // glass instead of droplets in flight.
    void draw_particles(std::span<const hyprimate::BubbleParticle> particles,
        hyprimate::Vec2 size,
        const SBubbleTarget &target,
        bool on_glass);
    // Draws soap microbubbles (logical px from the frame centre) over the bound framebuffer.
    // Requires premultiplied blending.
    void draw_microbubbles(std::span<const hyprimate::BubbleMicro> bubbles,
        hyprimate::Vec2 size,
        const SBubbleTarget &target);
    // Draws a wake's film ribbon and its free microbubbles, the frame being the wake's bounds.
    // Requires premultiplied blending.
    void draw_wake(const hyprimate::BubbleWakeView &wake, const SBubbleTarget &target);

    // Draw calls change only: current program, vertex array, ARRAY_BUFFER binding, active texture
    // unit, and 2D/3D texture bindings of units 0-3. Callers own every other state and restore
    // these afterwards.
  private:
    GLuint m_membrane = 0, m_curl = 0, m_particles = 0, m_film = 0, m_micro = 0, m_vao = 0,
           m_particle_vao = 0, m_micro_vao = 0, m_instance = 0, m_micro_instance = 0, m_lut = 0;
    // Per membrane: 13 fields, 4 holes of 6, then the movement's 8.
    static constexpr std::size_t K_SURFACE_UNIFORMS = 13 + (4 * 6) + 8;
    std::array<GLint, 14> m_membrane_uniforms{};
    std::array<std::array<GLint, K_SURFACE_UNIFORMS>, 2> m_surface_uniforms{};
    std::array<GLint, K_SURFACE_UNIFORMS> m_curl_surface_uniforms{};
    std::array<GLint, 15> m_curl_uniforms{};
    std::array<GLint, 10> m_sheet_uniforms{};
    std::array<std::array<GLint, 3>, 7> m_sheet_mask_uniforms{};
    std::array<GLint, 6> m_particle_uniforms{};
    std::array<GLint, 10> m_film_uniforms{};
    std::array<GLint, 6> m_micro_uniforms{};
    bool m_tried = false;
};
