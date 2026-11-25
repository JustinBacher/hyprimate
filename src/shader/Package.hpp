#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../compute/Types.hpp"

namespace hyprimate::shader {
    inline constexpr unsigned SPEC_VERSION = 1;
    inline constexpr unsigned MAX_PASSES = 32, MAX_ITERATIONS = 32, MAX_TEXTURES = 16,
                              MAX_BUFFERS = 16, MAX_MESHES = 16, MAX_PARAMETERS = 64;
    inline constexpr std::size_t MAX_INSTANCE_BYTES = 128ULL * 1024 * 1024;
    enum class Event { Open, Close, Move, Resize, Workspace, Wallpaper };
    enum class Phase { Initialize, Update, Render };
    enum class Lifetime { Persistent, Transient };
    enum class Format { R8, RG8, RGBA8, R16F, RG16F, RGBA16F, R32F, RG32F, RGBA32F };
    enum class ValueType { Float, Int, Bool, Vec2, Vec3, Vec4 };
    enum class Primitive { Points, Lines, Triangles, TriangleStrip };
    enum class Side { Single, Read, Write };
    enum class ResourceKind { Source, Output, Texture, Buffer, Field, Destination, Original, Processed, EnvironmentState, Disturbances };
    struct Ref {
        ResourceKind kind = ResourceKind::Source;
        std::uint16_t index = 0;
        Side side = Side::Single;
        bool operator==(const Ref &) const = default;
    };
    struct Value {
        ValueType type = ValueType::Float;
        std::array<float, 4> numbers{};
        int integer = 0;
        bool operator==(const Value &) const = default;
    };
    struct Parameter {
        std::string name;
        Value initial;
        double minimum = -3.402823466e+38F, maximum = 3.402823466e+38F;
        bool operator==(const Parameter &) const = default;
    };
    struct Size {
        unsigned width = 0, height = 0;
        float scale = 1;
        bool operator==(const Size &) const = default;
    };
    struct Texture {
        std::string name;
        Size size;
        Format format = Format::RGBA8;
        Lifetime lifetime = Lifetime::Persistent;
        bool ping_pong = false, linear = false, repeat = false;
        unsigned image_width = 0, image_height = 0;
        std::vector<std::uint8_t> image;
        bool operator==(const Texture &) const = default;
    };
    struct Field {
        std::string name;
        compute::Type type;
        std::optional<compute::Domain> domain;
        std::optional<float> scale;
        Lifetime lifetime = Lifetime::Persistent;
        bool ping_pong = false;
        bool operator==(const Field &) const = default;
    };
    struct Buffer {
        std::string name;
        std::size_t bytes = 0;
        Lifetime lifetime = Lifetime::Persistent;
        bool ping_pong = false;
        // Float32 data or raw file bytes; runtime zero-fills the remaining tail.
        std::vector<std::uint8_t> initial;
        bool operator==(const Buffer &) const = default;
    };
    struct Attribute {
        unsigned location = 0, components = 0, stride = 0, offset = 0, divisor = 0;
        Ref buffer;
        bool operator==(const Attribute &) const = default;
    };
    struct Mesh {
        std::string name;
        Primitive primitive = Primitive::Triangles;
        unsigned count = 3, instances = 1;
        std::vector<Attribute> attributes;
        std::vector<std::uint32_t> indices;
        bool operator==(const Mesh &) const = default;
    };
    struct Binding {
        std::string name;
        Ref resource;
        bool operator==(const Binding &) const = default;
    };
    struct Pass {
        std::string name;
        Phase phase = Phase::Render;
        std::string vertex, fragment, kernel;
        bool compute = false;
        std::vector<Binding> inputs, blocks;
        std::vector<Ref> outputs;
        std::vector<std::string> varyings;
        Ref feedback;
        int mesh = -1;
        unsigned iterations = 1;
        bool transform_feedback = false, clear = true, blend = false;
        bool operator==(const Pass &) const = default;
    };
    inline constexpr unsigned MAX_INTERACTIONS = 24;
    struct Environment {
        bool original = false, processed = false, interactions = false, publish = false;
        bool sample_state = false;
        unsigned settle_ms = 0;
        std::string channel;
        std::optional<Ref> state, emit;
        bool operator==(const Environment &) const = default;
        bool used() const noexcept {
            return original || processed || interactions || publish || sample_state || state || emit;
        }
    };
    struct Package {
        std::string path;
        std::vector<Event> events{Event::Open, Event::Close};
        // Fractions of the capture size, added left/top/right/bottom.
        std::array<float, 4> bounds{};
        bool blur_mask = false;
        Environment environment;
        std::vector<Parameter> parameters;
        std::vector<Texture> textures;
        std::vector<Field> fields;
        std::vector<Buffer> buffers;
        std::vector<Mesh> meshes;
        std::vector<Pass> passes;
        bool operator==(const Package &) const = default;
        bool supports(Event event) const noexcept;
    };
    using Values = std::vector<Value>;
    std::expected<Package, std::string> load_package(const std::string &path);
    // Overrides are strictly typed; missing entries retain manifest defaults.
    std::expected<Values, std::string> parameter_values(const Package &package,
        std::string_view overrides = "{}");
    std::expected<void, std::string> validate_values(const Package &package, const Values &values);
    // Also validates programmatically constructed packages used by developer tooling.
    std::expected<void, std::string> validate_package(const Package &package);
    // Bodies omit #version and injected uniforms. Fragment hypr_uv is injected;
    // custom vertices supply it. Includes zero-based hypr_iteration and hypr_iterations.
    std::string shader_source(const Package &package, std::string_view body, bool vertex,
        std::string_view field_helpers = {});
    // The package and pass must already have passed validate_package.
    compute::KernelDesc kernel_descriptor(const Package &package, const Pass &pass);
    std::string_view default_vertex() noexcept;
}  // namespace hyprimate::shader
