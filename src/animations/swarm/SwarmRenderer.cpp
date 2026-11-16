#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "animations/swarm/SwarmRenderer.hpp"

using hyprimate::Vec2;

namespace {

    // Atlas layout, px: a row of cells, each holding one frame with a transparent gutter around it.
    // After the walk frames come three shapes: a solid cell, a disc as wide as a frame, and a solid
    // cell with that disc cut out of it.
    constexpr int FRAME_PX = 192;
    constexpr int GUTTER_PX = 2;
    constexpr int CELL_PX = FRAME_PX + 2 * GUTTER_PX;
    constexpr int SHAPE_CELLS = 3;
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

        constexpr int columns = 4, rows = 2;
        constexpr int frames = columns * rows;
        const int width = (frames + SHAPE_CELLS) * CELL_PX;
        cairo_surface_t *atlas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, CELL_PX);
        cairo_t *cr = cairo_create(atlas);
        const double frameW =
            static_cast<double>(cairo_image_surface_get_width(image)) / columns;
        const double frameH =
            static_cast<double>(cairo_image_surface_get_height(image)) / rows;
        for (int k = 0; k < frames; ++k) {
            const double x = k * CELL_PX + GUTTER_PX;
            cairo_save(cr);
            cairo_rectangle(cr, x, GUTTER_PX, FRAME_PX, FRAME_PX);
            cairo_clip(cr);
            cairo_translate(cr, x, GUTTER_PX);
            cairo_scale(cr, FRAME_PX / frameW, FRAME_PX / frameH);
            cairo_set_source_surface(
                cr, image, -(k % columns) * frameW, -(k / columns) * frameH);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        const double shapes = frames * CELL_PX, middle = 0.5 * CELL_PX;
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
        cairo_rectangle(cr, shapes, 0.0, CELL_PX, CELL_PX);
        cairo_fill(cr);
        cairo_arc(
            cr, shapes + CELL_PX + middle, middle, 0.5 * FRAME_PX, 0.0, 2.0 * std::numbers::pi);
        cairo_fill(cr);
        cairo_rectangle(cr, shapes + 2.0 * CELL_PX, 0.0, CELL_PX, CELL_PX);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_arc(cr,
            shapes + 2.0 * CELL_PX + middle,
            middle,
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
            for (int y = 0; y < CELL_PX; ++y)
                std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4,
                    data + static_cast<std::size_t>(y) * stride,
                    static_cast<std::size_t>(width) * 4);

            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(FRAME_PX) * FRAME_PX);
            sheet.frames.clear();
            for (int k = 0; k < frames; ++k) {
                for (int y = 0; y < FRAME_PX; ++y) {
                    const std::uint8_t *row =
                        pixels.data() + (static_cast<std::size_t>(y + GUTTER_PX) * width +
                                            k * CELL_PX + GUTTER_PX) *
                                            4;
                    for (int x = 0; x < FRAME_PX; ++x)
                        alpha[static_cast<std::size_t>(y) * FRAME_PX + x] = row[x * 4 + 3];
                }
                sheet.frames.push_back(hyprimate::measureSprite(alpha, FRAME_PX));
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
            return SMeshRenderVertex{static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(u),
                static_cast<float>(v)};
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
                const SMaskPoint &a = polygon[i], &b = polygon[(i + 1) % count];
                if (inside(a) && n < kept.size()) kept[n++] = a;
                if (inside(a) != inside(b) && n < kept.size()) {
                    const double t = (limit - value(a)) / (value(b) - value(a));
                    kept[n++] = {a.x + (b.x - a.x) * t,
                        a.y + (b.y - a.y) * t,
                        a.u + (b.u - a.u) * t,
                        a.v + (b.v - a.v) * t};
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
    if (e.texture || e.pixels.empty() || !g_pHyprRenderer) return e.texture;
    const int width = (static_cast<int>(e.sheet.frames.size()) + SHAPE_CELLS) * CELL_PX;
    e.texture = g_pHyprRenderer->createTexture(DRM_FORMAT_ARGB8888,
        e.pixels.data(),
        width * 4,
        Vector2D{static_cast<double>(width), static_cast<double>(CELL_PX)},
        false,
        false);
    if (e.texture) e.pixels = {};
    return e.texture;
}

CBox swarmLayerBox(const CBox &source) {
    const double x0 = std::floor(source.x), y0 = std::floor(source.y);
    return CBox{x0, y0, std::ceil(source.x + source.w) - x0, std::ceil(source.y + source.h) - y0};
}

Vector2D swarmLayerSize(const CBox &source) {
    const CBox box = swarmLayerBox(source);
    return Vector2D{2.0 * box.w + LAYER_GAP, box.h};
}

void fillSwarmMeshes(const hyprimate::Swarm &swarm,
    const CSwarmSprites::SSheet &sheet,
    const CBox &source,
    const CBox &output,
    Vec2 textureOrigin,
    Vec2 textureSize,
    SSwarmMeshes &meshes) {
    meshes.mask.clear();
    meshes.cut.clear();
    meshes.fill.clear();
    meshes.content.clear();
    meshes.pieces.clear();

    const auto agents = swarm.agents();
    const Vec2 size = swarm.size();
    const int frames = static_cast<int>(sheet.frames.size());
    if (agents.empty() || frames == 0 || !(size.x > 0.0 && size.y > 0.0) || source.empty() ||
        output.empty())
        return;

    const CBox box = swarmLayerBox(source);
    const double sx = source.w / size.x, sy = source.h / size.y, unit = std::min(sx, sy);
    // The layer: walking creatures in its left half, everything standing in the window in its
    // right.
    const double width = 2.0 * box.w + LAYER_GAP, standingAt = box.w + LAYER_GAP;
    const double atlasW = (frames + SHAPE_CELLS) * CELL_PX;
    const double solidU = (frames + 0.5) * CELL_PX / atlasW,
                 discU = (frames + 1.5) * CELL_PX / atlasW,
                 holeU = (frames + 2.5) * CELL_PX / atlasW;
    const double radiusU = 0.5 * FRAME_PX / atlasW, radiusV = 0.5 * FRAME_PX / CELL_PX;

    // Layer and output coordinates of a device-px point.
    const auto lx = [&](double x, bool standing) {
        return (x - box.x + (standing ? standingAt : 0.0)) / width;
    };
    const auto ly = [&](double y) { return (y - box.y) / box.h; };
    const auto ox = [&](double x) { return (x - output.x) / output.w; };
    const auto oy = [&](double y) { return (y - output.y) / output.h; };

    // The window, once under each half.
    const double u0 = (box.x - textureOrigin.x) / textureSize.x,
                 u1 = (box.x + box.w - textureOrigin.x) / textureSize.x;
    const double v0 = (box.y - textureOrigin.y) / textureSize.y,
                 v1 = (box.y + box.h - textureOrigin.y) / textureSize.y;
    pushRect(meshes.content, 0.0, 0.0, box.w / width, 1.0, u0, v0, u1, v1);
    pushRect(meshes.content, standingAt / width, 0.0, 1.0, 1.0, u0, v0, u1, v1);

    // An agent stands in the window once it is home and has swung round; until then it is a walking
    // piece.
    const auto standing = [](const hyprimate::SwarmAgent &a) { return a.docked && a.turn <= 0.0; };
    const auto filleted = [](const hyprimate::SwarmAgent &a) {
        return std::ranges::any_of(a.fillet, [](double f) { return f > 0.0; });
    };
    // What stands in the window is drawn first and in one piece, so it has no seams; the walkers go
    // over it.
    if (std::ranges::any_of(
            agents, [&](const auto &a) { return (a.visible && standing(a)) || filleted(a); }))
        pushRect(meshes.pieces,
            ox(box.x),
            oy(box.y),
            ox(box.x + box.w),
            oy(box.y + box.h),
            standingAt / width,
            0.0,
            1.0,
            1.0);

    for (const hyprimate::SwarmAgent &a : agents) {
        const double x0 = source.x + a.tile.x * sx, x1 = source.x + (a.tile.x + a.tile.w) * sx;
        const double y0 = source.y + a.tile.y * sy, y1 = source.y + (a.tile.y + a.tile.h) * sy;
        const double hx = source.x + a.home.x * sx, hy = source.y + a.home.y * sy;
        const bool stands = standing(a);

        // A quarter of a shape cell over the square of `radius` in one corner of the tile, the
        // shape's middle on the square's inner corner: the disc rounds the corner off, the cut-out
        // disc fills it in.
        const auto corner = [&](std::vector<SMeshRenderVertex> &out,
                                std::size_t which,
                                double radius,
                                double shapeU,
                                bool inStanding) {
            const bool right = (which & 1U) != 0, bottom = (which & 2U) != 0;
            const double outerX = right ? x1 : x0, innerX = right ? x1 - radius : x0 + radius;
            const double outerY = bottom ? y1 : y0, innerY = bottom ? y1 - radius : y0 + radius;
            const double outerU = shapeU + (right ? radiusU : -radiusU),
                         outerV = 0.5 + (bottom ? radiusV : -radiusV);
            pushRect(out,
                lx(std::min(outerX, innerX), inStanding),
                ly(std::min(outerY, innerY)),
                lx(std::max(outerX, innerX), inStanding),
                ly(std::max(outerY, innerY)),
                right ? shapeU : outerU,
                bottom ? 0.5 : outerV,
                right ? outerU : shapeU,
                bottom ? outerV : 0.5);
        };

        // The notch between merged neighbours fills in where the tile is, whether or not its agent
        // is there yet.
        for (std::size_t c = 0; c < a.fillet.size(); ++c) {
            if (a.fillet[c] > 0.0) corner(meshes.fill, c, a.fillet[c] * a.fit * unit, holeU, true);
        }
        if (!a.visible) continue;

        // Mask: the whole tile when merged, otherwise the part of the tile its creature's frame
        // covers. The creature is drawn facing its rest direction; a walking piece is then turned
        // as a whole to face its heading.
        if (a.lock >= 1.0)
            pushRect(meshes.mask,
                lx(x0, stands),
                ly(y0),
                lx(x1, stands),
                ly(y1),
                solidU,
                0.5,
                solidU,
                0.5);
        else {
            const int k = hyprimate::walkFrame(a.walk, sheet.stride * 2.0 * a.fit, frames);
            const auto &frame = sheet.frames[static_cast<std::size_t>(k)];
            const double side =
                hyprimate::spriteSize(frame, a.fit * unit, a.reach * std::max(sx, sy), a.lock);
            const double facing = hyprimate::spriteRotation(a.rest, sheet.forward, 1.0);
            const double c = std::cos(facing), s = std::sin(facing);
            // Where a point of the frame lands: the creature's body on the tile's home, turned
            // about it.
            const auto at = [&](double fx, double fy) {
                const double dx = (fx - frame.centre.x) * side, dy = (fy - frame.centre.y) * side;
                return SMaskPoint{hx + dx * c - dy * s,
                    hy + dx * s + dy * c,
                    (k * CELL_PX + GUTTER_PX + fx * FRAME_PX) / atlasW,
                    (GUTTER_PX + fy * FRAME_PX) / CELL_PX};
            };
            const double low = -FRAME_MARGIN, high = 1.0 + FRAME_MARGIN;
            std::array<SMaskPoint, 8> polygon{
                at(low, low), at(high, low), at(high, high), at(low, high)};
            const std::size_t points = side > 0.0 ? clipToBox(polygon, 4, x0, y0, x1, y1) : 0;
            const auto vertex = [&](const SMaskPoint &p) {
                return SMeshRenderVertex{static_cast<float>(lx(p.x, stands)),
                    static_cast<float>(ly(p.y)),
                    static_cast<float>(p.u),
                    static_cast<float>(p.v)};
            };
            for (std::size_t i = 1; i + 1 < points; ++i) {
                meshes.mask.push_back(vertex(polygon[0]));
                meshes.mask.push_back(vertex(polygon[i]));
                meshes.mask.push_back(vertex(polygon[i + 1]));
            }
        }
        // A tile that is more than its creature keeps its rounded corners; a creature alone never
        // reaches them.
        if (a.lock > 0.0) {
            for (std::size_t c = 0; c < a.round.size(); ++c) {
                if (a.round[c] > 0.0)
                    corner(meshes.cut, c, a.round[c] * a.fit * unit, discU, stands);
            }
        }
        if (stands) continue;

        // Piece: the tile, carried to where its agent is and turned about its home.
        const double dx = (a.position.x - a.home.x) * sx, dy = (a.position.y - a.home.y) * sy;
        const double angle = hyprimate::spriteRotation(a.heading, a.rest, a.turn);
        const double c = std::cos(angle), s = std::sin(angle);
        const auto piece = [&](double x, double y) {
            return SMeshRenderVertex{static_cast<float>(ox(hx + dx + (x - hx) * c - (y - hy) * s)),
                static_cast<float>(oy(hy + dy + (x - hx) * s + (y - hy) * c)),
                static_cast<float>(lx(x, false)),
                static_cast<float>(ly(y))};
        };
        pushQuad(meshes.pieces, piece(x0, y0), piece(x0, y1), piece(x1, y0), piece(x1, y1));
    }
}

SP<Render::IFramebuffer> renderSwarmLayer(Render::CRenderContext &ctx,
    const SP<Render::ITexture> &window,
    const SP<Render::ITexture> &atlas,
    const Vector2D &size,
    const SSwarmMeshes &meshes) {
    if (!window || !atlas || meshes.content.empty() || !(size.x > 0.0 && size.y > 0.0))
        return nullptr;

    // Entry blend state, restored on exit; the wrapper does not expose the blend function, so GL is
    // queried for it.
    const bool entryBlend = Render::GL::g_pHyprOpenGL->blendEnabled();
    GLint entrySrc = 0, entryDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &entrySrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &entryDst);

    Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    auto state = ctx.saveDrawState();
    auto &data = ctx.m_data;

    const auto layer = g_pHyprRenderer->getWorkBuffer(ctx, size);
    if (!layer) return nullptr;

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
    if (!meshes.mask.empty())
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.mask, td);

    // Rounded corners: what a quarter disc does not cover of its corner is multiplied away.
    Render::GL::g_pHyprOpenGL->blend(true);
    if (!meshes.cut.empty()) {
        Render::GL::g_pHyprOpenGL->blendFunc(GL_ZERO, GL_SRC_ALPHA);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.cut, td);
    }
    // Filled notches, added on top.
    if (!meshes.fill.empty()) {
        Render::GL::g_pHyprOpenGL->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, atlas, box, meshes.fill, td);
    }

    // The window, weighted by that alpha: pixels outside every silhouette stay transparent.
    td.allowDim = true;
    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->blendFunc(GL_DST_ALPHA, GL_ZERO);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, window, box, meshes.content, td);

    // Restore the entry blend state through the wrapper so its cached factors stay in sync with GL.
    Render::GL::g_pHyprOpenGL->blend(entryBlend);
    Render::GL::g_pHyprOpenGL->blendFunc(
        static_cast<GLenum>(entrySrc), static_cast<GLenum>(entryDst));
    return layer;
}

CSwarmClosePassElement::CSwarmClosePassElement(SData data) : m_data(std::move(data)) {
}

std::vector<UP<IPassElement>> CSwarmClosePassElement::draw(Render::CRenderContext &ctx) {
    if (!m_data.meshes || m_data.meshes->pieces.empty()) return {};
    const auto layer =
        renderSwarmLayer(ctx, m_data.tex, m_data.atlas, m_data.layerSize, *m_data.meshes);
    if (!layer) return {};

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
