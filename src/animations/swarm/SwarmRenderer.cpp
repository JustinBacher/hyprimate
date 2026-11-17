#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <GLES3/gl32.h>
#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>

#include "animations/shared/Core.hpp"
#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "animations/swarm/SwarmSprite.hpp"
#include "animations/swarm/SwarmSurface.hpp"
#include "animations/swarm/SwarmSurfaceBody.hpp"

using hyprimate::Vec2;

namespace {

    // Atlas layout, px: a row of cells, each holding one frame with a transparent gutter around it.
    // A solid cell completes merged tiles; a disc rounds detached, shrinking pieces.
    constexpr int FRAME_PX = 192;
    constexpr int GUTTER_PX = 2;
    constexpr int CELL_PX = FRAME_PX + (2 * GUTTER_PX);
    constexpr int SHAPE_CELLS = 2;
    // Frame-space margin a mask quad may reach past its frame: half the gutter, so filtering never
    // reaches a neighbouring cell.
    constexpr double FRAME_MARGIN = 0.5 * GUTTER_PX / FRAME_PX;
    // Px between the two halves of the layer, so filtering one never reaches the other.
    constexpr double LAYER_GAP = 2.0;

    // Decodes the sheet, resamples its frames into the atlas and measures them. False when the
    // sheet cannot be decoded.
    bool bake(const std::string &creature,
        CSwarmSprites::SSheet &sheet,
        std::vector<std::uint8_t> &pixels) {
        const auto PATH = hyprimate::sprite_path(
            hyprimate::SpriteName{creature}, hyprimate::SpriteDirectory{HYPRIMATE_SPRITE_DIR});
        cairo_surface_t *image = cairo_image_surface_create_from_png(PATH.c_str());
        if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(image);
            return false;
        }

        constexpr int COLUMNS = 4;
        constexpr int ROWS = 2;
        constexpr int FRAMES = COLUMNS * ROWS;
        const int WIDTH = (FRAMES + SHAPE_CELLS) * CELL_PX;
        cairo_surface_t *atlas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, CELL_PX);
        cairo_t *context = cairo_create(atlas);
        const double FRAME_W = static_cast<double>(cairo_image_surface_get_width(image)) / COLUMNS;
        const double FRAME_H = static_cast<double>(cairo_image_surface_get_height(image)) / ROWS;
        for (int k = 0; k < FRAMES; ++k) {
            const double FRAME_X = (k * CELL_PX) + GUTTER_PX;
            cairo_save(context);
            cairo_rectangle(context, FRAME_X, GUTTER_PX, FRAME_PX, FRAME_PX);
            cairo_clip(context);
            cairo_translate(context, FRAME_X, GUTTER_PX);
            cairo_scale(context, FRAME_PX / FRAME_W, FRAME_PX / FRAME_H);
            const int FRAME_ROW = -(k / COLUMNS);
            cairo_set_source_surface(
                context, image, -(k % COLUMNS) * FRAME_W, static_cast<double>(FRAME_ROW) * FRAME_H);
            cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_BEST);
            cairo_paint(context);
            cairo_restore(context);
        }
        const double SHAPES = FRAMES * CELL_PX;
        cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 1.0);
        cairo_rectangle(context, SHAPES, 0.0, CELL_PX, CELL_PX);
        cairo_fill(context);
        cairo_arc(context,
            SHAPES + (CELL_PX * 1.5),
            CELL_PX * 0.5,
            0.5 * FRAME_PX,
            0.0,
            2.0 * std::numbers::pi);
        cairo_fill(context);
        cairo_destroy(context);
        cairo_surface_flush(atlas);
        const bool DRAWN = cairo_surface_status(atlas) == CAIRO_STATUS_SUCCESS;

        if (DRAWN) {
            // Premultiplied ARGB32 in native byte order: B, G, R, A in memory, as
            // DRM_FORMAT_ARGB8888 wants.
            const unsigned char *data = cairo_image_surface_get_data(atlas);
            const int STRIDE = cairo_image_surface_get_stride(atlas);
            pixels.resize(static_cast<std::size_t>(WIDTH) * CELL_PX * 4);
            auto source_rows = std::span{data, static_cast<std::size_t>(STRIDE) * CELL_PX};
            auto target_rows = std::span{pixels};
            for (int atlas_row = 0; atlas_row < CELL_PX; ++atlas_row) {
                std::memcpy(
                    target_rows.data(), source_rows.data(), static_cast<std::size_t>(WIDTH) * 4);
                source_rows = source_rows.subspan(STRIDE);
                target_rows = target_rows.subspan(static_cast<std::size_t>(WIDTH) * 4);
            }

            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(FRAME_PX) * FRAME_PX);
            sheet.frames.clear();
            sheet.core_share = 1.0;
            for (int k = 0; k < FRAMES; ++k) {
                auto target = alpha.begin();
                auto atlas_rows =
                    std::span{pixels}.subspan(static_cast<std::size_t>(GUTTER_PX) * WIDTH * 4);
                for (int row_index = 0; row_index < FRAME_PX; ++row_index) {
                    const auto PIXEL_ROW = atlas_rows.subspan(
                        ((static_cast<std::size_t>(k) * CELL_PX) + GUTTER_PX) * 4,
                        static_cast<std::size_t>(FRAME_PX) * 4);
                    auto pixel = PIXEL_ROW.begin();
                    for (int col_index = 0; col_index < FRAME_PX; ++col_index) {
                        *target++ = *std::next(pixel, 3);
                        std::advance(pixel, 4);
                    }
                    atlas_rows = atlas_rows.subspan(static_cast<std::size_t>(WIDTH) * 4);
                }
                sheet.frames.push_back(hyprimate::measure_sprite(alpha, FRAME_PX));
                const auto &frame = sheet.frames.back();
                sheet.core_share =
                    std::min(sheet.core_share, frame.reach > 0.0 ? frame.core / frame.reach : 0.0);
            }
            sheet.forward = -0.5 * std::numbers::pi;
            sheet.stride = 1.0;
        }
        cairo_surface_destroy(atlas);
        cairo_surface_destroy(image);
        return DRAWN;
    }

    void push_quad(std::vector<SMeshRenderVertex> &out,
        const SMeshRenderVertex &top_left,
        const SMeshRenderVertex &bottom_left,
        const SMeshRenderVertex &top_right,
        const SMeshRenderVertex &bottom_right) {
        out.push_back(top_left);
        out.push_back(bottom_left);
        out.push_back(top_right);
        out.push_back(top_right);
        out.push_back(bottom_left);
        out.push_back(bottom_right);
    }

    void push_rect(std::vector<SMeshRenderVertex> &out,
        double left,
        double top,
        double right,
        double bottom,
        double left_u,
        double top_v,
        double right_u,
        double bottom_v) {
        const auto VERTEX = [](double pos_x, double pos_y, double tex_u, double tex_v) {
            return SMeshRenderVertex{
                .x = static_cast<float>(pos_x),
                .y = static_cast<float>(pos_y),
                .u = static_cast<float>(tex_u),
                .v = static_cast<float>(tex_v),
            };
        };
        push_quad(out,
            VERTEX(left, top, left_u, top_v),
            VERTEX(left, bottom, left_u, bottom_v),
            VERTEX(right, top, right_u, top_v),
            VERTEX(right, bottom, right_u, bottom_v));
    }

    struct SMaskPoint {
        double x = 0.0, y = 0.0, u = 0.0, v = 0.0;
    };

    // Clips a convex polygon of `count` points to an axis-aligned box, in place, carrying its
    // texture coordinates along. Returns the new count; a quad clipped to a box has at most eight
    // points.
    std::size_t clip_to_box(std::array<SMaskPoint, 8> &polygon,
        std::size_t count,
        double left,
        double top,
        double right,
        double bottom) {
        // One pass keeps the points on one side of one of the box's edges.
        const auto PASS = [&](bool along_x, double limit, bool keep_below) {
            std::array<SMaskPoint, 8> kept;
            std::size_t kept_count = 0;
            auto kept_it = std::span{kept}.begin();
            const auto VALUE = [&](const SMaskPoint &point) { return along_x ? point.x : point.y; };
            const auto INSIDE = [&](const SMaskPoint &point) {
                return keep_below ? VALUE(point) <= limit : VALUE(point) >= limit;
            };
            const auto POINTS = std::span(polygon).first(count);
            for (auto point = POINTS.begin(); point != POINTS.end(); ++point) {
                const SMaskPoint &start_point = *point;
                const auto NEXT_POINT = std::next(point);
                const SMaskPoint &end_point =
                    NEXT_POINT == POINTS.end() ? POINTS.front() : *NEXT_POINT;
                if (INSIDE(start_point) && kept_count < kept.size()) {
                    *kept_it++ = start_point;
                    ++kept_count;
                }
                if (INSIDE(start_point) != INSIDE(end_point) && kept_count < kept.size()) {
                    const double FRACTION =
                        (limit - VALUE(start_point)) / (VALUE(end_point) - VALUE(start_point));
                    *kept_it++ = {
                        .x = start_point.x + ((end_point.x - start_point.x) * FRACTION),
                        .y = start_point.y + ((end_point.y - start_point.y) * FRACTION),
                        .u = start_point.u + ((end_point.u - start_point.u) * FRACTION),
                        .v = start_point.v + ((end_point.v - start_point.v) * FRACTION),
                    };
                    ++kept_count;
                }
            }
            polygon = kept;
            count = kept_count;
        };
        PASS(true, left, false);
        PASS(true, right, true);
        PASS(false, top, false);
        PASS(false, bottom, true);
        return count;
    }

}  // namespace

CSwarmSprites::SEntry &CSwarmSprites::entry(const std::string &creature) {
    SEntry &sprite_entry = m_entries[creature];
    if (!sprite_entry.tried) {
        sprite_entry.tried = true;
        if (!bake(creature, sprite_entry.sheet, sprite_entry.pixels)) {
            sprite_entry.sheet.frames.clear();
            sprite_entry.pixels.clear();
        }
    }
    return sprite_entry;
}

const CSwarmSprites::SSheet *CSwarmSprites::sheet(const std::string &creature) {
    const SEntry &sprite_entry = entry(creature);
    return sprite_entry.sheet.frames.empty() ? nullptr : &sprite_entry.sheet;
}

SP<Render::ITexture> CSwarmSprites::texture(const std::string &creature) {
    SEntry &sprite_entry = entry(creature);
    if (sprite_entry.texture || sprite_entry.pixels.empty() || !g_pHyprRenderer) {
        return sprite_entry.texture;
    }
    const int WIDTH = (static_cast<int>(sprite_entry.sheet.frames.size()) + SHAPE_CELLS) * CELL_PX;
    sprite_entry.texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        sprite_entry.pixels.data(),
        WIDTH * 4,
        Vector2D{static_cast<double>(WIDTH), static_cast<double>(CELL_PX)},
        false,
        false);
    if (sprite_entry.texture) {
        sprite_entry.pixels = {};
    }
    return sprite_entry.texture;
}

CBox swarm_layer_box(const CBox &source) {
    const double LEFT_EDGE = std::floor(source.x);
    const double TOP_EDGE = std::floor(source.y);
    return CBox{LEFT_EDGE,
        TOP_EDGE,
        std::ceil(source.x + source.w) - LEFT_EDGE,
        std::ceil(source.y + source.h) - TOP_EDGE};
}

Vector2D swarm_layer_size(const CBox &source) {
    const CBox BOX = swarm_layer_box(source);
    return Vector2D{(2.0 * BOX.w) + LAYER_GAP, BOX.h};
}

void fill_swarm_meshes(const hyprimate::Swarm &swarm,
    const CSwarmSprites::SSheet &sheet,
    const CBox &source,
    const CBox &output,
    Vec2 texture_origin,
    Vec2 texture_size,
    SSwarmMeshes &meshes) {
    meshes.mask.clear();
    meshes.creatures.clear();
    meshes.walking_cut.clear();
    meshes.content.clear();
    meshes.pieces.clear();
    meshes.bodies.clear();
    meshes.standing_at = 0.0;
    meshes.smoothing_radius = 0.0;
    meshes.corner_radius = 0.0;

    const auto AGENTS = swarm.agents();
    const Vec2 SIZE = swarm.size();
    const int FRAMES = static_cast<int>(sheet.frames.size());
    if (AGENTS.empty() || FRAMES == 0 || !(SIZE.x > 0.0) || !(SIZE.y > 0.0) || source.empty() ||
        output.empty()) {
        return;
    }

    const CBox BOX = swarm_layer_box(source);
    const double SCALE_X = source.w / SIZE.x;
    const double SCALE_Y = source.h / SIZE.y;
    const double UNIT = std::min(SCALE_X, SCALE_Y);
    // The layer: walking creatures in its left half, everything standing in the window in its
    // right.
    const double WIDTH = (2.0 * BOX.w) + LAYER_GAP;
    const double STANDING_AT = BOX.w + LAYER_GAP;
    const double ATLAS_W = (FRAMES + SHAPE_CELLS) * CELL_PX;
    const double SOLID_U = (FRAMES + 0.5) * CELL_PX / ATLAS_W;
    meshes.standing_at = STANDING_AT;

    // Layer and output coordinates of a device-px point.
    const auto LAYER_X = [&](double pos_x, bool standing) {
        return (pos_x - BOX.x + (standing ? STANDING_AT : 0.0)) / WIDTH;
    };
    const auto LAYER_Y = [&](double pos_y) { return (pos_y - BOX.y) / BOX.h; };
    const auto OUTPUT_X = [&](double pos_x) { return (pos_x - output.x) / output.w; };
    const auto OUTPUT_Y = [&](double pos_y) { return (pos_y - output.y) / output.h; };

    // The window, once under each half.
    const double LEFT_U = (BOX.x - texture_origin.x) / texture_size.x;
    const double RIGHT_U = (BOX.x + BOX.w - texture_origin.x) / texture_size.x;
    const double TOP_V = (BOX.y - texture_origin.y) / texture_size.y;
    const double BOTTOM_V = (BOX.y + BOX.h - texture_origin.y) / texture_size.y;
    push_rect(meshes.content, 0.0, 0.0, BOX.w / WIDTH, 1.0, LEFT_U, TOP_V, RIGHT_U, BOTTOM_V);
    push_rect(meshes.content, STANDING_AT / WIDTH, 0.0, 1.0, 1.0, LEFT_U, TOP_V, RIGHT_U, BOTTOM_V);

    // An agent stands in the window once it is home and has swung round; until then it is a walking
    // piece.
    const auto STANDING = [](const hyprimate::SwarmAgent &agent) {
        return agent.docked && agent.turn <= 0.0;
    };
    const bool HAS_STANDING = std::ranges::any_of(
        AGENTS, [&](const auto &agent) { return agent.visible && STANDING(agent); });
    if (HAS_STANDING && swarm.rounding() > 0.0) {
        const auto SMALLEST = std::ranges::min_element(AGENTS, {}, &hyprimate::SwarmAgent::fit);
        // Keep joins smooth from the first detachment. Only the outer rounding relaxes;
        // shrinking the union length would briefly cut square holes around departing agents.
        meshes.smoothing_radius = std::min(20.0, 0.4 * SMALLEST->fit * UNIT);
        meshes.corner_radius =
            hyprimate::K_SWARM_SURFACE_CORNER_SCALE * meshes.smoothing_radius * swarm.rounding();
        meshes.bodies.reserve(AGENTS.size());
    }
    // What stands in the window is drawn first and in one piece, so it has no seams; the walkers go
    // over it.
    if (HAS_STANDING) {
        push_rect(meshes.pieces,
            OUTPUT_X(BOX.x),
            OUTPUT_Y(BOX.y),
            OUTPUT_X(BOX.x + BOX.w),
            OUTPUT_Y(BOX.y + BOX.h),
            STANDING_AT / WIDTH,
            0.0,
            1.0,
            1.0);
    }

    for (const hyprimate::SwarmAgent &creature_agent : AGENTS) {
        const double TILE_LEFT = source.x + (creature_agent.tile.x * SCALE_X);
        const double TILE_RIGHT =
            source.x + ((creature_agent.tile.x + creature_agent.tile.w) * SCALE_X);
        const double TILE_TOP = source.y + (creature_agent.tile.y * SCALE_Y);
        const double TILE_BOTTOM =
            source.y + ((creature_agent.tile.y + creature_agent.tile.h) * SCALE_Y);
        const double HOME_X = source.x + (creature_agent.home.x * SCALE_X);
        const double HOME_Y = source.y + (creature_agent.home.y * SCALE_Y);
        const bool STANDS = STANDING(creature_agent);

        if (!creature_agent.visible) {
            continue;
        }

        // The silhouette keeps its travel heading while the carried tile straightens beneath it.
        // Counter-rotate the mask by the piece transform, including throughout docking.
        const auto SILHOUETTE = [&](std::vector<SMeshRenderVertex> &out, double lock) {
            const int FRAME_INDEX = hyprimate::walk_frame(
                creature_agent.walk, sheet.stride * 2.0 * creature_agent.fit, FRAMES);
            const auto &frame = sheet.frames[static_cast<std::size_t>(FRAME_INDEX)];
            const double SIDE = hyprimate::sprite_size(frame,
                hyprimate::SpriteFit{creature_agent.fit * UNIT},
                hyprimate::SpriteCover{creature_agent.reach * std::max(SCALE_X, SCALE_Y)},
                hyprimate::SpriteLock{lock});
            const double FACING =
                hyprimate::sprite_rotation(creature_agent.heading, sheet.forward, 1.0) -
                hyprimate::sprite_rotation(
                    creature_agent.heading, creature_agent.rest, creature_agent.turn);
            const double FACING_COS = std::cos(FACING);
            const double FACING_SIN = std::sin(FACING);
            // Where a point of the frame lands: the creature's body on the tile's home, turned
            // about it.
            const auto FRAME_POINT = [&](double frame_x, double frame_y) {
                const double OFFSET_X = (frame_x - frame.centre.x) * SIDE;
                const double OFFSET_Y = (frame_y - frame.centre.y) * SIDE;
                return SMaskPoint{
                    .x = HOME_X + (OFFSET_X * FACING_COS) - (OFFSET_Y * FACING_SIN),
                    .y = HOME_Y + (OFFSET_X * FACING_SIN) + (OFFSET_Y * FACING_COS),
                    .u = ((FRAME_INDEX * CELL_PX) + GUTTER_PX + (frame_x * FRAME_PX)) / ATLAS_W,
                    .v = (GUTTER_PX + (frame_y * FRAME_PX)) / CELL_PX,
                };
            };
            const double LOW = -FRAME_MARGIN;
            const double HIGH = 1.0 + FRAME_MARGIN;
            std::array<SMaskPoint, 8> polygon{
                FRAME_POINT(LOW, LOW),
                FRAME_POINT(HIGH, LOW),
                FRAME_POINT(HIGH, HIGH),
                FRAME_POINT(LOW, HIGH),
            };
            const std::size_t POINTS =
                SIDE > 0.0 ? clip_to_box(polygon, 4, TILE_LEFT, TILE_TOP, TILE_RIGHT, TILE_BOTTOM)
                           : 0;
            const auto VERTEX = [&](const SMaskPoint &point) {
                return SMeshRenderVertex{
                    .x = static_cast<float>(LAYER_X(point.x, STANDS)),
                    .y = static_cast<float>(LAYER_Y(point.y)),
                    .u = static_cast<float>(point.u),
                    .v = static_cast<float>(point.v),
                };
            };
            auto point = std::span{polygon}.begin();
            for (std::size_t i = 1; i + 1 < POINTS; ++i) {
                ++point;
                out.push_back(VERTEX(polygon.front()));
                out.push_back(VERTEX(*point));
                out.push_back(VERTEX(*std::next(point)));
            }
        };
        if (creature_agent.lock >= 1.0) {
            push_rect(meshes.mask,
                LAYER_X(TILE_LEFT, STANDS),
                LAYER_Y(TILE_TOP),
                LAYER_X(TILE_RIGHT, STANDS),
                LAYER_Y(TILE_BOTTOM),
                SOLID_U,
                0.5,
                SOLID_U,
                0.5);
        } else if (STANDS) {
            SILHOUETTE(meshes.creatures, 0.0);
        } else {
            SILHOUETTE(meshes.mask, creature_agent.lock);
        }

        if (STANDS) {
            if (creature_agent.lock > 0.0 && meshes.smoothing_radius > 0.0) {
                // Grow a stable body, not the sprite's raster outline. Gait frames stay on top
                // until engulfed, without changing the mass's contour as their limbs move.
                const double LOCK_BLEND =
                    creature_agent.lock * creature_agent.lock * (3.0 - (2.0 * creature_agent.lock));
                const double CORE = sheet.core_share * creature_agent.fit * UNIT;
                const double HALF_W = (TILE_RIGHT - TILE_LEFT) * 0.5;
                const double HALF_H = (TILE_BOTTOM - TILE_TOP) * 0.5;
                const double CORNER = std::min({HALF_W, HALF_H, meshes.corner_radius});
                meshes.bodies.push_back({
                    .centre_x = static_cast<float>(
                        std::lerp(HOME_X, (TILE_LEFT + TILE_RIGHT) * 0.5, LOCK_BLEND) - BOX.x),
                    .centre_y = static_cast<float>(
                        std::lerp(HOME_Y, (TILE_TOP + TILE_BOTTOM) * 0.5, LOCK_BLEND) - BOX.y),
                    .half_width = static_cast<float>(std::lerp(CORE, HALF_W, LOCK_BLEND)),
                    .half_height = static_cast<float>(std::lerp(CORE, HALF_H, LOCK_BLEND)),
                    .corner_radius = static_cast<float>(std::lerp(CORE, CORNER, LOCK_BLEND)),
                });
            }
            continue;
        }

        // Detached pieces are rounded on their own; only the standing mass uses union smoothing.
        // This keeps a closing tile from briefly becoming a sharp rectangle before its sprite
        // shrinks away from the tile's boundaries.
        if (creature_agent.lock > 0.0) {
            const double RADIUS = creature_agent.fit * UNIT;
            const double DISC_U = (FRAMES + 1.5) * CELL_PX / ATLAS_W;
            const double RADIUS_U = 0.5 * FRAME_PX / ATLAS_W;
            const double RADIUS_V = 0.5 * FRAME_PX / CELL_PX;
            for (unsigned corner = 0; corner < 4; ++corner) {
                const bool RIGHT = (corner & 1U) != 0U;
                const bool BOTTOM = (corner & 2U) != 0U;
                const double OUTER_X = RIGHT ? TILE_RIGHT : TILE_LEFT;
                const double INNER_X = RIGHT ? TILE_RIGHT - RADIUS : TILE_LEFT + RADIUS;
                const double OUTER_Y = BOTTOM ? TILE_BOTTOM : TILE_TOP;
                const double INNER_Y = BOTTOM ? TILE_BOTTOM - RADIUS : TILE_TOP + RADIUS;
                const double OUTER_U = DISC_U + (RIGHT ? RADIUS_U : -RADIUS_U);
                const double OUTER_V = 0.5 + (BOTTOM ? RADIUS_V : -RADIUS_V);
                push_rect(meshes.walking_cut,
                    LAYER_X(std::min(OUTER_X, INNER_X), false),
                    LAYER_Y(std::min(OUTER_Y, INNER_Y)),
                    LAYER_X(std::max(OUTER_X, INNER_X), false),
                    LAYER_Y(std::max(OUTER_Y, INNER_Y)),
                    RIGHT ? DISC_U : OUTER_U,
                    BOTTOM ? 0.5 : OUTER_V,
                    RIGHT ? OUTER_U : DISC_U,
                    BOTTOM ? OUTER_V : 0.5);
            }
        }

        // Piece: the tile, carried to where its agent is and turned about its home.
        const double SHIFT_X = (creature_agent.position.x - creature_agent.home.x) * SCALE_X;
        const double SHIFT_Y = (creature_agent.position.y - creature_agent.home.y) * SCALE_Y;
        const double ANGLE = hyprimate::sprite_rotation(
            creature_agent.heading, creature_agent.rest, creature_agent.turn);
        const double ANGLE_COS = std::cos(ANGLE);
        const double ANGLE_SIN = std::sin(ANGLE);
        const auto PIECE = [&](double pos_x, double pos_y) {
            return SMeshRenderVertex{
                .x = static_cast<float>(OUTPUT_X(HOME_X + SHIFT_X + ((pos_x - HOME_X) * ANGLE_COS) -
                                                 ((pos_y - HOME_Y) * ANGLE_SIN))),
                .y = static_cast<float>(OUTPUT_Y(HOME_Y + SHIFT_Y + ((pos_x - HOME_X) * ANGLE_SIN) +
                                                 ((pos_y - HOME_Y) * ANGLE_COS))),
                .u = static_cast<float>(LAYER_X(pos_x, false)),
                .v = static_cast<float>(LAYER_Y(pos_y)),
            };
        };
        push_quad(meshes.pieces,
            PIECE(TILE_LEFT, TILE_TOP),
            PIECE(TILE_LEFT, TILE_BOTTOM),
            PIECE(TILE_RIGHT, TILE_TOP),
            PIECE(TILE_RIGHT, TILE_BOTTOM));
    }
}

SP<Render::IFramebuffer> render_swarm_layer(Render::CRenderContext &ctx,
    const SP<Render::ITexture> &window,
    const SP<Render::ITexture> &atlas,
    const Vector2D &size,
    const SSwarmMeshes &meshes,
    CSwarmSurface &surface) {
    if (!window || !atlas || meshes.content.empty() || !(size.x > 0.0) || !(size.y > 0.0)) {
        return nullptr;
    }

    // Entry blend state, restored on exit; the wrapper does not expose the blend function, so GL is
    // queried for it.
    const bool ENTRY_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entry_src = 0;
    GLint entry_dst = 0;
    GLint entry_src_alpha = 0;
    GLint entry_dst_alpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entry_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &entry_dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &entry_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &entry_dst_alpha);
    const auto RESTORE_BLEND = [&] {
        Render::GL::g_pHyprOpenGL->blend(ENTRY_BLEND);
        Render::GL::g_pHyprOpenGL->blendFunc(
            static_cast<GLenum>(entry_src), static_cast<GLenum>(entry_dst));
        glBlendFuncSeparate(entry_src, entry_dst, entry_src_alpha, entry_dst_alpha);
    };

    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const auto STATE = ctx.saveDrawState();
    auto &data = ctx.m_data;

    const auto LAYER = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!LAYER) {
        return nullptr;
    }

    g_pHyprRenderer->bindFB(ctx, LAYER);
    data.damage = CRegion{0.0, 0.0, size.x, size.y};
    data.fbSize = size;
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &data.damage;
    texture_data.allowCustomUV = true;
    const CBox BOX{0.0, 0.0, size.x, size.y};

    // Silhouettes, unblended: tiles never overlap in the layer, and only the alpha they leave is
    // used.
    texture_data.allowDim = false;
    Render::GL::g_pHyprOpenGL->blend(false);
    if (!meshes.mask.empty()) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, BOX, meshes.mask, texture_data);
    }
    if (!meshes.walking_cut.empty()) {
        Render::GL::g_pHyprOpenGL->blend(true);
        Render::GL::g_pHyprOpenGL->blendFunc(GL_ZERO, GL_SRC_ALPHA);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, atlas, BOX, meshes.walking_cut, texture_data);
    }

    auto smooth = surface.filter(ctx,
        LAYER,
        size,
        meshes.standing_at,
        meshes.smoothing_radius,
        meshes.corner_radius,
        meshes.bodies);
    if (!smooth) {
        RESTORE_BLEND();
        return nullptr;
    }
    g_pHyprRenderer->bindFB(ctx, smooth);
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (!meshes.creatures.empty()) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, atlas, BOX, meshes.creatures, texture_data);
    }

    // The window, weighted by that alpha: pixels outside every silhouette stay transparent.
    texture_data.allowDim = true;
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ZERO);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, window, BOX, meshes.content, texture_data);

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    RESTORE_BLEND();
    return std::exchange(smooth, SP<Render::IFramebuffer>{});
}

CSwarmClosePassElement::CSwarmClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CSwarmClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.sprites || (m_data.meshes == nullptr) || m_data.meshes->pieces.empty()) {
        return {};
    }
    const auto LAYER = render_swarm_layer(ctx,
        m_data.tex,
        m_data.atlas,
        m_data.layer_size,
        *m_data.meshes,
        m_data.sprites->surface());
    if (!LAYER) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &m_data.damage;
    texture_data.a = m_data.alpha;
    texture_data.allowCustomUV = true;
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, LAYER->getTexture(), m_data.box, m_data.meshes->pieces, texture_data);
    return {};
}

bool CSwarmClosePassElement::needsLiveBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

bool CSwarmClosePassElement::needsPrecomputeBlur([[maybe_unused]] Render::CRenderContext &ctx) {
    return false;
}

const char *CSwarmClosePassElement::passName() {
    return "hyprimate-swarm-close";
}

ePassElementType CSwarmClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CSwarmClosePassElement::boundingBox(
    [[maybe_unused]] Render::CRenderContext &ctx) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CSwarmClosePassElement::opaqueRegion([[maybe_unused]] Render::CRenderContext &ctx) {
    return {};
}
