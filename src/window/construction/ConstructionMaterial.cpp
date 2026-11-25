#include "window/construction/ConstructionMaterial.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>

#include <png.h>

#include "construction_atlas_generated.hpp"
#include "gl/Storage.hpp"

namespace hyprimate {
namespace {

static_assert(std::is_standard_layout_v<ConstructionQuad> && sizeof(ConstructionQuad) <= 64);
// Keep the instance flags in ConstructionShaders.hpp in sync with the typed producer.
static_assert(static_cast<unsigned>(ConstructionSurface::Solid) == 0 &&
    static_cast<unsigned>(ConstructionSurface::Client) == 1 &&
    static_cast<unsigned>(ConstructionSurface::Atlas) == 2 &&
    static_cast<unsigned>(ConstructionSurface::Shadow) == 3);

bool finite_box(const Box &box) noexcept {
    return is_usable(box) && std::isfinite(box.x + box.w) && std::isfinite(box.y + box.h);
}

bool pixel_extent(double size) noexcept {
    return size >= 1 && size == std::floor(size) && std::isfinite(size);
}

bool finite(const auto &values) noexcept {
    return std::ranges::all_of(values, [](float v) { return std::isfinite(v); });
}

void instance_attribute(GLuint location, GLint components, GLenum type, GLboolean normalized,
    std::size_t offset) noexcept {
    glEnableVertexAttribArray(location);
    // Offsets into the bound instance VBO, not CPU pointers.
    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
    glVertexAttribPointer(location, components, type, normalized, sizeof(ConstructionQuad),
        reinterpret_cast<const void *>(offset));
    glVertexAttribDivisor(location, 1);
}

bool valid_attachment(GLuint image, GLuint output_texture) noexcept {
    if (!image || !output_texture || image == output_texture ||
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !gl::framebuffer_attachment_is(GL_COLOR_ATTACHMENT0, output_texture, GL_TEXTURE) ||
        !gl::framebuffer_has_single_draw_buffer(GL_COLOR_ATTACHMENT0))
        return false;
    GLint alpha = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &alpha);
    if (alpha < 8) return false;
    GLint component_type = 0, encoding = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component_type);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
    return (component_type == GL_UNSIGNED_NORMALIZED || component_type == GL_FLOAT) &&
        encoding == GL_LINEAR;
}

struct ClearLayer {
    const bool dither = glIsEnabled(GL_DITHER) == GL_TRUE;
    ClearLayer() {
        // Preserve exact identity pixels on integer-aligned SDR output.
        if (dither) glDisable(GL_DITHER);
        constexpr std::array<float, 4> CLEAR{};
        glClearBufferfv(GL_COLOR, 0, CLEAR.data());
    }
    ~ClearLayer() { if (dither) glEnable(GL_DITHER); }
};

}  // namespace

std::optional<CConstructionMaterial::View> CConstructionMaterial::prepare_view(
    const SConstructionTarget &t) noexcept {
    if (!finite_box(t.canvas) || !finite_box(t.source) || !finite_box(t.body) ||
        !finite_box(t.capture) || !pixel_extent(t.canvas.w) || !pixel_extent(t.canvas.h) ||
        !pixel_extent(t.image_size.x) || !pixel_extent(t.image_size.y) ||
        !std::isfinite(t.image_origin.x) || !std::isfinite(t.image_origin.y) ||
        !std::isfinite(t.scale) || t.scale <= 0 || t.canvas.w > MAX_TARGET_EDGE ||
        t.canvas.h > MAX_TARGET_EDGE || t.canvas.w * t.canvas.h > MAX_TARGET_PIXELS ||
        std::abs(t.source.w - t.capture.w * t.scale) > 1 ||
        std::abs(t.source.h - t.capture.h * t.scale) > 1)
        return std::nullopt;

    const double X = t.source.x - t.image_origin.x, Y = t.source.y - t.image_origin.y;
    const double LEFT = std::max(0.0, X), TOP = std::max(0.0, Y);
    const double RIGHT = std::min({t.image_size.x, X + t.source.w, X + t.capture.w * t.scale});
    const double BOTTOM = std::min({t.image_size.y, Y + t.source.h, Y + t.capture.h * t.scale});

    // Select actual texel centers in the half-open authorized intersection, not a half-pixel
    // gutter on every panel (or on a fractional source edge). This rules out foreign padding
    // in the borrowed framebuffer while adjacent panels share a continuous filter domain.
    const double FIRST_X = std::ceil(LEFT - 0.5) + 0.5;
    const double FIRST_Y = std::ceil(TOP - 0.5) + 0.5;
    const double LAST_X = std::ceil(RIGHT - 0.5) - 0.5;
    const double LAST_Y = std::ceil(BOTTOM - 0.5) - 0.5;
    const bool HAS_CROP = RIGHT > LEFT && BOTTOM > TOP && LAST_X >= FIRST_X && LAST_Y >= FIRST_Y;
    if (!HAS_CROP && !t.allow_empty_crop) return std::nullopt;
    const Box LOCAL{(LEFT - X) / t.scale, (TOP - Y) / t.scale,
        (RIGHT - LEFT) / t.scale, (BOTTOM - TOP) / t.scale};

    const double PIVOT_X = (t.body.x - t.capture.x) + t.body.w * 0.5;
    const double PIVOT_Y = (t.body.y - t.capture.y) + t.body.h * 0.5;
    const double FOCAL = 4 * std::max({t.body.w, t.body.h, 64.0});
    const double CANVAS_X = (t.source.x - t.canvas.x) + PIVOT_X * t.scale;
    const double CANVAS_Y = (t.source.y - t.canvas.y) + PIVOT_Y * t.scale;
    // The native export pass places logical top on the texture's first GL row.
    const double Y_SIGN = t.output_top_first ? 1 : -1;
    const double GRID_X = t.canvas.x - t.image_origin.x, GRID_Y = t.canvas.y - t.image_origin.y;
    // Canvas and image origins may each be nonzero, but sample centers coincide if their
    // difference is integral. Leave headroom for viewport-local coordinates added in GLSL.
    constexpr double GRID_LIMIT = std::numeric_limits<GLint>::max() - MAX_TARGET_EDGE;
    std::array<GLint, 3> texel_grid{};
    if (std::abs(GRID_X) <= GRID_LIMIT && std::abs(GRID_Y) <= GRID_LIMIT &&
        GRID_X == std::floor(GRID_X) && GRID_Y == std::floor(GRID_Y)) {
        texel_grid = {static_cast<GLint>(GRID_X),
            static_cast<GLint>(GRID_Y + (t.output_top_first ? 0 : t.canvas.h - 1)),
            t.output_top_first ? 1 : -1};
    }
    View view{.width = static_cast<GLsizei>(t.canvas.w),
        .height = static_cast<GLsizei>(t.canvas.h),
        .pivot = {static_cast<float>(PIVOT_X), static_cast<float>(PIVOT_Y),
            static_cast<float>(1 / FOCAL)},
        .canvas = {static_cast<float>(2 * CANVAS_X / t.canvas.w - 1),
            static_cast<float>(Y_SIGN * (2 * CANVAS_Y / t.canvas.h - 1)),
            static_cast<float>(2 * t.scale / t.canvas.w),
            static_cast<float>(Y_SIGN * 2 * t.scale / t.canvas.h)},
        .crop = HAS_CROP ? std::array{static_cast<float>(LOCAL.x), static_cast<float>(LOCAL.y),
            static_cast<float>((RIGHT - X) / t.scale), static_cast<float>((BOTTOM - Y) / t.scale)} :
            std::array<float, 4>{},
        .client_map = {static_cast<float>(X), static_cast<float>(Y),
            static_cast<float>(t.scale), static_cast<float>(t.scale)},
        .sample_bounds = HAS_CROP ? std::array{static_cast<float>(FIRST_X), static_cast<float>(FIRST_Y),
            static_cast<float>(LAST_X), static_cast<float>(LAST_Y)} : std::array<float, 4>{},
        .inverse_image = {static_cast<float>(1 / t.image_size.x),
            static_cast<float>(1 / t.image_size.y)},
        .texel_grid = texel_grid,
        .authorized_local_crop = HAS_CROP ? std::optional{LOCAL} : std::nullopt,
        .rgbx = t.image_rgbx,
        .mask_mode = t.mask_mode};
    if (!finite(view.pivot) || !finite(view.canvas) || !finite(view.crop) ||
        !finite(view.client_map) || !finite(view.sample_bounds) ||
        (HAS_CROP && (view.crop[0] >= view.crop[2] || view.crop[1] >= view.crop[3])) ||
        view.canvas[2] <= 0 || view.canvas[3] == 0 || view.pivot[2] <= 0 ||
        view.client_map[2] <= 0 || view.inverse_image[0] <= 0 || view.inverse_image[1] <= 0)
        return std::nullopt;
    if (t.mask_mode == ConstructionMaskMode::Body) {
        if (!std::isfinite(t.mask_rounding) || t.mask_rounding < 0 ||
            !(1 <= t.mask_rounding_power && t.mask_rounding_power <= std::numeric_limits<float>::max()))
            return std::nullopt;
        const double BODY_X = X + (t.body.x - t.capture.x) * t.scale;
        const double BODY_Y = Y + (t.body.y - t.capture.y) * t.scale;
        const double BODY_W = t.body.w * t.scale, BODY_H = t.body.h * t.scale;
        view.mask_body = {static_cast<float>(BODY_X), static_cast<float>(BODY_Y),
            static_cast<float>(BODY_X + BODY_W), static_cast<float>(BODY_Y + BODY_H)};
        if (!finite(view.mask_body) || view.mask_body[0] >= view.mask_body[2] ||
            view.mask_body[1] >= view.mask_body[3])
            return std::nullopt;
        view.mask_rounding = static_cast<float>(std::min({t.mask_rounding, BODY_W * 0.5, BODY_H * 0.5}));
        view.mask_rounding_power = static_cast<float>(t.mask_rounding_power);
    }
    return view;
}

bool CConstructionMaterial::supports_view(const View &view) noexcept {
    if (m_target_limit[0] == 0) {
        GLint texture = 0, renderbuffer = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer);
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, m_target_limit.data());
        for (auto &edge : m_target_limit)
            edge = std::min({edge, texture, renderbuffer, MAX_TARGET_EDGE});
    }
    return view.width > 0 && view.height > 0 && view.width <= m_target_limit[0] &&
        view.height <= m_target_limit[1] &&
        static_cast<double>(view.width) * view.height <= MAX_TARGET_PIXELS;
}

bool CConstructionMaterial::upload_atlas() noexcept {
    using namespace construction_atlas;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    struct ImageGuard {
        png_image &image;
        ~ImageGuard() { png_image_free(&image); }
    } guard{image};
    if (!png_image_begin_read_from_memory(&image, PNG.data(), PNG.size()) ||
        image.width != WIDTH || image.height != HEIGHT)
        return false;
    image.format = PNG_FORMAT_RGBA;
    constexpr std::size_t BYTES = static_cast<std::size_t>(WIDTH) * HEIGHT * 4;
    const std::unique_ptr<unsigned char[]> pixels{new (std::nothrow) unsigned char[BYTES]};
    if (!pixels || !png_image_finish_read(&image, nullptr, pixels.get(), 0, nullptr)) return false;
    // libpng's byte RGBA is straight alpha. Premultiply before the GPU filters across gutters.
    for (std::size_t i = 0; i < BYTES; i += 4) {
        const unsigned alpha = pixels[i + 3];
        for (std::size_t c = 0; c < 3; ++c)
            pixels[i + c] = static_cast<unsigned char>((pixels[i + c] * alpha + 127) / 255);
    }

    glGenTextures(1, &m_atlas);
    if (!m_atlas) return false;
    const gl::TextureUploadBinding BINDING;
    glBindTexture(GL_TEXTURE_2D, m_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!gl::allocate_texture_2d(GL_RGBA8, WIDTH, HEIGHT)) return false;
    const gl::ClientPixelUnpack UNPACK;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    return true;
}

bool CConstructionMaterial::initialize(GLuint program) noexcept {
    if (m_program) return m_program == program;
    if (!program) return false;
    m_uniforms = {.pivot = glGetUniformLocation(program, "u_pivot"),
        .canvas = glGetUniformLocation(program, "u_canvas"),
        .crop = glGetUniformLocation(program, "u_crop"),
        .client_map = glGetUniformLocation(program, "u_client_map"),
        .sample_bounds = glGetUniformLocation(program, "u_sample_bounds"),
        .inverse_image = glGetUniformLocation(program, "u_inverse_image"),
        .texel_grid = glGetUniformLocation(program, "u_texel_grid"),
        .rgbx = glGetUniformLocation(program, "u_rgbx"),
        .mask_mode = glGetUniformLocation(program, "u_mask_mode"),
        .mask_body = glGetUniformLocation(program, "u_mask_body"),
        .mask_rounding = glGetUniformLocation(program, "u_mask_rounding"),
        .image = glGetUniformLocation(program, "u_image"),
        .atlas = glGetUniformLocation(program, "u_atlas"),
        .mesh = glGetUniformLocation(program, "u_mesh")};
    if (std::min({m_uniforms.pivot, m_uniforms.canvas, m_uniforms.crop, m_uniforms.client_map,
            m_uniforms.sample_bounds, m_uniforms.inverse_image, m_uniforms.rgbx, m_uniforms.texel_grid,
            m_uniforms.mask_mode, m_uniforms.mask_body, m_uniforms.mask_rounding,
            m_uniforms.image, m_uniforms.atlas, m_uniforms.mesh}) < 0)
        return false;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_instances);
    glGenSamplers(1, &m_client_sampler);
    if (!m_vao || !m_instances || !m_client_sampler) {
        release();
        return false;
    }
    // Override filtering with our sampler, never with changes to the borrowed texture.
    glSamplerParameteri(m_client_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(m_client_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(m_client_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_client_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    if (!gl::allocate_buffer(GL_ARRAY_BUFFER, sizeof(ConstructionBatch::quads), nullptr, GL_STREAM_DRAW) ||
        !upload_atlas()) {
        release();
        return false;
    }
    instance_attribute(0, 3, GL_FLOAT, GL_FALSE, offsetof(ConstructionQuad, origin));
    instance_attribute(1, 3, GL_FLOAT, GL_FALSE, offsetof(ConstructionQuad, u));
    instance_attribute(2, 3, GL_FLOAT, GL_FALSE, offsetof(ConstructionQuad, v));
    instance_attribute(3, 4, GL_FLOAT, GL_FALSE, offsetof(ConstructionQuad, uv));
    instance_attribute(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(ConstructionQuad, tint));
    glEnableVertexAttribArray(5);
    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
    glVertexAttribIPointer(5, 1, GL_UNSIGNED_INT, sizeof(ConstructionQuad),
        reinterpret_cast<const void *>(offsetof(ConstructionQuad, surface)));
    glVertexAttribDivisor(5, 1);
    glUseProgram(program);
    glUniform1i(m_uniforms.image, 0);
    glUniform1i(m_uniforms.atlas, 1);
    m_program = program;
    return true;
}

bool CConstructionMaterial::initialize_close() noexcept {
    using Frame = construction::studies::RegionalClose::Frame;
    using IndexFace = construction::studies::RegionalClose::SourceTriangle;
    static_assert(sizeof(IndexFace) == 3 * sizeof(GLushort));
    static_assert(sizeof(MeshVertex) == 5 * sizeof(float));
    if (m_close_vao) return true;

    glGenVertexArrays(1, &m_close_vao);
    glGenBuffers(1, &m_close_vertices);
    glGenBuffers(1, &m_close_indices);
    if (!m_close_vao || !m_close_vertices || !m_close_indices) {
        release_close();
        return false;
    }
    glBindVertexArray(m_close_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_close_vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_close_indices);
    if (!gl::allocate_buffer(GL_ARRAY_BUFFER, Frame::MAX_VERTICES * sizeof(MeshVertex),
            nullptr, GL_STREAM_DRAW) ||
        !gl::allocate_buffer(GL_ELEMENT_ARRAY_BUFFER, Frame::MAX_TRIANGLES * sizeof(IndexFace),
            nullptr, GL_STREAM_DRAW)) {
        release_close();
        return false;
    }
    m_close_upload.reset(new (std::nothrow) MeshVertex[Frame::MAX_VERTICES]);
    if (!m_close_upload) {
        release_close();
        return false;
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);
    glEnableVertexAttribArray(3);
    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
        reinterpret_cast<const void *>(offsetof(MeshVertex, source)));
    return true;
}

void CConstructionMaterial::bind_view(const View &view, GLuint image) const noexcept {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    glUniform3fv(m_uniforms.pivot, 1, view.pivot.data());
    glUniform4fv(m_uniforms.canvas, 1, view.canvas.data());
    glUniform4fv(m_uniforms.crop, 1, view.crop.data());
    glUniform4fv(m_uniforms.client_map, 1, view.client_map.data());
    glUniform4fv(m_uniforms.sample_bounds, 1, view.sample_bounds.data());
    glUniform2fv(m_uniforms.inverse_image, 1, view.inverse_image.data());
    glUniform3iv(m_uniforms.texel_grid, 1, view.texel_grid.data());
    glUniform1i(m_uniforms.rgbx, view.rgbx ? 1 : 0);
    glUniform1i(m_uniforms.mask_mode, static_cast<GLint>(view.mask_mode));
    if (view.mask_mode == ConstructionMaskMode::Body) {
        glUniform4fv(m_uniforms.mask_body, 1, view.mask_body.data());
        glUniform2f(m_uniforms.mask_rounding, view.mask_rounding, view.mask_rounding_power);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image);
    glBindSampler(0, m_client_sampler);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_atlas);
    glBindSampler(1, 0);
}

void CConstructionMaterial::draw_instances(const ConstructionBatch &batch) const noexcept {
    glUniform1i(m_uniforms.mesh, 0);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(batch.count * sizeof(ConstructionQuad)), batch.quads.data());
    // Primitive order is the batch's paint order across every material variant.
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(batch.count));
}

bool CConstructionMaterial::draw(const ConstructionBatch &batch, const View &view, GLuint image,
    GLuint output_texture) const noexcept {
    if (!m_program || batch.overflow || batch.count > batch.quads.size() ||
        !view.authorized_local_crop || !valid_attachment(image, output_texture))
        return false;
    const ClearLayer CLEAR;
    if (batch.count != 0) {
        bind_view(view, image);
        draw_instances(batch);
    }
    return true;
}

bool CConstructionMaterial::draw_close(const construction::studies::RegionalClose::Frame &frame,
    const ConstructionBatch &effects, const View &view, GLuint image, GLuint output_texture) noexcept {
    const auto faces = frame.source_faces();
    if (effects.overflow || effects.count > effects.quads.size() ||
        std::ranges::any_of(std::span{effects.quads}.first(effects.count),
            [](const ConstructionQuad &quad) { return quad.surface == ConstructionSurface::Client; }) ||
        ((!faces.empty() || effects.count != 0) && (!m_program || !view.authorized_local_crop)) ||
        !valid_attachment(image, output_texture) || (!faces.empty() && !initialize_close()))
        return false;

    const ClearLayer CLEAR;
    if (faces.empty() && effects.count == 0) return true;
    bind_view(view, faces.empty() ? 0 : image);
    if (!faces.empty()) {
        const auto vertices = frame.source_vertices();
        bool identity = true;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto &[source, position] = vertices[i];
            m_close_upload[i] = {{static_cast<float>(position.x), static_cast<float>(position.y),
                static_cast<float>(position.z)}, {static_cast<float>(source.x), static_cast<float>(source.y)}};
            identity = identity && position.x == source.x && position.y == source.y && position.z == 0;
        }
        // Preserve source-over in stable publication order where two poses overlap.
        // Depth rejection or replacement would hide the earlier print through a cutout.
        glUniform1i(m_uniforms.mesh, identity ? 2 : 1);
        glBindVertexArray(m_close_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_close_vertices);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), m_close_upload.get());
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(faces.size_bytes()), faces.data());
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(faces.size() * 3), GL_UNSIGNED_SHORT, nullptr);
    }
    if (effects.count != 0) draw_instances(effects);
    return true;
}

void CConstructionMaterial::release_close() noexcept {
    if (m_close_vao) glDeleteVertexArrays(1, &m_close_vao);
    if (m_close_vertices) glDeleteBuffers(1, &m_close_vertices);
    if (m_close_indices) glDeleteBuffers(1, &m_close_indices);
    m_close_vao = m_close_vertices = m_close_indices = 0;
    m_close_upload.reset();
}

void CConstructionMaterial::release() noexcept {
    release_close();
    glDeleteSamplers(1, &m_client_sampler);
    glDeleteTextures(1, &m_atlas);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_instances);
    m_program = m_vao = m_instances = m_atlas = m_client_sampler = 0;
}

}  // namespace hyprimate
