#pragma once

#include "Types.hpp"

#include <functional>
#include <memory>
#include <span>
#include <utility>

namespace hyprimate::compute {
    struct GlResolver {
        using Proc = void (*)();
        Proc (*resolve)(const char*, void*) noexcept = nullptr;
        bool (*is_current)(void*) noexcept = nullptr;
        void* owner = nullptr;
    };
    struct StageLimits {
        unsigned inputs{}, outputs{}, parameter_components{}, combined_output_resources{};
        bool available{};
        std::string reason;
    };
    struct Capabilities {
        unsigned major{}, minor{}, texture_size{}, viewport_width{}, viewport_height{};
        unsigned width{}, height{}, combined_samplers{}, image_units{};
        std::array<unsigned, 3> group_count{}, local_size{};
        unsigned invocations{};
        StageLimits native, slibs;
        // R32I, RGBA32I, R32UI, RGBA32UI.
        std::array<bool, 4> formats{};
        std::uint64_t max_pixels = MAX_PIXELS;
        std::size_t max_field_bytes = MAX_FIELD_BYTES;
        std::string vendor, renderer, version;
    };
    struct Statistics {
        std::uint64_t compiles{}, allocations{}, pool_hits{}, native_dispatches{}, slibs_dispatches{}, samples{};
        std::uint64_t barriers{}, fetch_barriers{}, framebuffer_barriers{}, update_barriers{}, image_barriers{};
        std::uint64_t upload_bytes{}, copy_bytes{}, readback_bytes{}, uniform_updates{};
        std::size_t live_bytes{}, idle_bytes{};
    };
    struct SampledField {
        unsigned texture{}, width{}, height{}; // width is also the physical row stride in texels
        FieldDesc desc;
    };

    namespace detail { struct State; struct Program; struct Storage; struct Scope; }
    class Device;
    class Batch;

    class Kernel {
    public:
        Kernel() noexcept = default;
        Kernel(Kernel&&) noexcept = default;
        Kernel& operator=(Kernel&&) noexcept = default;
        [[nodiscard]] Backend backend() const noexcept;
        [[nodiscard]] std::string_view fallback_reason() const noexcept;
    private:
        friend Device;
        friend Batch;
        std::weak_ptr<detail::State> state_;
        std::shared_ptr<const detail::Program> program_;
    };
    class Field {
    public:
        Field() noexcept;
        ~Field();
        Field(Field&&) noexcept;
        Field& operator=(Field&&) noexcept;
        [[nodiscard]] FieldDesc descriptor() const noexcept { return desc_; }
        [[nodiscard]] Layout layout() const noexcept { return layout_; }
        // Relinquish ownership without GL; a still-live owner retires it on next entry.
        void abandon() noexcept;
    private:
        friend Device;
        friend Batch;
        void reset() noexcept;
        std::weak_ptr<detail::State> state_;
        std::unique_ptr<detail::Storage> storage_;
        FieldDesc desc_;
        Layout layout_{};
    };
    class FieldPair {
    public:
        FieldPair(Field first, Field second) noexcept : read_(std::move(first)), write_(std::move(second)) {}
        [[nodiscard]] const Field& read() const noexcept { return read_; }
        [[nodiscard]] Field& write() noexcept { return write_; }
        void swap() noexcept { std::swap(read_, write_); }
    private:
        Field read_, write_;
    };
    using KernelSet = std::span<const std::reference_wrapper<const Kernel>>;
    using FieldsIn = std::span<const Field* const>;
    using FieldsOut = std::span<Field* const>;

    class Batch {
    public:
        Batch() noexcept;
        ~Batch();
        Batch(Batch&&) noexcept;
        Batch& operator=(Batch&&) noexcept;
        [[nodiscard]] Result<void> dispatch(const Kernel&, FieldsIn, FieldsOut, std::span<const ParameterValue>);
    private:
        friend Device;
        void reset() noexcept;
        std::weak_ptr<detail::State> state_;
    };
    class Device {
    public:
        Device() noexcept = default;
        ~Device();
        Device(Device&&) noexcept;
        Device& operator=(Device&&) noexcept;
        [[nodiscard]] Result<void> initialize(GlResolver resolver, Backend preference = Backend::Auto);
        [[nodiscard]] const Capabilities& capabilities() const noexcept;
        [[nodiscard]] Statistics statistics() const noexcept;
        [[nodiscard]] Result<Kernel> prepare(const KernelDesc&);
        [[nodiscard]] Result<Field> allocate(FieldDesc);
        [[nodiscard]] Result<Batch> begin(KernelSet);
        [[nodiscard]] Result<void> upload(Field&, std::span<const std::byte>);
        // Explicit blocking diagnostic/offline transfer.
        [[nodiscard]] Result<void> readback(const Field&, std::span<std::byte>);
        [[nodiscard]] Result<void> copy(const Field&, Field&);
        // Immediate same-context draw only. Do not retain the name across writes, swaps or release.
        [[nodiscard]] Result<SampledField> sample(const Field&);
        [[nodiscard]] Result<void> release();
        void abandon() noexcept;
    private:
        std::shared_ptr<detail::State> state_;
    };
}
