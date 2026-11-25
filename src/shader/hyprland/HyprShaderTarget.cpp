#include "shader/hyprland/HyprShaderTarget.hpp"

#include <cmath>

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

namespace hyprimate::shader::hyprland {
    namespace {

        Render::GL::CGLFramebuffer *gl_framebuffer(const Image &image) {
            return image ? dynamic_cast<Render::GL::CGLFramebuffer *>(image.get()) : nullptr;
        }

    }
    bool encloses(const Box &outer, const Box &inner) noexcept {
        constexpr double EPS = 1e-4;
        return inner.x >= outer.x - EPS && inner.y >= outer.y - EPS &&
               inner.x + inner.w <= outer.x + outer.w + EPS &&
               inner.y + inner.h <= outer.y + outer.h + EPS;
    }

    std::optional<Extent> extent(double width, double height, unsigned max_pixels) noexcept {
        if (!(width >= 1 && height >= 1 && width <= MAX_EDGE && height <= MAX_EDGE &&
                width * height <= max_pixels && std::floor(width) == width &&
                std::floor(height) == height)) {
            return std::nullopt;
        }
        return Extent{static_cast<unsigned>(width), static_cast<unsigned>(height)};
    }

    bool same_encoding(const Image &a, const Image &b) {
        if (!a || !b) return false;
        // A normalized XBGR8888 scene has opaque destination alpha. Its
        // RGB channels have the same encoding as ABGR8888 snapshot RGB.
        if (a->m_drmFormat != b->m_drmFormat &&
            !(a->m_drmFormat == DRM_FORMAT_XBGR8888 && b->m_drmFormat == DRM_FORMAT_ABGR8888) &&
            !(a->m_drmFormat == DRM_FORMAT_XRGB8888 && b->m_drmFormat == DRM_FORMAT_ABGR8888)) {
            return false;
        }
        const auto left = a->imageDescription(), right = b->imageDescription();
        return left && right && left->value() == right->value();
    }

    DRMFormat alpha_format(DRMFormat format) {
        switch (format) {
            case DRM_FORMAT_XBGR8888:
            case DRM_FORMAT_XRGB8888: return DRM_FORMAT_ABGR8888;
            case DRM_FORMAT_XBGR2101010:
            case DRM_FORMAT_XRGB2101010: return DRM_FORMAT_ABGR2101010;
            default: return format;
        }
    }

    std::optional<Target> direct_target(Render::CRenderContext &ctx, const Image &encoding,
        const Box &box, Extent size, const std::array<float, 4> &domain, ClipList &clips,
        bool &submitted) {
        const auto &data = ctx.m_data;
        const auto monitor = data.pMonitor.lock();
        const auto &target = data.currentFB;
        auto *fb = gl_framebuffer(target);
        if (!monitor || !fb || !target->isAllocated() || target->getMirrorTexture() ||
            !same_encoding(target, encoding) || monitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL ||
            (data.projectionType != Render::RPT_MONITOR && data.projectionType != Render::RPT_OUTPUT) ||
            monitor->m_transformedSize != target->m_size ||
            (data.renderModif.enabled && !data.renderModif.modifs.empty()) ||
            box.x != std::floor(box.x) || box.y != std::floor(box.y) ||
            !encloses(Box{0, 0, target->m_size.x, target->m_size.y}, box)) return std::nullopt;
        const auto id = fb->getFBID();
        const auto texture = target->getTexture();
        if (!id || !texture || !texture->m_texID) return std::nullopt;
        auto damage = data.damage.copy().intersect(box.x, box.y, box.w, box.h);
        if (!data.clipBox.empty()) damage.intersect(CRegion{data.clipBox});
        unsigned count = 0;
        damage.forEachRect([&](const pixman_box32_t &rect) {
            if (count < clips.size()) {
                clips[count] = {rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1};
            }
            ++count;
        });
        if (count > clips.size()) return std::nullopt;
        return Target{.framebuffer = id, .texture = texture->m_texID,
            .width = size.width, .height = size.height, .domain = domain,
            .origin_x = static_cast<GLint>(box.x), .origin_y = static_cast<GLint>(box.y),
            .clips = std::span(clips).first(count), .composite = true, .submitted = &submitted};
    }

    std::optional<Target> private_target(const Image &layer, Extent size,
        const std::array<float, 4> &domain, bool blur_mask) {
        auto *fb = gl_framebuffer(layer);
        if (!fb || !layer->isAllocated() || layer->getMirrorTexture()) return std::nullopt;
        const auto tex = layer->getTexture();
        const auto id = fb->getFBID();
        if (!id || !tex || !tex->m_texID || tex->m_type != Render::TEXTURE_RGBA ||
            tex->m_size != Vector2D(double(size.width), double(size.height))) return std::nullopt;
        return Target{.framebuffer = id, .texture = tex->m_texID,
            .width = size.width, .height = size.height, .domain = domain,
            .clear_before_render = true, .blur_mask = blur_mask};
    }
}  // namespace hyprimate::shader::hyprland
