#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include <drm_fourcc.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/NativeFramebuffer.hpp"
#include "gl/NativeResource.hpp"
#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    // One RGBA8 texel per producer. The renderer copies, never retains, the
    // animation's texture. The receiver gets a separately cleared row laid
    // out in interaction order, containing only the preceding frame's samples.
    class DisturbanceAtlas {
      public:
        DisturbanceAtlas() = default;
        DisturbanceAtlas(const DisturbanceAtlas &) = delete;
        DisturbanceAtlas &operator=(const DisturbanceAtlas &) = delete;
        ~DisturbanceAtlas() {
            reset();
        }
        std::uint64_t copied_bytes() const noexcept {
            return m_copied_bytes;
        }
        std::uint64_t uploaded_bytes() const noexcept {
            return m_uploaded_bytes;
        }

        void reset() noexcept {
            const gl::ScopedCompositorContext context{m_owner};
            if (context && m_read) glDeleteFramebuffers(1, &m_read);
            m_read = 0;
            gl::release_native(m_mailbox, m_owner);
            gl::release_native(m_batch, m_owner);
            m_owner.reset();
            m_slots = {};
            m_copied_bytes = m_uploaded_bytes = 0;
        }

        void put(std::uint64_t frame, std::uint64_t id, GLuint source) {
            if (!frame || !id || !source || !m_owner.ensure_current()) return;
            const auto slot =
                std::ranges::find_if(m_slots, [&](const Slot &entry) { return entry.id == id; });
            auto candidate =
                slot == m_slots.end() ? std::ranges::min_element(m_slots, {}, &Slot::frame) : slot;
            if (candidate->frame == frame) return;
            const Bindings bindings;
            if (!prepare(bindings)) return;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_read);
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, name(m_mailbox));
                const auto x = int(candidate - m_slots.begin());
                glBlitFramebuffer(0, 0, 1, 1, x, 0, x + 1, 1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                if (glGetError() == GL_NO_ERROR) {
                    *candidate = {frame, id};
                    m_copied_bytes += 4;
                } else {
                    *candidate = {};
                }
            }
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        }

        GLuint gather(std::uint64_t frame, std::span<const std::uint64_t> ids) {
            if (frame < 2 || ids.empty() || !m_mailbox || !m_owner.ensure_current()) return 0;
            const auto active = [&](const Slot &slot) {
                return slot.id && slot.frame == frame - 1;
            };
            if (std::ranges::none_of(m_slots, active)) return 0;
            const Bindings bindings;
            if (!prepare(bindings)) return 0;
            {
                const gl::TextureUploadBinding texture;
                const gl::ClientPixelUnpack unpack;
                constexpr std::array<std::uint8_t, MAX_INTERACTIONS * 4> ZERO{};
                glBindTexture(GL_TEXTURE_2D, m_batch->getTexture()->m_texID);
                glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    MAX_INTERACTIONS,
                    1,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    ZERO.data());
                m_uploaded_bytes += ZERO.size();
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, name(m_mailbox));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, name(m_batch));
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const auto position = std::ranges::find_if(
                    m_slots, [&](const Slot &slot) { return active(slot) && slot.id == ids[i]; });
                if (position == m_slots.end()) continue;
                const auto x = int(position - m_slots.begin()), y = int(i);
                glBlitFramebuffer(x, 0, x + 1, 1, y, 0, y + 1, 1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                m_copied_bytes += 4;
            }
            return glGetError() == GL_NO_ERROR ? m_batch->getTexture()->m_texID : 0;
        }

        std::uint64_t gpu_bytes() const noexcept {
            return (unsigned(bool(m_mailbox)) + unsigned(bool(m_batch))) * MAX_INTERACTIONS * 4;
        }

      private:
        using Image = SP<Render::IFramebuffer>;
        std::uint64_t m_copied_bytes = 0, m_uploaded_bytes = 0;
        struct Slot {
            std::uint64_t frame = 0, id = 0;
        };
        struct Bindings {
            GLint read = 0, draw = 0;
            GLboolean scissor = GL_FALSE;
            Bindings() {
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                scissor = glIsEnabled(GL_SCISSOR_TEST);
                glDisable(GL_SCISSOR_TEST);
            }
            ~Bindings() {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
                if (scissor)
                    glEnable(GL_SCISSOR_TEST);
                else
                    glDisable(GL_SCISSOR_TEST);
            }
        };
        static GLuint name(const Image &image) {
            auto *fb = dynamic_cast<Render::GL::CGLFramebuffer *>(image.get());
            return fb ? fb->getFBID() : 0;
        }
        static Image allocate() {
            auto image =
                gl::retain_native(g_pHyprRenderer->createFB("hyprimate disturbance atlas"));
            if (!image ||
                !gl::allocate_framebuffer(*image, MAX_INTERACTIONS, 1, DRM_FORMAT_ABGR8888))
                return {};
            return image;
        }
        bool prepare(const Bindings &) {
            if (!m_read) glGenFramebuffers(1, &m_read);
            if (!m_mailbox) m_mailbox = allocate();
            if (!m_batch) m_batch = allocate();
            glDisable(GL_SCISSOR_TEST);
            return m_read && name(m_mailbox) && name(m_batch);
        }
        gl::CompositorContextOwner m_owner;
        Image m_mailbox, m_batch;
        GLuint m_read = 0;
        std::array<Slot, MAX_INTERACTIONS> m_slots{};
    };
}  // namespace hyprimate::shader
