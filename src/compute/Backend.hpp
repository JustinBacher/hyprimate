#pragma once

#include "Compute.hpp"

#include <GLES3/gl3.h>

namespace hyprimate::compute::detail {
    inline constexpr GLbitfield FETCH_BARRIER = 0x00000008;
    inline constexpr GLbitfield IMAGE_BARRIER = 0x00000020;
    inline constexpr GLbitfield UPDATE_BARRIER = 0x00000100;
    inline constexpr GLbitfield FRAMEBUFFER_BARRIER = 0x00000400;

    struct OutputBinding { GLuint texture; GLenum format; };
    struct ImageBinding { GLint name{}, level{}, layered{}, layer{}, access{}, format{}; };
    struct NativeBackend {
        using Dispatch = void (GL_APIENTRYP)(GLuint, GLuint, GLuint);
        using BindImage = void (GL_APIENTRYP)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
        using Barrier = void (GL_APIENTRYP)(GLbitfield);
        Dispatch dispatch{};
        BindImage bind_image{};
        Barrier barrier{};
        [[nodiscard]] Result<void> configure(const GlResolver&, Capabilities&);
        [[nodiscard]] Result<void> probe();
        [[nodiscard]] Result<GLuint> prepare(const KernelDesc&) const;
        void capture(std::span<ImageBinding>) const;
        void restore(std::span<const ImageBinding>) const noexcept;
        [[nodiscard]] Result<void> submit(Layout, std::span<const OutputBinding>, const Capabilities&) const;
    };

    struct RasterBackend;
    struct RasterDeleter { void operator()(RasterBackend*) const noexcept; };
    using RasterPointer = std::unique_ptr<RasterBackend, RasterDeleter>;
    // Raster backend owns its private FBO, VAO and reusable attachment/state arrays.
    [[nodiscard]] Result<RasterPointer> configure_raster(const GlResolver&, Capabilities&, std::span<const std::string_view> extensions);
    void release_raster(RasterBackend&) noexcept;
    [[nodiscard]] Result<GLuint> prepare_raster(const KernelDesc&);
    void begin_raster(RasterBackend&, unsigned outputs);
    void end_raster(RasterBackend&) noexcept;
    [[nodiscard]] Result<void> submit_raster(RasterBackend&, Layout, std::span<const OutputBinding>);

    [[nodiscard]] inline GLenum format(Type type) noexcept {
        if (type.scalar == Element::Int) return type.lanes == 1 ? GL_R32I : GL_RGBA32I;
        return type.lanes == 1 ? GL_R32UI : GL_RGBA32UI;
    }
    [[nodiscard]] inline GLint integer(GLenum name) noexcept { GLint value{}; glGetIntegerv(name, &value); return value; }
}
