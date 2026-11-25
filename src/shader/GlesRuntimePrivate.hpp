#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../compute/Compute.hpp"
#include "GlesState.hpp"

#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
#include "compute/tests/NestedCpuAdapter.hpp"
#endif

namespace hyprimate::shader::gles_detail {
    inline constexpr unsigned MAX_EXTENT = 8192;
    inline constexpr std::uint64_t MAX_PIXELS = 16'777'216;
    inline constexpr std::array<GLfloat, 4> TRANSPARENT{};
    inline constexpr Statistics EMPTY_STATS{};

    struct PixelFormat {
        GLenum internal, layout, type;
        unsigned bytes;
    };

    constexpr PixelFormat pixel_format(Format format) {
        switch (format) {
            case Format::R8:
                return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
            case Format::RG8:
                return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
            case Format::RGBA8:
                return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
            case Format::R16F:
                return {GL_R16F, GL_RED, GL_HALF_FLOAT, 2};
            case Format::RG16F:
                return {GL_RG16F, GL_RG, GL_HALF_FLOAT, 4};
            case Format::RGBA16F:
                return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
            case Format::R32F:
                return {GL_R32F, GL_RED, GL_FLOAT, 4};
            case Format::RG32F:
                return {GL_RG32F, GL_RG, GL_FLOAT, 8};
            case Format::RGBA32F:
                return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
        }
        return {};
    }

    struct Extent {
        unsigned width = 0, height = 0;
        bool operator==(const Extent &) const = default;

        bool bounded() const {
            return width != 0 && height != 0 && width <= MAX_EXTENT && height <= MAX_EXTENT &&
                   std::uint64_t{width} * height <= MAX_PIXELS;
        }
    };

    inline Extent texture_extent(const Size &size, Extent capture) {
        if (size.width != 0) {
            return {size.width, size.height};
        }
        return {std::max(1U, static_cast<unsigned>(std::ceil(capture.width * double{size.scale}))),
            std::max(1U, static_cast<unsigned>(std::ceil(capture.height * double{size.scale})))};
    }

    inline void texture_storage(GLuint &name, const Texture &texture, Extent extent) {
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexStorage2D(
            GL_TEXTURE_2D, 1, pixel_format(texture.format).internal, extent.width, extent.height);
        const GLint filter = texture.linear ? GL_LINEAR : GL_NEAREST;
        const GLint wrap = texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    }

    inline constexpr GLenum draw_primitive(Primitive primitive) {
        switch (primitive) {
            case Primitive::Points:
                return GL_POINTS;
            case Primitive::Lines:
                return GL_LINES;
            case Primitive::Triangles:
                return GL_TRIANGLES;
            case Primitive::TriangleStrip:
                return GL_TRIANGLE_STRIP;
        }
        return GL_TRIANGLES;
    }

    inline bool linear_host_image(ResourceKind kind) noexcept {
        return kind == ResourceKind::Source || kind == ResourceKind::Destination ||
               kind == ResourceKind::Original || kind == ResourceKind::Processed;
    }

    inline GLuint environmental_texture(
        ResourceKind kind, const EnvironmentInputs &inputs) noexcept {
        switch (kind) {
            case ResourceKind::Original:
                return inputs.original;
            case ResourceKind::Processed:
                return inputs.processed;
            case ResourceKind::EnvironmentState:
                return inputs.state;
            case ResourceKind::Disturbances:
                return inputs.disturbances;
            default:
                return 0;
        }
    }

    inline const Mesh *pass_mesh(const Package &package, const Pass &pass) {
        return pass.mesh < 0 ? nullptr : &package.meshes[static_cast<unsigned>(pass.mesh)];
    }

    enum Uniform : std::size_t {
        Progress,
        Elapsed,
        Delta,
        Duration,
        Scale,
        SizeXY,
        TargetSize,
        Pointer,
        Seed,
        SourceUV,
        Domain,
        EventID,
        PointerValid,
        Fullscreen,
        Iteration,
        Iterations,
        DestinationUV,
        Geometry,
        PreviousGeometry,
        Velocity,
        Direction,
        Held,
        Settling,
        BlurMask,
        OutputSize,
        OutputOrigin,
        OutputRect,
        OriginalUV,
        OriginalAvailable,
        ProcessedAvailable,
        StateAvailable,
        DisturbancesAvailable,
        InteractionCount,
        UniformCount
    };

    struct Block {
        Ref buffer;
        GLsizeiptr bytes;
    };

    struct CompiledKernel {
        compute::Kernel kernel;
        std::size_t batch_offset;
    };

    struct CompiledPass {
        GLuint program = 0;
        std::array<GLint, UniformCount> uniforms;
        std::array<Ref, MAX_SHADER_SAMPLERS> samplers{};
        std::unique_ptr<std::array<GLint, MAX_SHADER_SAMPLERS>> field_sizes;
        std::unique_ptr<CompiledKernel> kernel;
        GLint interactions = -1;
        GLint interaction_vectors = 0;
        bool uses_environment = false;
        std::array<Block, MAX_BLOCK_BINDINGS> blocks{};
        unsigned sampler_count = 0, block_count = 0;

        CompiledPass() {
            uniforms.fill(-1);
        }
    };

    struct TexturePair {
        Extent size;
        std::array<GLuint, 2> names{};
        unsigned read = 0;
    };

    struct BufferPair {
        std::array<GLuint, 2> names{};
        unsigned read = 0;
    };

    struct DrawFramebuffer {
        GLuint name = 0;
        Extent size;
        std::array<GLuint, MAX_TEXTURES> attached{};
    };

    struct VertexArray {
        GLuint name = 0;
        std::array<GLuint, 16> attached{};
    };

    inline std::array<float, 4> seed_values(std::uint64_t seed) {
        std::array<float, 4> values{};
        for (auto &value : values) {
            // SplitMix64. A high 24-bit integer converts to float without rounding to 1.
            auto x = (seed += 0x9e3779b97f4a7c15ULL);
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            x ^= x >> 31;
            value = static_cast<float>(x >> 40) * (1.F / 16777216.F);
        }
        return values;
    }

    struct Timing {
        Frame frame;
        std::array<float, 4> seed = seed_values(0);
    };
    struct FieldPairStorage {
        compute::FieldDesc descriptor;
        std::array<compute::Field, 2> sides;
        unsigned read = 0;
    };
}  // namespace hyprimate::shader::gles_detail

namespace hyprimate::shader {
    struct Program::Impl {
        const Package package;
        Values values;
        gles_detail::Limits limits;
        gles_detail::IndexedColor color;
        Statistics stats;
        std::array<gles_detail::CompiledPass, MAX_PASSES> passes;
        std::array<GLuint, MAX_TEXTURES> assets{};
        std::array<GLuint, MAX_MESHES> elements{};
        std::unique_ptr<compute::Device> compute_device;
        std::shared_ptr<void> compute_owner;
        std::vector<std::reference_wrapper<const compute::Kernel>> compute_kernels;
        bool compute_only_simulation = false;
#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
        std::optional<compute::fixture::Workload> benchmark_workload;
#endif
        GLuint source_sampler = 0;
        unsigned sampler_count = 0, block_count = 0, max_outputs = 1;
        bool has_feedback = false, samples_borrowed_target = false, blends_final = false,
             armed = true;
        unsigned final_draws = 0;
        bool final_is_last = false, uses_frag_coord = false, uses_destination = false;
        bool final_source_over = true;

        bool direct() const noexcept;
        explicit Impl(Package spec, const Values &resolved_values, std::shared_ptr<void> owner);
        ~Impl();
        bool prepare(const Values &values, GlResolver resolver,
            compute::GlResolver compute_resolver, compute::Backend backend, std::string &error);
        bool preflight_formats(std::string &error);
        bool upload(std::string &error);
    };


    struct Instance::Impl {
        std::shared_ptr<Program::Impl> owner;
        gles_detail::Extent capture;
        Statistics stats;
        std::size_t ancillary_bytes = 0;
        std::uint64_t asset_budget_bytes = 0;
        compute::Statistics compute_stats;
        std::vector<gles_detail::FieldPairStorage> fields;
#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
        std::unique_ptr<compute::fixture::NestedCpuAdapter> benchmark_cpu;
#endif
        std::array<gles_detail::TexturePair, MAX_TEXTURES> textures;
        std::array<gles_detail::BufferPair, MAX_BUFFERS> buffers;
        std::array<gles_detail::DrawFramebuffer, MAX_PASSES> framebuffers;
        std::array<gles_detail::VertexArray, MAX_MESHES> vaos;
        std::vector<GLenum> saved_draw_buffers;
        GLuint empty_vao = 0, feedback = 0;
        gles_detail::Timing pending_frame, committed_frame;
        bool have_tick = false, pending = false, primed = false, faulted = false, armed = true;

        ~Impl();
        bool size_storage(std::string &error);
        GLuint texture(Ref ref, const Source &source) const;
        compute::Field &field(Ref ref);
        GLuint buffer(Ref ref) const;
        void configure_mesh(unsigned index, bool create);
        void attach_outputs(gles_detail::DrawFramebuffer &fbo, const Pass &pass);
        bool clear_texture(gles_detail::PrepareState &saved,
            GLuint name,
            gles_detail::PixelFormat format,
            gles_detail::Extent size,
            std::string &error);
        bool allocate(std::string &error);
        bool owns_texture(GLuint name) const;
        bool validate_target(const Source &source, const Target &target, std::string &error) const;
        void submit_mesh(const Pass &pass);
        bool draw(std::size_t index,
            unsigned iteration,
            const gles_detail::Timing &time,
            const Source &source,
            const Target &target,
            unsigned &active_blocks,
            bool &clear_final,
            std::string &error);
        bool dispatch_compute(std::size_t pass_index, unsigned iteration,
            const gles_detail::Timing &time, compute::Batch &batch, std::string &error);
        bool phase(Phase phase,
            const gles_detail::Timing &frame,
            const Source &source,
            const Target &target,
            unsigned &active_blocks,
            bool &clear_final,
            std::string &error);
    };
} // namespace hyprimate::shader
