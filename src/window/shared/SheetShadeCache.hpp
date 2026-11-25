#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include <hyprland/src/helpers/memory/Memory.hpp>

#include "gl/ContextOwnerState.hpp"
#include "gl/Storage.hpp"

namespace hyprimate::gl {
    // The host supplies only the CPU texture factory and native anchors. The ramp and its
    // owner transition are shared with the surfaceless EGL test.
    template <class Texture, class Owner, class Current, class Scope>
    class SheetShadeCache {
      public:
        template <class Create, class Retain, class Retire>
        SP<Texture> texture(Create create, Retain retain, Retire retire) {
            if (m_texture) {
                const Current current;
                switch (m_owner.classify_current(current)) {
                    case ContextOwnerState::current: return m_texture;
                    case ContextOwnerState::unavailable: return nullptr;
                    case ContextOwnerState::replaced:
                        if (!retire(m_texture, current)) return nullptr;
                        m_texture.reset();
                        m_owner.reset();
                        break;
                }
            }

            const Scope scope;
            Owner owner;
            if (!scope || !owner.ensure_current()) return nullptr;

            // Premultiplied; memory order is B, G, R, A for DRM_FORMAT_ARGB8888.
            std::array<std::uint8_t, RAMP_SIZE * 4> pixels;
            for (int i = 0; i < RAMP_SIZE; ++i) {
                const double coord = std::max((i - 128) / 127.0, -1.0);
                const std::uint8_t colour = ramp_byte(std::max(coord, 0.0) * 0.45);
                const std::uint8_t alpha = coord < 0.0 ? ramp_byte(-coord * 0.6) : colour;
                pixels[i * 4] = colour;
                pixels[i * 4 + 1] = colour;
                pixels[i * 4 + 2] = colour;
                pixels[i * 4 + 3] = alpha;
            }

            const TextureUploadBinding binding;
            const ClientPixelUnpack unpack;
            auto candidate = create(pixels.data(), RAMP_SIZE);
            if (!candidate || !has_color_storage(candidate->m_texID)) return nullptr;

            m_texture = retain(std::move(candidate));
            if (m_texture) m_owner = std::move(owner);
            return m_texture;
        }

      private:
        static constexpr int RAMP_SIZE = 256;

        static std::uint8_t ramp_byte(double unit) noexcept {
            return static_cast<std::uint8_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
        }

        Owner m_owner;
        SP<Texture> m_texture;
    };
}  // namespace hyprimate::gl
