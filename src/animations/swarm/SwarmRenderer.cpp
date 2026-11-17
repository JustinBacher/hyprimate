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
        const auto path = hyprimate::spritePath(creature, HYPRIMATE_SPRITE_DIR);
        cairo_surface_t *image = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(image);
            return false;
        }

        constexpr int columns = 4;
        constexpr int rows = 2;
        constexpr int frames = columns * rows;
        const int width = (frames + SHAPE_CELLS) * CELL_PX;
        cairo_surface_t *atlas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, CELL_PX);
        cairo_t *cr = cairo_create(atlas);
        const double frameW = static_cast<double>(cairo_image_surface_get_width(image)) / columns;
        const double frameH = static_cast<double>(cairo_image_surface_get_height(image)) / rows;
        for (int k = 0; k < frames; ++k) {
            const double x = (k * CELL_PX) + GUTTER_PX;
            cairo_save(cr);
            cairo_rectangle(cr, x, GUTTER_PX, FRAME_PX, FRAME_PX);
            cairo_clip(cr);
            cairo_translate(cr, x, GUTTER_PX);
            cairo_scale(cr, FRAME_PX / frameW, FRAME_PX / frameH);
            cairo_set_source_surface(cr, image, -(k % columns) * frameW, -(k / columns) * frameH);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        const double shapes = frames * CELL_PX;
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
        cairo_rectangle(cr, shapes, 0.0, CELL_PX, CELL_PX);
        cairo_fill(cr);
        cairo_arc(cr,
            shapes + (CELL_PX * 1.5),
            CELL_PX * 0.5,
            0.5 * FRAME_PX,
            0.0,
            2.0 * std::numbers::pi);
        cairo_fill(cr);
        cairo_destroy(cr);
        cairo_surface_flush(atlas);
        const bool drawn = cairo_surface_status(atlas) == CAIRO_STATUS_SUCCESS;

        if (drawn) {
            // Premultiplied ARGB32 in native byte order: B, G, R, A in memory, as
            // DRM_FORMAT_ARGB8888 wants.
            const unsigned char *data = cairo_image_surface_get_data(atlas);
            const int stride = cairo_image_surface_get_stride(atlas);
            pixels.resize(static_cast<std::size_t>(width) * CELL_PX * 4);
            for (int y = 0; y < CELL_PX; ++y) {
                std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * width * 4),
                    data + (static_cast<std::size_t>(y) * stride),
                    static_cast<std::size_t>(width) * 4);
            }

            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(FRAME_PX) * FRAME_PX);
            sheet.frames.clear();
            sheet.coreShare = 1.0;
            for (int k = 0; k < frames; ++k) {
                for (int y = 0; y < FRAME_PX; ++y) {
                    const std::uint8_t *row =
                        pixels.data() + (((static_cast<std::size_t>(y + GUTTER_PX) * width) +
                                             (k * CELL_PX) + GUTTER_PX) *
                                            4);
                    for (int x = 0; x < FRAME_PX; ++x) {
                        alpha[(static_cast<std::size_t>(y) * FRAME_PX) + x] = row[(x * 4) + 3];
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
        return drawn;
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
        const auto vertex = [](double x, double y, double u, double v) {
            return SMeshRenderVertex{.x = static_cast<float>(x),
                .y = static_cast<float>(y),
                .u = static_cast<float>(u),
                .v = static_cast<float>(v)};
        };
        pushQuad(out,
            vertex(x0, y0, u0, v0),
            vertex(x0, y1, u0, v1),
            vertex(x1, y0, u1, v0),
            vertex(x1, y1, u1, v1));
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
        const auto pass = [&](bool alongX, double limit, bool keepBelow) {
            std::array<SMaskPoint, 8> kept;
            std::size_t n = 0;
            const auto value = [&](const SMaskPoint &p) { return alongX ? p.x : p.y; };
            const auto inside = [&](const SMaskPoint &p) {
                return keepBelow ? value(p) <= limit : value(p) >= limit;
            };
            for (std::size_t i = 0; i < count; ++i) {
                const SMaskPoint &a = polygon[i];
                const SMaskPoint &b = polygon[(i + 1) % count];
                if (inside(a) && n < kept.size()) {
                    kept[n++] = a;
                }
                if (inside(a) != inside(b) && n < kept.size()) {
                    const double t = (limit - value(a)) / (value(b) - value(a));
                    kept[n++] = {.x = a.x + ((b.x - a.x) * t),
                        .y = a.y + ((b.y - a.y) * t),
                        .u = a.u + ((b.u - a.u) * t),
                        .v = a.v + ((b.v - a.v) * t)};
                }
            }
            polygon = kept;
            count = n;
        };
        pass(true, x0, false);
        pass(true, x1, true);
        pass(false, y0, false);
        pass(false, y1, true);
        return count;
    }

}  // namespace

CSwarmSprites::SEntry &CSwarmSprites::entry(const std::string &creature) {
    SEntry &e = m_entries[creature];
    if (!e.tried) {
        e.tried = true;
        if (!bake(creature, e.sheet, e.pixels)) {
            e.sheet.frames.clear();
            e.pixels.clear();
        }
    }
    return e;
}

const CSwarmSprites::SSheet *CSwarmSprites::sheet(const std::string &creature) {
    const SEntry &e = entry(creature);
    return e.sheet.frames.empty() ? nullptr : &e.sheet;
}

SP<Render::ITexture> CSwarmSprites::texture(const std::string &creature) {
    SEntry &e = entry(creature);
    if (e.texture || e.pixels.empty() || !g_pHyprRenderer) {
        return e.texture;
    }
    const int width = (static_cast<int>(e.sheet.frames.size()) + SHAPE_CELLS) * CELL_PX;
    e.texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        e.pixels.data(),
        width * 4,
        Vector2D{static_cast<double>(width), static_cast<double>(CELL_PX)},
        false,
        false);
    if (e.texture) {
        e.pixels = {};
    }
    return e.texture;
}

CBox swarmLayerBox(const CBox &source) {
    const double x0 = std::floor(source.x);
    const double y0 = std::floor(source.y);
    return CBox{x0, y0, std::ceil(source.x + source.w) - x0, std::ceil(source.y + source.h) - y0};
}

Vector2D swarmLayerSize(const CBox &source) {
    const CBox box = swarmLayerBox(source);
    return Vector2D{(2.0 * box.w) + LAYER_GAP, box.h};
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

    const auto agents = swarm.agents();
    const Vec2 size = swarm.size();
    const int frames = static_cast<int>(sheet.frames.size());
    if (agents.empty() || frames == 0 || !(size.x > 0.0 && size.y > 0.0) || source.empty() ||
        output.empty()) {
        return;
    }

    const CBox box = swarmLayerBox(source);
    const double sx = source.w / size.x;
    const double sy = source.h / size.y;
    const double unit = std::min(sx, sy);
    // The layer: walking creatures in its left half, everything standing in the window in its
    // right.
    const double width = 2.0 * box.w + LAYER_GAP;
    const double standingAt = box.w + LAYER_GAP;
    const double atlasW = (frames + SHAPE_CELLS) * CELL_PX;
    const double solidU = (frames + 0.5) * CELL_PX / atlasW;
    meshes.standingAt = standingAt;

    // Layer and output coordinates of a device-px point.
    const auto lx = [&](double x, bool standing) {
        return (x - box.x + (standing ? standingAt : 0.0)) / width;
    };
    const auto ly = [&](double y) { return (y - box.y) / box.h; };
    const auto ox = [&](double x) { return (x - output.x) / output.w; };
    const auto oy = [&](double y) { return (y - output.y) / output.h; };

    // The window, once under each half.
    const double u0 = (box.x - textureOrigin.x) / textureSize.x;
    const double u1 = (box.x + box.w - textureOrigin.x) / textureSize.x;
    const double v0 = (box.y - textureOrigin.y) / textureSize.y;
    const double v1 = (box.y + box.h - textureOrigin.y) / textureSize.y;
    pushRect(meshes.content, 0.0, 0.0, box.w / width, 1.0, u0, v0, u1, v1);
    pushRect(meshes.content, standingAt / width, 0.0, 1.0, 1.0, u0, v0, u1, v1);

    // An agent stands in the window once it is home and has swung round; until then it is a walking
    // piece.
    const auto standing = [](const hyprimate::SwarmAgent &a) { return a.docked && a.turn <= 0.0; };
    const bool hasStanding =
        std::ranges::any_of(agents, [&](const auto &a) { return a.visible && standing(a); });
    if (hasStanding && swarm.rounding() > 0.0) {
        const auto smallest = std::ranges::min_element(agents, {}, &hyprimate::SwarmAgent::fit);
        // Keep joins smooth from the first detachment. Only the outer rounding relaxes;
        // shrinking the union length would briefly cut square holes around departing agents.
        meshes.smoothingRadius = std::min(20.0, 0.4 * smallest->fit * unit);
        meshes.cornerRadius =
            hyprimate::kSwarmSurfaceCornerScale * meshes.smoothingRadius * swarm.rounding();
        meshes.bodies.reserve(agents.size());
    }
    // What stands in the window is drawn first and in one piece, so it has no seams; the walkers go
    // over it.
    if (hasStanding) {
        pushRect(meshes.pieces,
            ox(box.x),
            oy(box.y),
            ox(box.x + box.w),
            oy(box.y + box.h),
            standingAt / width,
            0.0,
            1.0,
            1.0);
    }

    for (const hyprimate::SwarmAgent &a : agents) {
        const double x0 = source.x + a.tile.x * sx;
        const double x1 = source.x + (a.tile.x + a.tile.w) * sx;
        const double y0 = source.y + a.tile.y * sy;
        const double y1 = source.y + (a.tile.y + a.tile.h) * sy;
        const double hx = source.x + a.home.x * sx;
        const double hy = source.y + a.home.y * sy;
        const bool stands = standing(a);

        if (!a.visible) {
            continue;
        }

        // The silhouette keeps its travel heading while the carried tile straightens beneath it.
        // Counter-rotate the mask by the piece transform, including throughout docking.
        const auto silhouette = [&](std::vector<SMeshRenderVertex> &out, double lock) {
            const int k = hyprimate::walkFrame(a.walk, sheet.stride * 2.0 * a.fit, frames);
            const auto &frame = sheet.frames[static_cast<std::size_t>(k)];
            const double side =
                hyprimate::spriteSize(frame, a.fit * unit, a.reach * std::max(sx, sy), lock);
            const double facing = hyprimate::spriteRotation(a.heading, sheet.forward, 1.0) -
                                  hyprimate::spriteRotation(a.heading, a.rest, a.turn);
            const double c = std::cos(facing);
            const double s = std::sin(facing);
            // Where a point of the frame lands: the creature's body on the tile's home, turned
            // about it.
            const auto at = [&](double fx, double fy) {
                const double dx = (fx - frame.centre.x) * side;
                const double dy = (fy - frame.centre.y) * side;
                return SMaskPoint{.x = hx + (dx * c) - (dy * s),
                    .y = hy + (dx * s) + (dy * c),
                    .u = ((k * CELL_PX) + GUTTER_PX + (fx * FRAME_PX)) / atlasW,
                    .v = (GUTTER_PX + (fy * FRAME_PX)) / CELL_PX};
            };
            const double low = -FRAME_MARGIN;
            const double high = 1.0 + FRAME_MARGIN;
            std::array<SMaskPoint, 8> polygon{
                at(low, low), at(high, low), at(high, high), at(low, high)};
            const std::size_t points = side > 0.0 ? clipToBox(polygon, 4, x0, y0, x1, y1) : 0;
            const auto vertex = [&](const SMaskPoint &p) {
                return SMeshRenderVertex{.x = static_cast<float>(lx(p.x, stands)),
                    .y = static_cast<float>(ly(p.y)),
                    .u = static_cast<float>(p.u),
                    .v = static_cast<float>(p.v)};
            };
            for (std::size_t i = 1; i + 1 < points; ++i) {
                out.push_back(vertex(polygon[0]));
                out.push_back(vertex(polygon[i]));
                out.push_back(vertex(polygon[i + 1]));
            }
        };
        if (a.lock >= 1.0) {
            pushRect(meshes.mask,
                lx(x0, stands),
                ly(y0),
                lx(x1, stands),
                ly(y1),
                solidU,
                0.5,
                solidU,
                0.5);
        } else if (stands) {
            silhouette(meshes.creatures, 0.0);
        } else {
            silhouette(meshes.mask, a.lock);
        }

        if (stands) {
            if (a.lock > 0.0 && meshes.smoothingRadius > 0.0) {
                // Grow a stable body, not the sprite's raster outline. Gait frames stay on top
                // until engulfed, without changing the mass's contour as their limbs move.
                const double t = a.lock * a.lock * (3.0 - (2.0 * a.lock));
                const double core = sheet.coreShare * a.fit * unit;
                const double halfW = (x1 - x0) * 0.5;
                const double halfH = (y1 - y0) * 0.5;
                const double corner = std::min({halfW, halfH, meshes.cornerRadius});
                meshes.bodies.push_back({
                    .centreX = static_cast<float>(std::lerp(hx, (x0 + x1) * 0.5, t) - box.x),
                    .centreY = static_cast<float>(std::lerp(hy, (y0 + y1) * 0.5, t) - box.y),
                    .halfWidth = static_cast<float>(std::lerp(core, halfW, t)),
                    .halfHeight = static_cast<float>(std::lerp(core, halfH, t)),
                    .cornerRadius = static_cast<float>(std::lerp(core, corner, t)),
                });
            }
            continue;
        }

        // Detached pieces are rounded on their own; only the standing mass uses union smoothing.
        // This keeps a closing tile from briefly becoming a sharp rectangle before its sprite
        // shrinks away from the tile's boundaries.
        if (a.lock > 0.0) {
            const double radius = a.fit * unit;
            const double discU = (frames + 1.5) * CELL_PX / atlasW;
            const double radiusU = 0.5 * FRAME_PX / atlasW;
            const double radiusV = 0.5 * FRAME_PX / CELL_PX;
            for (unsigned corner = 0; corner < 4; ++corner) {
                const bool right = corner & 1U;
                const bool bottom = corner & 2U;
                const double outerX = right ? x1 : x0;
                const double innerX = right ? x1 - radius : x0 + radius;
                const double outerY = bottom ? y1 : y0;
                const double innerY = bottom ? y1 - radius : y0 + radius;
                const double outerU = discU + (right ? radiusU : -radiusU);
                const double outerV = 0.5 + (bottom ? radiusV : -radiusV);
                pushRect(meshes.walkingCut,
                    lx(std::min(outerX, innerX), false),
                    ly(std::min(outerY, innerY)),
                    lx(std::max(outerX, innerX), false),
                    ly(std::max(outerY, innerY)),
                    right ? discU : outerU,
                    bottom ? 0.5 : outerV,
                    right ? outerU : discU,
                    bottom ? outerV : 0.5);
            }
        }

        // Piece: the tile, carried to where its agent is and turned about its home.
        const double dx = (a.position.x - a.home.x) * sx;
        const double dy = (a.position.y - a.home.y) * sy;
        const double angle = hyprimate::spriteRotation(a.heading, a.rest, a.turn);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const auto piece = [&](double x, double y) {
            return SMeshRenderVertex{
                .x = static_cast<float>(ox(hx + dx + ((x - hx) * c) - ((y - hy) * s))),
                .y = static_cast<float>(oy(hy + dy + ((x - hx) * s) + ((y - hy) * c))),
                .u = static_cast<float>(lx(x, false)),
                .v = static_cast<float>(ly(y))};
        };
        pushQuad(meshes.pieces, piece(x0, y0), piece(x0, y1), piece(x1, y0), piece(x1, y1));
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
    const bool entryBlend = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entrySrc = 0;
    GLint entryDst = 0;
    GLint entrySrcAlpha = 0;
    GLint entryDstAlpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entrySrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &entryDst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &entrySrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &entryDstAlpha);
    const auto restoreBlend = [&] {
        Render::GL::g_pHyprOpenGL->blend(entryBlend);
        Render::GL::g_pHyprOpenGL->blendFunc(
            static_cast<GLenum>(entrySrc), static_cast<GLenum>(entryDst));
        glBlendFuncSeparate(entrySrc, entryDst, entrySrcAlpha, entryDstAlpha);
    };

    const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    const auto state = ctx.saveDrawState();
    auto &data = ctx.m_data;

    const auto layer = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!layer) {
        return nullptr;
    }

    g_pHyprRenderer->bindFB(ctx, layer);
    data.damage = CRegion{0.0, 0.0, size.x, size.y};
    data.fbSize = size;
    g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
    g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});

    Render::GL::CHyprOpenGLImpl::STextureRenderData td{};
    td.damage = &data.damage;
    td.allowCustomUV = true;
    const CBox box{0.0, 0.0, size.x, size.y};

    // Silhouettes, unblended: tiles never overlap in the layer, and only the alpha they leave is
    // used.
    td.allowDim = false;
    Render::GL::g_pHyprOpenGL->blend(false);
    if (!meshes.mask.empty()) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.mask, td);
    }
    if (!meshes.walkingCut.empty()) {
        Render::GL::g_pHyprOpenGL->blend(true);
        Render::GL::g_pHyprOpenGL->blendFunc(GL_ZERO, GL_SRC_ALPHA);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.walkingCut, td);
    }

    auto smooth = surface.filter(ctx,
        layer,
        size,
        meshes.standingAt,
        meshes.smoothingRadius,
        meshes.cornerRadius,
        meshes.bodies);
    if (!smooth) {
        restoreBlend();
        return nullptr;
    }
    g_pHyprRenderer->bindFB(ctx, smooth);
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (!meshes.creatures.empty()) {
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.creatures, td);
    }

    // The window, weighted by that alpha: pixels outside every silhouette stay transparent.
    td.allowDim = true;
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ZERO);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, window, box, meshes.content, td);

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    restoreBlend();
    return std::exchange(smooth, SP<Render::IFramebuffer>{});
}

CSwarmClosePassElement::CSwarmClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CSwarmClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.sprites || !m_data.meshes || m_data.meshes->pieces.empty()) {
        return {};
    }
    const auto layer = renderSwarmLayer(
        ctx, m_data.tex, m_data.atlas, m_data.layerSize, *m_data.meshes, m_data.sprites->surface());
    if (!layer) {
        return {};
    }

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage = &m_data.damage;
    textureData.a = m_data.alpha;
    textureData.allowCustomUV = true;
    Render::GL::g_pHyprOpenGL->renderTextureMesh(
        ctx, layer->getTexture(), m_data.box, m_data.meshes->pieces, textureData);
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
