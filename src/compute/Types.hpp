#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hyprimate::compute {
    template<class T> using Result = std::expected<T, std::string>;

    enum class Backend { Auto, Native, Slibs };
    enum class Element { Float, Int, Uint };
    struct Type {
        Element scalar = Element::Float;
        unsigned lanes = 1;
        bool operator==(const Type&) const = default;
    };
    struct Domain {
        std::uint32_t x = 1, y = 1, z = 1;
        bool operator==(const Domain&) const = default;
    };
    struct FieldDesc {
        Type type;
        Domain domain;
        bool operator==(const FieldDesc&) const = default;
    };
    struct Slot {
        std::string name;
        Type type;
        bool operator==(const Slot&) const = default;
    };
    struct KernelDesc {
        std::string name, source;
        std::vector<Slot> inputs, outputs, parameters;
        bool operator==(const KernelDesc&) const = default;
    };
    struct ParameterValue {
        std::variant<std::array<float, 4>, std::array<std::int32_t, 4>, std::array<std::uint32_t, 4>> data;
        unsigned lanes = 1;
        [[nodiscard]] Type type() const noexcept { return {static_cast<Element>(data.index()), lanes}; }
    };

    inline constexpr unsigned MAX_EXTENT = 8192;
    inline constexpr std::uint64_t MAX_PIXELS = 16'777'216;
    inline constexpr std::size_t MAX_FIELD_BYTES = 128u * 1024u * 1024u;

    struct Layout {
        unsigned width, height, elements;
        std::size_t logical_bytes, storage_bytes;
        bool operator==(const Layout&) const = default;
    };

    [[nodiscard]] std::string_view type_name(Type type) noexcept;
    [[nodiscard]] Result<Type> parse_type(std::string_view name);
    [[nodiscard]] Result<Backend> parse_backend(std::string_view name);
    [[nodiscard]] Result<Layout> field_layout(FieldDesc desc, unsigned width = MAX_EXTENT,
                                              unsigned height = MAX_EXTENT, std::size_t budget = MAX_FIELD_BYTES);
    [[nodiscard]] Result<void> validate_kernel(const KernelDesc& desc);
    [[nodiscard]] Result<std::string> sample_source(std::string_view alias, Type type);
}
