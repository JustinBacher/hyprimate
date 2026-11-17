#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <optional>
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
        const auto PATH = hyprimate::spritePath(creature, HYPRIMATE_SPRITE_DIR);
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
            cairo_set_source_surface(
                context, image, -(k % COLUMNS) * FRAME_W, -(k / COLUMNS) * FRAME_H);
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
            for (int atlasRow = 0; atlasRow < CELL_PX; ++atlasRow) {
                std::memcpy(pixels.data() + (static_cast<std::size_t>(atlasRow) * WIDTH * 4),
                    data + (static_cast<std::size_t>(atlasRow) * STRIDE),
                    static_cast<std::size_t>(WIDTH) * 4);
            }

            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(FRAME_PX) * FRAME_PX);
            sheet.frames.clear();
            sheet.coreShare = 1.0;
            for (int k = 0; k < FRAMES; ++k) {
                for (int rowIndex = 0; rowIndex < FRAME_PX; ++rowIndex) {
                    const std::uint8_t *row =
                        pixels.data() + (((static_cast<std::size_t>(rowIndex + GUTTER_PX) * WIDTH) +
                                             (k * CELL_PX) + GUTTER_PX) *
                                            4);
                    for (int colIndex = 0; colIndex < FRAME_PX; ++colIndex) {
                        alpha[(static_cast<std::size_t>(rowIndex) * FRAME_PX) + colIndex] =
                            row[(colIndex * 4) + 3];
                    }
                }
                sheet.frames.push_back(hyprimate::measureSprite(alpha, FRAME_PX));
                const auto &frame = sheet.frames.back();
                sheet.coreShare =
                    std::min(sheet.coreShare, frame.reach > 0.0 ? frame.core / frame.reach : 0.0);
            }
            sheet.forward = -0.5 * std::numbers::pi;
            sheet.stride = 1.0;
        }
        cairo_surface_destroy(atlas);
        cairo_surface_destroy(image);
        return DRAWN;
    }

    void pushQuad(std::vector<SMeshRenderVertex> &out,
        const SMeshRenderVertex &topLeft,
        const SMeshRenderVertex &bottomLeft,
        const SMeshRenderVertex &topRight,
        const SMeshRenderVertex &bottomRight) {
        out.push_back(topLeft);
        out.push_back(bottomLeft);
        out.push_back(topRight);
        out.push_back(topRight);
        out.push_back(bottomLeft);
        out.push_back(bottomRight);
    }

    void pushRect(std::vector<SMeshRenderVertex> &out,
        double x0,
        double y0,
        double x1,
        double y1,
        double u0,
        double v0,
        double u1,
        double v1) {
        const auto VERTEX = [](double x, double y, double u, double v) {
            return SMeshRenderVertex{.x = static_cast<float>(x),
                .y = static_cast<float>(y),
                .u = static_cast<float>(u),
                .v = static_cast<float>(v)};
        };
        pushQuad(out,
            VERTEX(x0, y0, u0, v0),
            VERTEX(x0, y1, u0, v1),
            VERTEX(x1, y0, u1, v0),
            VERTEX(x1, y1, u1, v1));
    }

    struct SMaskPoint {
        double x = 0.0, y = 0.0, u = 0.0, v = 0.0;
    };

    // Clips a convex polygon of `count` points to an axis-aligned box, in place, carrying its
    // texture coordinates along. Returns the new count; a quad clipped to a box has at most eight
    // points.
    std::size_t clipToBox(std::array<SMaskPoint, 8> &polygon,
        std::size_t count,
        double x0,
        double y0,
        double x1,
        double y1) {
        // One pass keeps the points on one side of one of the box's edges.
        const auto PASS = [&](bool alongX, double limit, bool keepBelow) {
            std::array<SMaskPoint, 8> kept;
            std::size_t keptCount = 0;
            const auto VALUE = [&](const SMaskPoint &p) { return alongX ? p.x : p.y; };
            const auto INSIDE = [&](const SMaskPoint &p) {
                return keepBelow ? VALUE(p) <= limit : VALUE(p) >= limit;
            };
            for (std::size_t i = 0; i < count; ++i) {
                const SMaskPoint &startPoint = polygon[i];
                const SMaskPoint &endPoint = polygon[(i + 1) % count];
                if (INSIDE(startPoint) && keptCount < kept.size()) {
                    kept[keptCount++] = startPoint;
                }
                if (INSIDE(startPoint) != INSIDE(endPoint) && keptCount < kept.size()) {
                    const double FRACTION =
                        (limit - VALUE(startPoint)) / (VALUE(endPoint) - VALUE(startPoint));
                    kept[keptCount++] = {
                        .x = startPoint.x + ((endPoint.x - startPoint.x) * FRACTION),
                        .y = startPoint.y + ((endPoint.y - startPoint.y) * FRACTION),
                        .u = startPoint.u + ((endPoint.u - startPoint.u) * FRACTION),
                        .v = startPoint.v + ((endPoint.v - startPoint.v) * FRACTION)};
                }
            }
            polygon = kept;
            count = keptCount;
        };
        PASS(true, x0, false);
        PASS(true, x1, true);
        PASS(false, y0, false);
        PASS(false, y1, true);
        return count;
    }

}  // namespace

CSwarmSprites::SEntry &CSwarmSprites::entry(const std::string &creature) {
    SEntry &spriteEntry = m_entries[creature];
    if (!spriteEntry.tried) {
        spriteEntry.tried = true;
        if (!bake(creature, spriteEntry.sheet, spriteEntry.pixels)) {
            spriteEntry.sheet.frames.clear();
            spriteEntry.pixels.clear();
        }
    }
    return spriteEntry;
}

const CSwarmSprites::SSheet *CSwarmSprites::sheet(const std::string &creature) {
    const SEntry &spriteEntry = entry(creature);
    return spriteEntry.sheet.frames.empty() ? nullptr : &spriteEntry.sheet;
}

SP<Render::ITexture> CSwarmSprites::texture(const std::string &creature) {
    SEntry &spriteEntry = entry(creature);
    if (spriteEntry.texture || spriteEntry.pixels.empty() || !g_pHyprRenderer) {
        return spriteEntry.texture;
    }
    const int WIDTH = (static_cast<int>(spriteEntry.sheet.frames.size()) + SHAPE_CELLS) * CELL_PX;
    spriteEntry.texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        spriteEntry.pixels.data(),
        WIDTH * 4,
        Vector2D{static_cast<double>(WIDTH), static_cast<double>(CELL_PX)},
        false,
        false);
    if (spriteEntry.texture) {
        spriteEntry.pixels = {};
    }
    return spriteEntry.texture;
}

CBox swarmLayerBox(const CBox &source) {
    const double LEFT_EDGE = std::floor(source.x);
    const double TOP_EDGE = std::floor(source.y);
    return CBox{LEFT_EDGE,
        TOP_EDGE,
        std::ceil(source.x + source.w) - LEFT_EDGE,
        std::ceil(source.y + source.h) - TOP_EDGE};
}

Vector2D swarmLayerSize(const CBox &source) {
    const CBox BOX = swarmLayerBox(source);
    return Vector2D{(2.0 * BOX.w) + LAYER_GAP, BOX.h};
}

void fillSwarmMeshes(const hyprimate::Swarm &swarm,
    const CSwarmSprites::SSheet &sheet,
    const CBox &source,
    const CBox &output,
    Vec2 textureOrigin,
    Vec2 textureSize,
    SSwarmMeshes &meshes) {
    meshes.mask.clear();
    meshes.creatures.clear();
    meshes.walkingCut.clear();
    meshes.content.clear();
    meshes.pieces.clear();
    meshes.bodies.clear();
    meshes.standingAt = 0.0;
    meshes.smoothingRadius = 0.0;
    meshes.cornerRadius = 0.0;

    const auto AGENTS = swarm.agents();
    const Vec2 SIZE = swarm.size();
    const int FRAMES = static_cast<int>(sheet.frames.size());
    if (AGENTS.empty() || FRAMES == 0 || !(SIZE.x > 0.0 && SIZE.y > 0.0) || source.empty() ||
        output.empty()) {
        return;
    }

    const CBox BOX = swarmLayerBox(source);
    const double SCALE_X = source.w / SIZE.x;
    const double SCALE_Y = source.h / SIZE.y;
    const double UNIT = std::min(SCALE_X, SCALE_Y);
    // The layer: walking creatures in its left half, everything standing in the window in its
    // right.
    const double WIDTH = (2.0 * BOX.w) + LAYER_GAP;
    const double STANDING_AT = BOX.w + LAYER_GAP;
    const double ATLAS_W = (FRAMES + SHAPE_CELLS) * CELL_PX;
    const double SOLID_U = (FRAMES + 0.5) * CELL_PX / ATLAS_W;
    meshes.standingAt = STANDING_AT;

    // Layer and output coordinates of a device-px point.
    const auto LAYER_X = [&](double x, bool standing) {
        return (x - BOX.x + (standing ? STANDING_AT : 0.0)) / WIDTH;
    };
    const auto LAYER_Y = [&](double y) { return (y - BOX.y) / BOX.h; };
    const auto OUTPUT_X = [&](double x) { return (x - output.x) / output.w; };
    const auto OUTPUT_Y = [&](double y) { return (y - output.y) / output.h; };

    // The window, once under each half.
    const double LEFT_U = (BOX.x - textureOrigin.x) / textureSize.x;
    const double RIGHT_U = (BOX.x + BOX.w - textureOrigin.x) / textureSize.x;
    const double TOP_V = (BOX.y - textureOrigin.y) / textureSize.y;
    const double BOTTOM_V = (BOX.y + BOX.h - textureOrigin.y) / textureSize.y;
    pushRect(meshes.content, 0.0, 0.0, BOX.w / WIDTH, 1.0, LEFT_U, TOP_V, RIGHT_U, BOTTOM_V);
    pushRect(meshes.content, STANDING_AT / WIDTH, 0.0, 1.0, 1.0, LEFT_U, TOP_V, RIGHT_U, BOTTOM_V);

    // An agent stands in the window once it is home and has swung round; until then it is a walking
    // piece.
    const auto STANDING = [](const hyprimate::SwarmAgent &a) { return a.docked && a.turn <= 0.0; };
    const bool HAS_STANDING =
        std::ranges::any_of(AGENTS, [&](const auto &a) { return a.visible && STANDING(a); });
    if (HAS_STANDING && swarm.rounding() > 0.0) {
        const auto SMALLEST = std::ranges::min_element(AGENTS, {}, &hyprimate::SwarmAgent::fit);
        // Keep joins smooth from the first detachment. Only the outer rounding relaxes;
        // shrinking the union length would briefly cut square holes around departing agents.
        meshes.smoothingRadius = std::min(20.0, 0.4 * SMALLEST->fit * UNIT);
        meshes.cornerRadius =
            hyprimate::kSwarmSurfaceCornerScale * meshes.smoothingRadius * swarm.rounding();
        meshes.bodies.reserve(AGENTS.size());
    }
    // What stands in the window is drawn first and in one piece, so it has no seams; the walkers go
    // over it.
    if (HAS_STANDING) {
        pushRect(meshes.pieces,
            OUTPUT_X(BOX.x),
            OUTPUT_Y(BOX.y),
            OUTPUT_X(BOX.x + BOX.w),
            OUTPUT_Y(BOX.y + BOX.h),
            STANDING_AT / WIDTH,
            0.0,
            1.0,
            1.0);
    }

    for (const hyprimate::SwarmAgent &creatureAgent : AGENTS) {
        const double TILE_LEFT = source.x + (creatureAgent.tile.x * SCALE_X);
        const double TILE_RIGHT =
            source.x + ((creatureAgent.tile.x + creatureAgent.tile.w) * SCALE_X);
        const double TILE_TOP = source.y + (creatureAgent.tile.y * SCALE_Y);
        const double TILE_BOTTOM =
            source.y + ((creatureAgent.tile.y + creatureAgent.tile.h) * SCALE_Y);
        const double HOME_X = source.x + (creatureAgent.home.x * SCALE_X);
        const double HOME_Y = source.y + (creatureAgent.home.y * SCALE_Y);
        const bool STANDS = STANDING(creatureAgent);

        if (!creatureAgent.visible) {
            continue;
        }

        // The silhouette keeps its travel heading while the carried tile straightens beneath it.
        // Counter-rotate the mask by the piece transform, including throughout docking.
        const auto SILHOUETTE = [&](std::vector<SMeshRenderVertex> &out, double lock) {
            const int FRAME_INDEX = hyprimate::walkFrame(
                creatureAgent.walk, sheet.stride * 2.0 * creatureAgent.fit, FRAMES);
            const auto &frame = sheet.frames[static_cast<std::size_t>(FRAME_INDEX)];
            const double SIDE = hyprimate::spriteSize(frame,
                creatureAgent.fit * UNIT,
                creatureAgent.reach * std::max(SCALE_X, SCALE_Y),
                lock);
            const double FACING =
                hyprimate::spriteRotation(creatureAgent.heading, sheet.forward, 1.0) -
                hyprimate::spriteRotation(
                    creatureAgent.heading, creatureAgent.rest, creatureAgent.turn);
            const double FACING_COS = std::cos(FACING);
            const double FACING_SIN = std::sin(FACING);
            // Where a point of the frame lands: the creature's body on the tile's home, turned
            // about it.
            const auto FRAME_POINT = [&](double fx, double fy) {
                const double OFFSET_X = (fx - frame.centre.x) * SIDE;
                const double OFFSET_Y = (fy - frame.centre.y) * SIDE;
                return SMaskPoint{.x = HOME_X + (OFFSET_X * FACING_COS) - (OFFSET_Y * FACING_SIN),
                    .y = HOME_Y + (OFFSET_X * FACING_SIN) + (OFFSET_Y * FACING_COS),
                    .u = ((FRAME_INDEX * CELL_PX) + GUTTER_PX + (fx * FRAME_PX)) / ATLAS_W,
                    .v = (GUTTER_PX + (fy * FRAME_PX)) / CELL_PX};
            };
            const double LOW = -FRAME_MARGIN;
            const double HIGH = 1.0 + FRAME_MARGIN;
            std::array<SMaskPoint, 8> polygon{FRAME_POINT(LOW, LOW),
                FRAME_POINT(HIGH, LOW),
                FRAME_POINT(HIGH, HIGH),
                FRAME_POINT(LOW, HIGH)};
            const std::size_t POINTS =
                SIDE > 0.0 ? clipToBox(polygon, 4, TILE_LEFT, TILE_TOP, TILE_RIGHT, TILE_BOTTOM)
                           : 0;
            const auto VERTEX = [&](const SMaskPoint &p) {
                return SMeshRenderVertex{.x = static_cast<float>(LAYER_X(p.x, STANDS)),
                    .y = static_cast<float>(LAYER_Y(p.y)),
                    .u = static_cast<float>(p.u),
                    .v = static_cast<float>(p.v)};
            };
            for (std::size_t i = 1; i + 1 < POINTS; ++i) {
                out.push_back(VERTEX(polygon[0]));
                out.push_back(VERTEX(polygon[i]));
                out.push_back(VERTEX(polygon[i + 1]));
            }
        };
        if (creatureAgent.lock >= 1.0) {
            pushRect(meshes.mask,
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
            SILHOUETTE(meshes.mask, creatureAgent.lock);
        }

        if (STANDS) {
            if (creatureAgent.lock > 0.0 && meshes.smoothingRadius > 0.0) {
                // Grow a stable body, not the sprite's raster outline. Gait frames stay on top
                // until engulfed, without changing the mass's contour as their limbs move.
                const double LOCK_BLEND =
                    creatureAgent.lock * creatureAgent.lock * (3.0 - (2.0 * creatureAgent.lock));
                const double CORE = sheet.coreShare * creatureAgent.fit * UNIT;
                const double HALF_W = (TILE_RIGHT - TILE_LEFT) * 0.5;
                const double HALF_H = (TILE_BOTTOM - TILE_TOP) * 0.5;
                const double CORNER = std::min({HALF_W, HALF_H, meshes.cornerRadius});
                meshes.bodies.push_back({
                    .centreX = static_cast<float>(
                        std::lerp(HOME_X, (TILE_LEFT + TILE_RIGHT) * 0.5, LOCK_BLEND) - BOX.x),
                    .centreY = static_cast<float>(
                        std::lerp(HOME_Y, (TILE_TOP + TILE_BOTTOM) * 0.5, LOCK_BLEND) - BOX.y),
                    .halfWidth = static_cast<float>(std::lerp(CORE, HALF_W, LOCK_BLEND)),
                    .halfHeight = static_cast<float>(std::lerp(CORE, HALF_H, LOCK_BLEND)),
                    .cornerRadius = static_cast<float>(std::lerp(CORE, CORNER, LOCK_BLEND)),
                });
            }
            continue;
        }

        // Detached pieces are rounded on their own; only the standing mass uses union smoothing.
        // This keeps a closing tile from briefly becoming a sharp rectangle before its sprite
        // shrinks away from the tile's boundaries.
        if (creatureAgent.lock > 0.0) {
            const double RADIUS = creatureAgent.fit * UNIT;
            const double DISC_U = (FRAMES + 1.5) * CELL_PX / ATLAS_W;
            const double RADIUS_U = 0.5 * FRAME_PX / ATLAS_W;
            const double RADIUS_V = 0.5 * FRAME_PX / CELL_PX;
            for (unsigned corner = 0; corner < 4; ++corner) {
                const bool RIGHT = (corner & 1U) != 0u;
                const bool BOTTOM = (corner & 2U) != 0u;
                const double OUTER_X = RIGHT ? TILE_RIGHT : TILE_LEFT;
                const double INNER_X = RIGHT ? TILE_RIGHT - RADIUS : TILE_LEFT + RADIUS;
                const double OUTER_Y = BOTTOM ? TILE_BOTTOM : TILE_TOP;
                const double INNER_Y = BOTTOM ? TILE_BOTTOM - RADIUS : TILE_TOP + RADIUS;
                const double OUTER_U = DISC_U + (RIGHT ? RADIUS_U : -RADIUS_U);
                const double OUTER_V = 0.5 + (BOTTOM ? RADIUS_V : -RADIUS_V);
                pushRect(meshes.walkingCut,
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
        const double SHIFT_X = (creatureAgent.position.x - creatureAgent.home.x) * SCALE_X;
        const double SHIFT_Y = (creatureAgent.position.y - creatureAgent.home.y) * SCALE_Y;
        const double ANGLE = hyprimate::spriteRotation(
            creatureAgent.heading, creatureAgent.rest, creatureAgent.turn);
        const double ANGLE_COS = std::cos(ANGLE);
        const double ANGLE_SIN = std::sin(ANGLE);
        const auto PIECE = [&](double x, double y) {
            return SMeshRenderVertex{
                .x = static_cast<float>(OUTPUT_X(
                    HOME_X + SHIFT_X + ((x - HOME_X) * ANGLE_COS) - ((y - HOME_Y) * ANGLE_SIN))),
                .y = static_cast<float>(OUTPUT_Y(
                    HOME_Y + SHIFT_Y + ((x - HOME_X) * ANGLE_SIN) + ((y - HOME_Y) * ANGLE_COS))),
                .u = static_cast<float>(LAYER_X(x, false)),
                .v = static_cast<float>(LAYER_Y(y))};
        };
        pushQuad(meshes.pieces,
            PIECE(TILE_LEFT, TILE_TOP),
            PIECE(TILE_LEFT, TILE_BOTTOM),
            PIECE(TILE_RIGHT, TILE_TOP),
            PIECE(TILE_RIGHT, TILE_BOTTOM));
    }
}

SP<Render::IFramebuffer> renderSwarmLayer(Render::CRenderContext &ctx,
    const SP<Render::ITexture> &window,
    const SP<Render::ITexture> &atlas,
    const Vector2D &size,
    const SSwarmMeshes &meshes,
    CSwarmSurface &surface) {
    if (!window || !atlas || meshes.content.empty() || !(size.x > 0.0 && size.y > 0.0)) {
        return nullptr;
    }

    // Entry blend state, restored on exit; the wrapper does not expose the blend function, so GL is
    // queried for it.
    const bool ENTRY_BLEND = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entrySrc = 0;
    GLint entryDst = 0;
    GLint entrySrcAlpha = 0;
    GLint entryDstAlpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entrySrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &entryDst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &entrySrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &entryDstAlpha);
    const auto RESTORE_BLEND = [&] {
        Render::GL::g_pHyprOpenGL->blend(ENTRY_BLEND);
        Render::GL::g_pHyprOpenGL->blendFunc(
            static_cast<GLenum>(entrySrc), static_cast<GLenum>(entryDst));
        glBlendFuncSeparate(entrySrc, entryDst, entrySrcAlpha, entryDstAlpha);
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

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &data.damage;
    textureData.allowCustomUV = true;
    const CBox BOX{0.0, 0.0, size.x, size.y};

    // Silhouettes, unblended: tiles never overlap in the layer, and only the alpha they leave is
    // used.
    textureData.allowDim = false;
    Render::GL::g_pHyprOpenGL->blend(false);
    if (!meshes.mask.empty()) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, BOX, meshes.mask, textureData);
    }
    if (!meshes.walkingCut.empty()) {
        Render::GL::g_pHyprOpenGL->blend(true);
        Render::GL::g_pHyprOpenGL->blendFunc(GL_ZERO, GL_SRC_ALPHA);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(
            ctx, atlas, BOX, meshes.walkingCut, textureData);
    }

    auto smooth = surface.filter(ctx,
        LAYER,
        size,
        meshes.standingAt,
        meshes.smoothingRadius,
        meshes.cornerRadius,
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
            ctx, atlas, BOX, meshes.creatures, textureData);
    }

    // The window, weighted by that alpha: pixels outside every silhouette stay transparent.
    textureData.allowDim = true;
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ZERO);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, window, BOX, meshes.content, textureData);

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
    const auto LAYER = renderSwarmLayer(
        ctx, m_data.tex, m_data.atlas, m_data.layerSize, *m_data.meshes, m_data.sprites->surface());
    if (!LAYER) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &m_data.damage;
    textureData.a = m_data.alpha;
    textureData.allowCustomUV = true;
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, LAYER->getTexture(), m_data.box, m_data.meshes->pieces, textureData);
    return {};
}

bool CSwarmClosePassElement::needsLiveBlur(Render::CRenderContext &) {
    return false;
}

bool CSwarmClosePassElement::needsPrecomputeBlur(Render::CRenderContext &) {
    return false;
}

const char *CSwarmClosePassElement::passName() {
    return "hyprimate-swarm-close";
}

ePassElementType CSwarmClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CSwarmClosePassElement::boundingBox(Render::CRenderContext &) {
    return m_data.box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CSwarmClosePassElement::opaqueRegion(Render::CRenderContext &) {
    return {};
}
