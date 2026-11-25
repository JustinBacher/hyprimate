#include <algorithm>
#include <stdexcept>
#include <utility>

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "HyprWindowPort.hpp"
#include "HyprWindowPortGeometry.hpp"
#include "WindowAnimationPass.hpp"
#include "gl/NativeResource.hpp"
#include "window/WindowAnimations.hpp"

using namespace hyprimate::animation;
using namespace hyprimate::window_port_detail;

CHyprWindowPort::VisualCache &CHyprWindowPort::acquire_visuals(Handle handle) {
    m_visuals.push_back({.handle = handle, .cache = std::make_unique<VisualCache>()});
    return *m_visuals.back().cache;
}
CHyprWindowPort::VisualCache &CHyprWindowPort::visual_cache(Handle handle) {
    for (auto &item : m_visuals) {
        if (item.handle == handle) {
            return *item.cache;
        }
    }
    throw std::logic_error("missing window animation visuals");
}
const CHyprWindowPort::VisualCache *CHyprWindowPort::find_visuals(Handle handle) const {
    for (const auto &item : m_visuals) {
        if (item.handle == handle) {
            return item.cache.get();
        }
    }
    return nullptr;
}
void CHyprWindowPort::release_visuals(Handle handle) {
    std::erase_if(m_visuals, [handle](const auto &item) { return item.handle == handle; });
}
const CHyprWindowPort::Backdrop *CHyprWindowPort::backdrop(Handle handle, PassKind pass) const {
    const auto *cache = find_visuals(handle);
    if (cache == nullptr || pass == PassKind::Inert) {
        return nullptr;
    }
    const auto &slot =
        pass == PassKind::WorkspaceSource ? cache->scratch_backdrop : cache->backdrop;
    return slot && slot->owner.ensure_current() ? &slot : nullptr;
}
void CHyprWindowPort::retain_mapped_image(Handle handle,
    const Canvas::Image &image,
    hyprimate::Vec2 origin,
    hyprimate::Box capture,
    FrameId frame) {
    auto &cache = visual_cache(handle);
    if (!cache.image && image && cache.image_owner.ensure_current()) {
        cache.image = image;
        cache.image_origin = origin;
        cache.capture = capture;
        cache.frame = frame;
    }
}
void CHyprWindowPort::release_mapped_image(Handle handle) {
    auto &cache = visual_cache(handle);
    hyprimate::gl::release_native(cache.image, cache.image_owner);
    cache.image_owner.reset();
    cache.image_origin = {};
    cache.capture = {};
    cache.frame = 0;
}
void CHyprWindowPort::queue_tail(
    Handle handle, Canvas &canvas, const Canvas::Image &image, const TailDraw &draw) {
    if (m_module == nullptr || !image) {
        return;
    }
    CWindowAnimationPass::SData data;
    data.animations = m_module;
    data.resources = &m_canvas_resources;
    data.handle = handle;
    data.image = image;
    data.image_owner = visual_cache(handle).image_owner;
    data.retained_tail = true;
    data.no_screen_share = draw.no_screen_share;
    data.source = draw.source;
    data.output = draw.output;
    data.canvas_box = draw.canvas_box;
    data.image_origin = draw.image_origin;
    data.image_size = draw.image_size;
    data.scale = draw.scale;
    data.alpha = draw.alpha;
    data.monitor = draw.monitor;
    data.frame = draw.frame;
    data.damage = CRegion{host_box(draw.canvas_box)};
    Render::IHyprRenderer::addPassElement(
        canvas.context(), makeUnique<CWindowAnimationPass>(std::move(data)));
}
std::optional<RetainedFrame<CHyprWindowPort::Canvas>> CHyprWindowPort::retained_mapped_image(
    Handle handle) const {
    const auto *cache = find_visuals(handle);
    if (cache == nullptr || !cache->image || !cache->image_owner.ensure_current()) {
        return std::nullopt;
    }
    const auto TEXTURE = cache->image->getTexture();
    const auto SIZE = TEXTURE ? vec_of(TEXTURE->m_size) : hyprimate::Vec2{};
    return RetainedFrame<Canvas>{.image = &cache->image,
        .image_origin = cache->image_origin,
        .image_size = SIZE,
        .capture = cache->capture,
        .frame = cache->frame};
}
CHyprWindowPort::RetainedImage CHyprWindowPort::fadeout_image(Handle handle) const {
    const auto *cache = find_visuals(handle);
    return cache != nullptr && cache->fadeout_image && cache->fadeout_owner.ensure_current()
               ? cache->fadeout_image
               : RetainedImage{};
}
