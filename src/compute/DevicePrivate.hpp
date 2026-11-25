#pragma once

#include "Backend.hpp"

#include <atomic>
#include <list>
#include <thread>
#include <unordered_set>

namespace hyprimate::compute::detail {
    struct Storage {
        GLuint name{};
        GLenum format{};
        unsigned width{}, height{};
        std::size_t bytes{};
        std::uint64_t access{}, image_write{};
        bool faulted{};
        Storage* deferred_next{};
    };
    struct Program {
        struct Input { GLint sampler, size; };
        KernelDesc desc;
        Backend backend = Backend::Auto;
        std::string fallback;
        GLuint name{};
        GLint domain = -1, width = -1;
        std::vector<Input> inputs;
        std::vector<GLint> parameters;
    };
    struct Unit { GLint texture{}, sampler{}; bool used{}; };
    struct Scope {
        std::vector<Unit> units;
        std::vector<ImageBinding> images;
        std::vector<OutputBinding> outputs;
        unsigned samplers{}, native_outputs{}, raster_outputs{};
        GLuint program{};
        GLenum active_texture{};
        bool active{}, faulted{}, uses_samplers{};
    };
    struct State {
        using ColorMask = void(GL_APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
        using TestIndexed = GLboolean(GL_APIENTRYP)(GLenum, GLuint);
        using SetIndexed = void(GL_APIENTRYP)(GLenum, GLuint);
        GlResolver resolver;
        Backend preference = Backend::Auto;
        std::thread::id thread;
        Capabilities caps;
        Statistics stats;
        NativeBackend native;
        RasterPointer raster;
        Scope scope;
        GLuint draw_fb{}, read_fb{};
        ColorMask indexed_mask{};
        TestIndexed scissor_enabled{};
        SetIndexed enable_scissor{}, disable_scissor{};
        bool primitives_query{}, window_rectangles{}, reverse_pack_rows{}, alive = true;
        std::atomic_bool stranded_batch{};
        std::atomic<Storage*> deferred{};
        std::list<std::unique_ptr<Storage>> idle;
        std::unordered_set<GLuint> names;
        std::vector<std::shared_ptr<const Program>> kernels;
        std::vector<std::vector<GLenum>> raster_formats;
        std::unique_ptr<std::uint32_t[]> staging;
        std::uint64_t serial{}, fetch{}, framebuffer{}, update{}, image{};

        ~State();
        [[nodiscard]] bool current() const noexcept {
            return thread == std::this_thread::get_id() && alive && resolver.is_current(resolver.owner);
        }
        [[nodiscard]] Result<void> enter(bool inside_batch = false) const;
        [[nodiscard]] Result<void> can_change_program() const;
        [[nodiscard]] bool rectangles_active() const;
        void end_batch() noexcept;
        void release_gl() noexcept;
        void recycle(std::unique_ptr<Storage>) noexcept;
        void defer_retirement(std::unique_ptr<Storage>) noexcept;
        void drain_retirements() noexcept;
        [[nodiscard]] GLbitfield image_hazard(const Storage&) const;
        [[nodiscard]] GLbitfield write_hazard(const Storage&, GLbitfield consumer) const;
        void synchronize(GLbitfield bits);
        [[nodiscard]] Result<void> initialize_storage(std::span<const std::string_view> extensions);
        [[nodiscard]] Result<std::unique_ptr<Storage>> acquire(Type, Layout);
        [[nodiscard]] Result<void> preflight_outputs(std::span<const Slot>);
        [[nodiscard]] Result<void> scrub(Storage&);
        [[nodiscard]] Result<void> upload(Storage&, FieldDesc, Layout, std::span<const std::byte>);
        [[nodiscard]] Result<void> readback(Storage&, FieldDesc, Layout, std::span<std::byte>);
        [[nodiscard]] Result<void> copy(Storage&, Storage&);
    };
}
