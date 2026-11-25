#include <algorithm>
#include <cmath>
#include <cstddef>

#include <numbers>

#include "workspace/planet_hop/PlanetGlRenderer.hpp"
#include "workspace/planet_hop/PlanetGlPrivate.hpp"

namespace hyprimate::workspace {
    namespace {
        constexpr std::array<float, 16> IDENTITY{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        using detail::capture_mix;
    }
    bool PlanetGlRenderer::valid_draw(
        const DrawContext &context, std::span<const PlanetNeighborTextures> neighbors) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto PROGRESS = context.progress;
        if (textures.width <= 0 || textures.height <= 0 || textures.output == 0 ||
            !std::isfinite(PROGRESS) || PROGRESS < 0 || PROGRESS > 1 ||
            !std::isfinite(frame.radius) || !std::isfinite(frame.aspect) || frame.radius <= 0 ||
            frame.aspect <= 0) {
            return false;
        }
        const auto VALID = [&](const PlanetCaptureTextures &capture) {
            if (capture.composite == 0 || capture.wallpaper == 0 ||
                capture.composite == textures.output || capture.wallpaper == textures.output ||
                capture.previous_wallpaper == textures.output) {
                return false;
            }
            const auto LAYER_VALID = [&](const PlanetLayerTextures &layer) {
                return layer.image != textures.output && layer.backdrop != textures.output &&
                       layer.previous_image != textures.output &&
                       layer.previous_backdrop != textures.output;
            };
            if (!LAYER_VALID(capture.bottom) || !LAYER_VALID(capture.windows) ||
                !LAYER_VALID(capture.top)) {
                return false;
            }
            return std::ranges::all_of(
                capture.floating, [&](const auto &item) { return LAYER_VALID(item.layer); });
        };
        if (!VALID(textures.source) || !VALID(textures.destination)) {
            return false;
        }
        if (std::ranges::any_of(neighbors, [&](const auto &neighbor) {
                return neighbor.image == textures.output ||
                       neighbor.previous_image == textures.output;
            })) {
            return false;
        }
        return true;
    }
    void PlanetGlRenderer::draw_endpoint(const DrawContext &context) {
        const auto &textures = context.textures.get();
        const auto PROGRESS = context.progress;
        const auto &full_screen = context.full_screen.get();
        const auto &capture = PROGRESS == 0 ? textures.source : textures.destination;
        if (!textures.native_hud) {
            draw_quad({.primary = {.image = capture.composite, .backdrop = 0},
                .view_projection = IDENTITY,
                .model = full_screen,
                .opacity = 1,
                .rect = {0, 0, 1, 1},
                .force_opaque = true});
        } else {
            // Never blit a composite that may contain a baked shell. Restore
            // only the native desktop layers; the monitor pass replays HUD.
            draw_quad({.primary = {.image = capture.wallpaper},
                .view_projection = IDENTITY,
                .model = full_screen,
                .opacity = 1,
                .rect = {0, 0, 1, 1},
                .force_opaque = true});
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            draw_quad(
                {.primary = {.image = capture.windows.image, .backdrop = capture.windows.backdrop},
                    .view_projection = IDENTITY,
                    .model = full_screen,
                    .opacity = 1});
            for (const auto &item : capture.floating) {
                auto model = IDENTITY;
                model[0] = 2 * item.rect[2];
                model[5] = 2 * item.rect[3];
                model[12] = (2 * item.rect[0]) + item.rect[2] - 1;
                model[13] =
                    (textures.top_first ? 1.F : -1.F) * ((2 * item.rect[1]) + item.rect[3] - 1);
                auto rect = item.cropped ? std::array<float, 4>{0, 0, 1, 1} : item.rect;
                if (!item.cropped && textures.top_first) {
                    rect[1] = 1 - rect[1] - rect[3];
                }
                draw_quad({.primary = {.image = item.layer.image, .backdrop = item.layer.backdrop},
                    .view_projection = IDENTITY,
                    .model = model,
                    .opacity = std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.F, 1.F) : 0,
                    .rect = rect});
            }
        }
    }
    PlanetGlRenderer::PreviewSamplers::~PreviewSamplers() {
        if (image) {
            glBindSampler(0, regular);
        }
        if (previous) {
            glBindSampler(3, regular);
        }
    }
    PlanetGlRenderer::NeighborView PlanetGlRenderer::neighbor_view(const DrawContext &context) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto PROGRESS = context.progress;
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        NeighborView result;
        result.radius = static_cast<float>(frame.radius) * settings.solar_background_scale;
        const double BOUND =
            result.radius + (settings.depth_enabled ? std::abs(settings.depth_strength) : 0);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            for (std::size_t side = 0; side < 2; ++side) {
                const float SIGN = side == 0 ? -1.F : 1.F;
                auto &plane = result.planes[(axis * 2) + side];
                for (std::size_t column = 0; column < 4; ++column) {
                    plane[column] = view_projection[(column * 4) + 3] +
                                    (SIGN * view_projection[(column * 4) + axis]);
                }
                plane[4] = BOUND * std::hypot(plane[0], plane[1], plane[2]);
            }
        }
        result.w_radius =
            BOUND * std::hypot(view_projection[3], view_projection[7], view_projection[11]);
        const double SCALE_X =
            std::hypot(view_projection[0], view_projection[4], view_projection[8]) *
            static_cast<float>(textures.width);
        const double SCALE_Y =
            std::hypot(view_projection[1], view_projection[5], view_projection[9]) *
            static_cast<float>(textures.height);
        result.pixel_scale = BOUND * std::max(SCALE_X, SCALE_Y);
        result.envelope = static_cast<float>(std::clamp(frame.endpoint_envelope, 0., 1.));
        result.limit = settings.rotation_enabled && std::isfinite(settings.rotation_limit)
                           ? std::clamp(settings.rotation_limit, 0.F, .5F)
                           : 0.F;
        result.motion = .08F * static_cast<float>(std::sin(PROGRESS * std::numbers::pi));
        return result;
    }
    int PlanetGlRenderer::neighbor_quality(const DrawContext &context,
        const NeighborView &view,
        const PlanetNeighborTextures &neighbor) {
        const auto &textures = context.textures.get();
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        if (neighbor.endpoint.identity == textures.source_identity ||
            neighbor.endpoint.identity == textures.destination_identity) {
            return -1;
        }
        const auto &center = neighbor.center;
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
            return -1;
        }
        if (std::ranges::any_of(view.planes, [&](const auto &plane) {
                return (plane[0] * center.x) + (plane[1] * center.y) + (plane[2] * center.z) +
                           plane[3] <
                       -plane[4];
            })) {
            return -1;
        }
        const double CLIP_W = (view_projection[3] * center.x) + (view_projection[7] * center.y) +
                              (view_projection[11] * center.z) + view_projection[15];
        const double PIXELS = view.pixel_scale / std::max(CLIP_W - view.w_radius, 0.000001);
        if (PIXELS < 2) {
            return -1;
        }
        if (PIXELS < 96) {
            return 0;
        }
        const int PIXEL_QUALITY = PIXELS < 256 ? 1 : 2;
        return std::min(std::clamp(settings.quality, 0, 2), PIXEL_QUALITY);
    }
    bool PlanetGlRenderer::draw_neighbor(const DrawContext &context,
        const NeighborView &view,
        const PlanetNeighborTextures &neighbor,
        int quality,
        PreviewSamplers &sampling) {
        if (!prepare_mesh(quality)) {
            return false;
        }
        const auto PLACEMENT = neighbor.endpoint.placement;
        const float PHASE =
            (static_cast<float>((PLACEMENT * 11400714819323198485ULL) >> 56U) / 255.F) - .5F;
        const float ANGLE = view.limit * view.envelope * ((.3F * PHASE) + view.motion);
        const float COSINE = std::cos(ANGLE);
        const float SINE = std::sin(ANGLE);
        const std::array<float, 9> ROTATION{COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        const float MIX =
            std::isfinite(neighbor.live_mix) ? std::clamp(neighbor.live_mix, 0.F, 1.F) : 1.F;
        const float SHARED = neighbor.shared_wallpaper || neighbor.image == 0 ? 1.F : 0.F;
        const float PREVIOUS_SHARED = neighbor.previous_image == 0 ? 1.F : 0.F;
        const float IDENTITY_SHADE =
            MIX < 1 ? (PREVIOUS_SHARED * (1 - MIX)) + (SHARED * MIX) : SHARED;
        const float TINT = (.04F + (.04F * IDENTITY_SHADE)) * view.envelope;
        std::array<float, 3> tint{};
        tint[static_cast<std::size_t>(PLACEMENT % 3)] = -TINT;
        tint[static_cast<std::size_t>(((PLACEMENT / 3) + 1) % 3)] -= TINT * .5F;
        if ((neighbor.image == 0 || (MIX < 1 && neighbor.previous_image == 0)) &&
            !prepare_neutral()) {
            return false;
        }
        const GLuint IMAGE = neighbor.image != 0 ? neighbor.image : m_neutral_texture;
        const GLuint PREVIOUS_IMAGE =
            neighbor.previous_image != 0 ? neighbor.previous_image : m_neutral_texture;
        const GLuint PREVIOUS = MIX < 1 ? PREVIOUS_IMAGE : 0;
        const bool IMAGE_MIPPED = neighbor.image != 0 && neighbor.image_mipped;
        const bool PREVIOUS_MIPPED =
            PREVIOUS != 0 && neighbor.previous_image != 0 && neighbor.previous_mipped;
        if (sampling.image != IMAGE_MIPPED) {
            glBindSampler(0, IMAGE_MIPPED ? m_mip_sampler : m_sampler);
            sampling.image = IMAGE_MIPPED;
        }
        if (sampling.previous != PREVIOUS_MIPPED) {
            glBindSampler(3, PREVIOUS_MIPPED ? m_mip_sampler : m_sampler);
            sampling.previous = PREVIOUS_MIPPED;
        }
        const double OWNER_ASPECT =
            std::isfinite(neighbor.endpoint.monitor_width) &&
                    std::isfinite(neighbor.endpoint.monitor_height) &&
                    neighbor.endpoint.monitor_width > 0 && neighbor.endpoint.monitor_height > 0
                ? neighbor.endpoint.monitor_width / neighbor.endpoint.monitor_height
                : 0;
        draw_surface(context,
            {.capture = context.textures.get().source,
                .center = neighbor.center,
                .rotation = ROTATION,
                .envelope = 1,
                .layer = {.image = IMAGE,
                    .backdrop = 0,
                    .previous_image = PREVIOUS,
                    .previous_backdrop = 0,
                    .live_mix = MIX},
                .kind = 0,
                .elevation = 0,
                .opacity = 1,
                .radius = view.radius,
                .quality = quality,
                .tint = tint,
                .aspect = static_cast<float>(OWNER_ASPECT)});
        return true;
    }
    bool PlanetGlRenderer::draw_neighbors(
        const DrawContext &context, std::span<const PlanetNeighborTextures> neighbors) {
        if (neighbors.empty() || !std::isfinite(context.settings.get().solar_background_scale) ||
            context.settings.get().solar_background_scale <= 0) {
            return true;
        }
        PreviewSamplers sampling{m_sampler};
        const NeighborView VIEW = neighbor_view(context);
        for (const auto &neighbor : neighbors) {
            const int QUALITY = neighbor_quality(context, VIEW, neighbor);
            if (QUALITY >= 0 && !draw_neighbor(context, VIEW, neighbor, QUALITY, sampling)) {
                return false;
            }
        }
        return prepare_mesh(context.settings.get().quality);
    }
    void PlanetGlRenderer::draw_floating(const DrawContext &context,
        const EndpointDraw &endpoint,
        const PlanetFloatingTextures &item) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        const auto &center = endpoint.center;
        const auto &rotation = endpoint.rotation.get();
        const auto ENVELOPE = endpoint.envelope;
        const float RELIEF = settings.depth_enabled ? std::abs(settings.depth_strength) * 0.5F : 0;
        const double COORD_U = item.rect[0] + (item.rect[2] * 0.5);
        const double COORD_V = item.rect[1] + (item.rect[3] * 0.5);
        const auto ANCHOR = planet_hop::project_surface({.u = COORD_U,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = RELIEF + std::max(0.0F, settings.elevation)});
        const auto EAST = planet_hop::project_surface({.u = COORD_U + 0.001,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = 0});
        const auto WEST = planet_hop::project_surface({.u = COORD_U - 0.001,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = 0});
        planet_hop::Vec3 right{.x = EAST.position.x - WEST.position.x,
            .y = EAST.position.y - WEST.position.y,
            .z = EAST.position.z - WEST.position.z};
        const double LENGTH =
            std::sqrt((right.x * right.x) + (right.y * right.y) + (right.z * right.z));
        if (LENGTH <= 0.000001) {
            return;
        }
        right = {.x = right.x / LENGTH, .y = right.y / LENGTH, .z = right.z / LENGTH};
        const auto &normal = ANCHOR.normal;
        const planet_hop::Vec3 UP_AXIS{.x = (normal.y * right.z) - (normal.z * right.y),
            .y = (normal.z * right.x) - (normal.x * right.z),
            .z = (normal.x * right.y) - (normal.y * right.x)};
        auto model = IDENTITY;
        const float WIDTH = 2 * static_cast<float>(frame.aspect) * item.rect[2];
        const float HEIGHT = 2 * item.rect[3];
        model[0] = static_cast<float>(right.x) * WIDTH;
        model[1] = static_cast<float>(right.y) * WIDTH;
        model[2] = static_cast<float>(right.z) * WIDTH;
        model[4] = static_cast<float>(UP_AXIS.x) * HEIGHT;
        model[5] = static_cast<float>(UP_AXIS.y) * HEIGHT;
        model[6] = static_cast<float>(UP_AXIS.z) * HEIGHT;
        model[12] = static_cast<float>(ANCHOR.position.x);
        model[13] = static_cast<float>(ANCHOR.position.y);
        model[14] = static_cast<float>(ANCHOR.position.z);
        const bool PREVIOUS = capture_mix(item.layer) < 1;
        const PlanetLayerTextures PRIMARY =
            PREVIOUS ? PlanetLayerTextures{.image = item.layer.previous_image,
                           .backdrop = item.layer.previous_backdrop}
                     : item.layer;
        const PlanetLayerTextures SECONDARY{
            .image = item.layer.image, .backdrop = item.layer.backdrop};
        draw_quad({.primary = PRIMARY,
            .view_projection = view_projection,
            .model = model,
            .opacity = std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.0F, 1.0F) : 0,
            .rect = item.cropped ? std::array<float, 4>{0, 0, 1, 1} : item.rect,
            .force_opaque = false,
            .secondary = SECONDARY,
            .mix = PREVIOUS ? capture_mix(item.layer) : 0,
            .top_first = textures.top_first});
    }
    void PlanetGlRenderer::draw_layers(const DrawContext &context, const EndpointDraw &endpoint) {
        const auto &textures = context.textures.get();
        const auto &settings = context.settings.get();
        const auto &capture = endpoint.capture.get();
        const auto &center = endpoint.center;
        const auto &rotation = endpoint.rotation.get();
        const auto ENVELOPE = endpoint.envelope;
        const auto SOURCE = endpoint.source;
        const float OVERLAY_OPACITY =
            settings.overlay_mode == 2 ? fade_opacity(context, SOURCE) : 1;
        if (settings.overlay_mode != 1 && !textures.native_hud) {
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.bottom,
                    .kind = 2,
                    .elevation = 0.003F,
                    .opacity = OVERLAY_OPACITY});
        }
        if (settings.window_mode != 1) {
            const float OPACITY = settings.window_mode == 2 ? fade_opacity(context, SOURCE) : 1;
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.windows,
                    .kind = 1,
                    .elevation = 0.006F,
                    .opacity = OPACITY});
        }
        for (const auto &item : capture.floating) {
            draw_floating(context, endpoint, item);
        }
        if (settings.overlay_mode != 1 && !textures.native_hud) {
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.top,
                    .kind = 2,
                    .elevation = std::max(0.0F, settings.elevation) + 0.01F,
                    .opacity = OVERLAY_OPACITY});
        }
    }
    void PlanetGlRenderer::draw_hud(const DrawContext &context, bool top) {
        const auto &source =
            top ? context.textures.get().source.top : context.textures.get().source.bottom;
        const auto &destination = top ? context.textures.get().destination.top
                                      : context.textures.get().destination.bottom;
        glDisable(GL_DEPTH_TEST);
        draw_quad({.primary = source,
            .view_projection = IDENTITY,
            .model = context.full_screen,
            .opacity = 1,
            .rect = {0, 0, 1, 1},
            .force_opaque = false,
            .secondary = destination,
            .mix = static_cast<float>(context.progress)});
        glEnable(GL_DEPTH_TEST);
    }
    float PlanetGlRenderer::fade_opacity(const DrawContext &context, bool source) {
        const float WIDTH = std::isfinite(context.settings.get().fade)
                                ? std::clamp(context.settings.get().fade, 0.02F, 1.0F)
                                : 0.2F;
        const auto DISTANCE = static_cast<float>(source ? context.progress : 1 - context.progress);
        const float AMOUNT = std::clamp(DISTANCE / WIDTH, 0.0F, 1.0F);
        return 1.0F - (AMOUNT * AMOUNT * (3.0F - (2.0F * AMOUNT)));
    }
    bool PlanetGlRenderer::draw_raw(const PlanetFrameTextures &textures,
        const planet_hop::Frame &frame,
        double progress,
        const PlanetRenderSettings &settings,
        std::span<const PlanetNeighborTextures> neighbors) {
        std::array<float, 16> reflected_projection{};
        std::array<float, 16> reflected_inverse{};
        auto full_screen = IDENTITY;
        const DrawContext CONTEXT{.textures = textures,
            .frame = frame,
            .progress = progress,
            .settings = settings,
            .view_projection = textures.top_first ? reflected_projection : frame.view_projection,
            .inverse_projection =
                textures.top_first ? reflected_inverse : frame.inverse_view_projection,
            .full_screen = full_screen};
        if (m_planet.id == 0 || !valid_draw(CONTEXT, neighbors) || !bind_output(textures)) {
            return false;
        }
        struct DepthState {
            GLint function = 0;
            GLboolean mask = GL_TRUE;
            GLfloat clear = 1;
            DepthState() {
                glGetIntegerv(GL_DEPTH_FUNC, &function);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
                glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear);
            }
            DepthState(const DepthState &) = delete;
            DepthState &operator=(const DepthState &) = delete;
            DepthState(DepthState &&) = delete;
            DepthState &operator=(DepthState &&) = delete;
            ~DepthState() {
                glDepthFunc(static_cast<GLenum>(function));
                glDepthMask(mask);
                glClearDepthf(clear);
            }
        };
        const DepthState DEPTH_STATE;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        full_screen[0] = full_screen[5] = 2;
        if (progress == 0 || progress == 1) {
            draw_endpoint(CONTEXT);
            return true;
        }
        // Custom depth is optional: failed uploads retain pixels for retry and use
        // procedural depth, never the stale image from a previous setting.
        (void)upload_depth();
        if (!prepare_mesh(settings.quality)) {
            return false;
        }
        if (textures.top_first) {
            reflected_projection = frame.view_projection;
            reflected_inverse = frame.inverse_view_projection;
            for (std::size_t column = 0; column < 4; ++column) {
                reflected_projection[(column * 4) + 1] *= -1;
            }
            for (std::size_t row = 0; row < 4; ++row) {
                reflected_inverse[4 + row] *= -1;
            }
        }
        draw_sky(CONTEXT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glClearDepthf(1);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (!draw_neighbors(CONTEXT, neighbors)) {
            return false;
        }
        draw_surface(CONTEXT,
            {.capture = textures.source,
                .center = frame.source_center,
                .rotation = frame.source_rotation,
                .envelope = static_cast<float>(frame.source_curvature),
                .layer = {.image = textures.source.wallpaper,
                    .backdrop = 0,
                    .previous_image = textures.source.previous_wallpaper,
                    .previous_backdrop = 0,
                    .live_mix = textures.source.live_mix},
                .kind = 0,
                .elevation = 0,
                .opacity = 1});
        draw_surface(CONTEXT,
            {.capture = textures.destination,
                .center = frame.destination_center,
                .rotation = frame.destination_rotation,
                .envelope = static_cast<float>(frame.destination_curvature),
                .layer = {.image = textures.destination.wallpaper,
                    .backdrop = 0,
                    .previous_image = textures.destination.previous_wallpaper,
                    .previous_backdrop = 0,
                    .live_mix = textures.destination.live_mix},
                .kind = 0,
                .elevation = 0,
                .opacity = 1});
        glDepthMask(GL_FALSE);
        if (settings.overlay_mode == 1 && !textures.native_hud) {
            draw_hud(CONTEXT, false);
        }
        draw_layers(CONTEXT,
            {.capture = textures.source,
                .center = frame.source_center,
                .rotation = frame.source_rotation,
                .envelope = static_cast<float>(frame.source_curvature),
                .source = true});
        draw_layers(CONTEXT,
            {.capture = textures.destination,
                .center = frame.destination_center,
                .rotation = frame.destination_rotation,
                .envelope = static_cast<float>(frame.destination_curvature),
                .source = false});
        if (settings.overlay_mode == 1 && !textures.native_hud) {
            draw_hud(CONTEXT, true);
        }
        return true;
    }
}  // namespace hyprimate::workspace
