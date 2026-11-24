#include "shader/HyprWallpaperSource.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/ColorManagement.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>

namespace hyprimate::shader {
    namespace {
        bool positive_size(const Vector2D &size) {
            return size.x > 0 && size.y > 0 && std::isfinite(size.x) && std::isfinite(size.y);
        }

        bool is_wallpaper_texture(const SP<Render::ITexture> &texture) {
            return texture && texture->ok() && !texture->isDMA() &&
                texture->m_transform == HYPRUTILS_TRANSFORM_NORMAL && positive_size(texture->m_size) &&
                (texture->m_type == Render::TEXTURE_RGBX || texture->m_type == Render::TEXTURE_RGBA);
        }

        bool compatible_sdr(const NColorManagement::PImageDescription &source,
            const NColorManagement::PImageDescription &target) {
            using namespace NColorManagement;
            if (!source || !target || source->needsCM(target)) return false;
            const auto &image = source->value();
            return (image.transferFunction == CM_TRANSFER_FUNCTION_SRGB ||
                    image.transferFunction == CM_TRANSFER_FUNCTION_GAMMA22) &&
                image.transferFunctionPower == 1.F && !image.isWindows && !image.icc.present &&
                image.rawICC.empty() && image.getPrimaries() == NColorPrimaries::BT709 &&
                image.luminances == SImageDescription::SPCLuminances{} && image.maxCLL == 0 && image.maxFALL == 0;
        }

        bool solid(const SSurfaceState &state) {
            if (state.texture->m_opaque || state.texture->m_type == Render::TEXTURE_RGBX) {
                return true;
            }
            // For alpha-bearing wl_shm, require the entire committed surface to be opaque.
            // The bounds of a region alone would also accept regions with holes.
            constexpr auto LIMIT = std::numeric_limits<int32_t>::max();
            if (state.size.x > LIMIT || state.size.y > LIMIT) {
                return false;
            }
            const pixman_box32_t BOX{.x1 = 0, .y1 = 0,
                .x2 = static_cast<int32_t>(std::ceil(state.size.x)),
                .y2 = static_cast<int32_t>(std::ceil(state.size.y))};
            return pixman_region32_contains_rectangle(state.opaque.pixman(), &BOX) == PIXMAN_REGION_IN;
        }

        bool covers_output(const CBox &box, const Vector2D &output) {
            return box.x <= 0 && box.y <= 0 && box.x + box.w >= output.x && box.y + box.h >= output.y;
        }

        OriginalWallpaper stock_wallpaper(const PHLMONITOR &monitor) {
            static const auto DISABLED = CConfigValue<Config::INTEGER>("misc:disable_hyprland_logo");
            static const auto NO_SPLASH = CConfigValue<Config::INTEGER>("misc:disable_splash_rendering");
            const auto &texture = monitor->m_background;
            if (*DISABLED != 0 || (*NO_SPLASH == 0 && monitor->m_splash) || !monitor->m_backgroundOpacity ||
                monitor->m_backgroundOpacity->isBeingAnimated() || monitor->m_backgroundOpacity->value() != 1.F ||
                !is_wallpaper_texture(texture) || !compatible_sdr(texture->m_imageDescription ?
                    texture->m_imageDescription : NColorManagement::getDefaultImageDescription(),
                    monitor->workBufferImageDescription())) {
                return {};
            }

            // renderBackground draws this already-owned texture with centered cover
            // in transformed output pixels. getBackground may have pre-scaled/cropped
            // the asset into an output-sized texture; do not crop the PNG a second time.
            const auto &output = monitor->m_transformedSize;
            const auto &image = texture->m_size;
            const double COVER = std::max(output.x / image.x, output.y / image.y);
            const double SPAN_X = std::min(output.x / (image.x * COVER), 1.0);
            const double SPAN_Y = std::min(output.y / (image.y * COVER), 1.0);
            return {.texture = texture,
                .uv = {static_cast<float>((1.0 - SPAN_X) * 0.5), static_cast<float>((1.0 - SPAN_Y) * 0.5),
                    static_cast<float>(SPAN_X), static_cast<float>(SPAN_Y)},
                .owner = {}};
        }

        std::optional<std::array<float, 4>> layer_uv(
            const Monitor::CMonitor &monitor, const Desktop::View::CLayerSurface &layer,
            const SSurfaceState &state) {
            // Mirror getTexBox()/drawSurface() for an untransformed layer root:
            // global animated placement, integer monitor origin, then scaled/rounded.
            using Desktop::View::IGeometric;
            const auto POS = layer.position(IGeometric::GEOMETRIC_CURRENT) -
                Vector2D{std::trunc(monitor.m_position.x), std::trunc(monitor.m_position.y)};
            CBox draw{POS, layer.size(IGeometric::GEOMETRIC_CURRENT)};
            CBox clip{{}, monitor.m_size};
            const double SCALE = monitor.m_scale;
            draw.scale(SCALE).round();
            clip.scale(SCALE).round();
            const auto &output = monitor.m_transformedSize;
            if (draw.w <= 1 || draw.h <= 1 || !positive_size(draw.size()) ||
                draw.pos() != Vector2D{} || draw.size() != output || !covers_output(clip, output)) {
                return std::nullopt;
            }

            const auto &buffer = state.bufferSize;
            // Native fractional-scale v1 changes both the endpoint and sampler
            // for small buffer mismatches. That special path is not this affine.
            if (SCALE != std::floor(SCALE) && state.scale == 1 && draw.size() != buffer &&
                std::abs(draw.w - buffer.x) < 3 && std::abs(draw.h - buffer.y) < 3) {
                return std::nullopt;
            }

            const auto &viewport = state.viewport;
            const bool SCALE_UNAWARE = SCALE != 1.F &&
                (std::ceil(SCALE) == state.scale || !viewport.hasDestination);
            static const auto EXPAND = CConfigValue<Config::INTEGER>("render:expand_undersized_textures");
            if (*EXPAND != 0 && !SCALE_UNAWARE) {
                auto expected = viewport.hasSource ? viewport.source.size() : draw.size();
                if (viewport.hasDestination) {
                    expected = viewport.destination;
                }
                expected = (expected * SCALE).round();
                if (draw.w > expected.x || draw.h > expected.y) {
                    return std::nullopt;
                }
            }

            // The ABI exposes a sampler as well as the affine helper. An explicit
            // viewport subrect does not authorize the hidden parts of its backing
            // texture; do not expose them even if the helper would crop correctly.
            if (viewport.hasSource) {
                CBox source = viewport.source;
                source.scale(state.scale);
                if (source != CBox{{}, buffer}) return std::nullopt;
            }
            return std::array{static_cast<float>(-draw.x / draw.w), static_cast<float>(-draw.y / draw.h),
                static_cast<float>(output.x / draw.w), static_cast<float>(output.y / draw.h)};
        }

        bool source_matches(const CWLSurfaceResource &surface, const Render::ITexture &texture,
            const NColorManagement::PImageDescription &target) {
            // Match native precedence: texture metadata, surface metadata, then
            // the compositor's untagged-source transfer function.
            if (texture.m_imageDescription) {
                return compatible_sdr(texture.m_imageDescription, target);
            }
            if (surface.m_colorManagement) {
                return compatible_sdr(target, target) && surface.m_colorManagement->imageDescription() == target->value();
            }
            return compatible_sdr(NColorManagement::getDefaultImageDescription(), target);
        }

        OriginalWallpaper layer_wallpaper(const PHLMONITOR &monitor, const PHLLS &layer) {
            const auto &rules = layer->m_ruleApplicator;
            if (layer->m_monitor != monitor || layer->m_layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND ||
                !rules || rules->noScreenShare().valueOrDefault() || rules->dimAround().valueOrDefault() ||
                layer->shouldBlur() || !layer->acceptsInput() ||
                layer->alpha()[Desktop::View::LS_ALPHA_FADE]->value() != 1.F ||
                layer->positionAnimation()->isBeingAnimated() || layer->sizeAnimation()->isBeingAnimated() ||
                layer->popupMappedTreeSize() != 0) {
                return {};
            }

            const auto &view = layer->wlSurface();
            const auto SURFACE = view ? view->resource() : nullptr;
            if (!SURFACE || !SURFACE->m_mapped || !SURFACE->m_role ||
                SURFACE->m_role->role() != SURFACE_ROLE_LAYER_SHELL ||
                std::ranges::any_of(SURFACE->m_subsurfaces, [](const auto &child) { return !child.expired(); }) ||
                view->m_alphaModifier != 1.F || view->m_overallOpacity != 1.F ||
                !view->m_visibleRegion.empty() || view->m_hasBackgroundEffect) {
                return {};
            }

            const auto &state = SURFACE->m_current;
            const auto &texture = state.texture;
            // commitState uploads synchronous buffers to a host GL texture and then
            // drops the buffer reference. Never borrow a client's DMA-BUF texture:
            // it would need both acquire-fence and native async release accounting.
            if (state.buffer || !is_wallpaper_texture(texture) || !texture->m_isSynchronous ||
                texture->m_size != state.bufferSize || !positive_size(state.size) ||
                state.transform != WL_OUTPUT_TRANSFORM_NORMAL || state.scale < 1 ||
                state.offset != Vector2D{} || !solid(state) ||
                !source_matches(*SURFACE, *texture, monitor->workBufferImageDescription())) {
                return {};
            }
            const auto UV = layer_uv(*monitor, *layer, state);
            return UV ? OriginalWallpaper{.texture = texture, .uv = *UV, .owner = SURFACE} : OriginalWallpaper{};
        }
    }  // namespace

    OriginalWallpaper::operator bool() const noexcept {
        return static_cast<bool>(texture);
    }

    OriginalWallpaper original_wallpaper(const PHLMONITOR &monitor, std::string_view source_namespace) {
        static const auto XP_MODE = CConfigValue<Config::INTEGER>("render:xp_mode");
        if (!monitor || !monitor->m_output || !monitor->m_enabled || !monitor->m_dpmsStatus ||
            monitor->isMirror() || !g_pSessionLockManager || g_pSessionLockManager->isSessionLocked() ||
            *XP_MODE != 0 || !positive_size(monitor->m_size) || !positive_size(monitor->m_transformedSize) ||
            !(monitor->m_scale > 0.F) || !std::isfinite(monitor->m_scale)) {
            return {};
        }

        const auto &fadeouts = Desktop::fadingOutState();
        if (!fadeouts || std::ranges::any_of(fadeouts->fadeouts(), [&](const auto &fadeout) {
                return fadeout && fadeout->monitor() == monitor &&
                    fadeout->plane() == Desktop::FADEOUT_PLANE_LAYER_BACKGROUND;
            })) {
            return {};
        }

        PHLLS selected;
        for (const auto &entry : monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
            const auto LAYER = entry.lock();
            if (!LAYER || !LAYER->mapped()) {
                continue;
            }
            // The effect executes after all native BACKGROUND roots. It cannot
            // preserve a second root on that plane, even if the root is protected
            // or its namespace differs. Later native planes are not inputs.
            if (source_namespace.empty() || LAYER->m_namespace != source_namespace || selected) {
                return {};
            }
            selected = LAYER;
        }

        if (source_namespace.empty()) {
            return stock_wallpaper(monitor);
        }
        return selected ? layer_wallpaper(monitor, selected) : OriginalWallpaper{};
    }
}  // namespace hyprimate::shader
