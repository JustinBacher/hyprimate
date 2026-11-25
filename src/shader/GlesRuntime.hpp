#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <span>

#include <GLES3/gl3.h>

#include "../compute/Compute.hpp"
#include "Package.hpp"

namespace hyprimate::shader {
    struct Frame {
        std::uint64_t tick = 0, seed = 0;
        Event event = Event::Open;
        float progress = 0, elapsed = 0, delta = 0, duration = 0;
        float width = 1, height = 1, scale = 1;
        float pointer_x = 0.5F, pointer_y = 0.5F;
        bool pointer_valid = false, fullscreen = false;
        std::array<float, 4> geometry{}, previous_geometry{}, velocity{};
        float direction = 0;
        bool held = false, settling = false;
    };
    using Interaction = std::array<std::array<float, 4>, 4>;
    struct EnvironmentInputs {
        GLuint original = 0, processed = 0, state = 0, disturbances = 0;
        std::array<float, 2> output_size{}, output_origin{};
        std::array<float, 4> output_rect{0, 0, 1, 1}, original_uv{0, 0, 1, 1};
        std::span<const Interaction> interactions{};
    };
    struct Source {
        GLuint texture = 0;
        unsigned width = 0, height = 0;
        // Physical dimensions of the backing texture, not of the captured rectangle.
        // Normalized source rectangle in the borrowed texture; top-first.
        std::array<float, 4> uv{0, 0, 1, 1};
        GLuint destination_texture = 0;
        std::array<float, 4> destination_uv{0, 0, 1, 1};
        EnvironmentInputs environment{};
    };
    struct Clip {
        GLint x, y;
        GLsizei width, height;
    };
    struct Target {
        GLuint framebuffer = 0, texture = 0;
        unsigned width = 0, height = 0;
        // Window-normalized coordinates covered by this target.
        std::array<float, 4> domain{0, 0, 1, 1};
        // Pixel origin in color attachment zero. The caller guarantees this
        // region is inside the backing framebuffer. Clears are clipped to it.
        GLint origin_x = 0, origin_y = 0;
        // Private pooled targets must not retain another draw's pixels. Clear
        // once before the first final pass unless that pass already clears.
        bool clear_before_render = false;
        // In-place presentation of a single final pass over host pixels. Clips
        // are disjoint physical framebuffer rectangles within the viewport.
        std::span<const Clip> clips{};
        bool composite = false;
        bool blur_mask = false;
        // Set before any host-visible submission. A false return afterward
        // must not cause replay over potentially modified compositor pixels.
        bool *submitted = nullptr;
    };
    struct Statistics {
        std::uint64_t allocations = 0, allocated_bytes = 0;
        std::uint64_t update_passes = 0, render_passes = 0, draws = 0;
        std::uint64_t initialize_passes = 0;
        std::uint64_t upload_bytes = 0, copy_bytes = 0;
    };
    using GlProc = void (GL_APIENTRYP)();
    using GlResolver = GlProc (*)(const char *);

    // All GPU operations, including destruction/release, require the caller's
    // owning GLES context current and a clean GL error flag. Abandon instead on
    // context loss. An initialized Instance pins its prepared Program generation.
    class Program {
      public:
        Program();
        ~Program();
        Program(const Program &) = delete;
        Program &operator=(const Program &) = delete;
        // Transfer a package into the immutable prepared generation. The resolver
        // is required when the host exposes indexed draw-buffer state.
        bool initialize(Package package, const Values &values, std::string &error,
            GlResolver resolver = nullptr, compute::GlResolver compute_resolver = {},
            compute::Backend backend = compute::Backend::Auto,
            std::shared_ptr<void> compute_owner = {});
        void release() noexcept;
        void abandon() noexcept;
        const Package &package() const;
        bool can_composite_direct() const noexcept;
        // Counters describe preparation of the current generation. Allocations
        // include short-lived one-texel format probes; upload_bytes counts data
        // transferred from the CPU, not probe storage.
        const Statistics &statistics() const noexcept;
        compute::Statistics compute_statistics() const noexcept;
        const compute::Capabilities *compute_capabilities() const noexcept;
      private:
        friend class Instance;
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };
    class Instance {
      public:
        Instance();
        ~Instance();
        Instance(const Instance &) = delete;
        Instance &operator=(const Instance &) = delete;
        // Matching resolved attachment dimensions preserve simulation. Otherwise
        // allocate transactionally and replay initialization. ancillary_bytes
        // reserves the shared instance budget for retained host-owned targets.
        bool initialize(Program &program, unsigned width, unsigned height, std::string &error,
            std::size_t ancillary_bytes = 0);
        // CPU timing only. Older/repeated tick IDs are ignored; pending delta accumulates.
        void advance(const Frame &frame) noexcept;
        // Initializes/updates only on an authorized live execution. Read-only
        // capture rendering never consumes a pending tick or mutates persistent state.
        // Capture uses the last committed frame's uniforms and simulation state.
        bool render(const Source &source, const Target &target, bool advance_simulation,
            std::string &error);
        // Borrowed until this instance is advanced, resized, or destroyed.
        GLuint exported_texture(Ref resource) const noexcept;
        void release() noexcept;
        void abandon() noexcept;
        const Statistics &statistics() const noexcept;
        compute::Statistics compute_statistics() const noexcept;
#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
        std::uint64_t benchmark_cpu_dispatches() const noexcept;
#endif
      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace hyprimate::shader
