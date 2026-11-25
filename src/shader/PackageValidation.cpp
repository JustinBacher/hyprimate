#include "../compute/KernelSource.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include "PackageValidationPrivate.hpp"

namespace hyprimate::shader::package_detail {
    [[noreturn]] void fail(const std::string &message) {
        throw std::runtime_error(message);
    }
    void require(bool condition, const std::string &message) {
        if (!condition) fail(message);
    }
    unsigned components(ValueType type) {
        switch (type) {
            case ValueType::Float:
            case ValueType::Int:
            case ValueType::Bool:
                return 1;
            case ValueType::Vec2:
                return 2;
            case ValueType::Vec3:
                return 3;
            case ValueType::Vec4:
                return 4;
        }
        fail("invalid parameter type");
    }
    void range(const Parameter &parameter, const Value &v) {
        require(v.type == parameter.initial.type && std::isfinite(parameter.minimum) &&
                    std::isfinite(parameter.maximum) && parameter.minimum <= parameter.maximum,
            "invalid parameter range/type: " + parameter.name);
        if (v.type == ValueType::Bool) {
            require(v.integer == 0 || v.integer == 1, "invalid boolean");
            return;
        }
        if (v.type == ValueType::Int) {
            require(static_cast<double>(v.integer) >= parameter.minimum &&
                        static_cast<double>(v.integer) <= parameter.maximum,
                "parameter outside range: " + parameter.name);
        } else
            for (unsigned i = 0; i < components(v.type); ++i)
                require(std::isfinite(v.numbers[i]) && v.numbers[i] >= parameter.minimum &&
                            v.numbers[i] <= parameter.maximum,
                    "parameter outside range: " + parameter.name);
    }
    unsigned pixel_bytes(Format f) {
        switch (f) {
            case Format::R8:
                return 1;
            case Format::RG8:
                return 2;
            case Format::RGBA8:
                return 4;
            case Format::R16F:
                return 2;
            case Format::RG16F:
                return 4;
            case Format::RGBA16F:
                return 8;
            case Format::R32F:
                return 4;
            case Format::RG32F:
                return 8;
            case Format::RGBA32F:
                return 16;
        }
        fail("invalid texture format");
    }
    namespace {
        bool identifier(std::string_view name) {
            if (name.empty() || name.starts_with("hypr_") || name.starts_with("gl_") ||
                name.find("__") != name.npos)
                return false;
            const auto alpha = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            };
            if (!alpha(name.front())) return false;
            for (char c : name)
                if (!alpha(c) && !(c >= '0' && c <= '9')) return false;
            // Includes ES 3.00 keywords and reserved future keywords, not merely C++ identifiers.
            static const std::unordered_set<std::string_view> reserved{"attribute",
                "const",
                "uniform",
                "varying",
                "layout",
                "centroid",
                "flat",
                "smooth",
                "break",
                "continue",
                "do",
                "for",
                "while",
                "switch",
                "case",
                "default",
                "if",
                "else",
                "subroutine",
                "in",
                "out",
                "inout",
                "float",
                "double",
                "int",
                "void",
                "bool",
                "true",
                "false",
                "invariant",
                "discard",
                "return",
                "mat2",
                "mat3",
                "mat4",
                "mat2x2",
                "mat2x3",
                "mat2x4",
                "mat3x2",
                "mat3x3",
                "mat3x4",
                "mat4x2",
                "mat4x3",
                "mat4x4",
                "vec2",
                "vec3",
                "vec4",
                "ivec2",
                "ivec3",
                "ivec4",
                "bvec2",
                "bvec3",
                "bvec4",
                "uint",
                "uvec2",
                "uvec3",
                "uvec4",
                "lowp",
                "mediump",
                "highp",
                "precision",
                "sampler2D",
                "sampler3D",
                "samplerCube",
                "sampler2DShadow",
                "samplerCubeShadow",
                "sampler2DArray",
                "sampler2DArrayShadow",
                "isampler2D",
                "isampler3D",
                "isamplerCube",
                "isampler2DArray",
                "usampler2D",
                "usampler3D",
                "usamplerCube",
                "usampler2DArray",
                "struct",
                "common",
                "partition",
                "active",
                "asm",
                "class",
                "union",
                "enum",
                "typedef",
                "template",
                "this",
                "resource",
                "goto",
                "inline",
                "noinline",
                "public",
                "static",
                "extern",
                "external",
                "interface",
                "long",
                "short",
                "half",
                "fixed",
                "unsigned",
                "superp",
                "input",
                "output",
                "hvec2",
                "hvec3",
                "hvec4",
                "dvec2",
                "dvec3",
                "dvec4",
                "fvec2",
                "fvec3",
                "fvec4",
                "sampler2DRect",
                "sampler3DRect",
                "sampler2DRectShadow",
                "sizeof",
                "cast",
                "namespace",
                "using",
                "row_major",
                "main",
                "texture",
                "textureLod",
                "textureSize",
                "texelFetch",
                "sin",
                "cos",
                "tan",
                "asin",
                "acos",
                "atan",
                "pow",
                "exp",
                "log",
                "sqrt",
                "abs",
                "sign",
                "floor",
                "ceil",
                "fract",
                "mod",
                "min",
                "max",
                "clamp",
                "mix",
                "step",
                "smoothstep",
                "length",
                "distance",
                "dot",
                "cross",
                "normalize",
                "reflect",
                "refract",
                "matrixCompMult",
                "lessThan",
                "greaterThan",
                "equal",
                "notEqual",
                "any",
                "all",
                "not",
                "dFdx",
                "dFdy",
                "fwidth"};
            static const std::unordered_set<std::string_view> builtins{"radians",
                "degrees",
                "sinh",
                "cosh",
                "tanh",
                "asinh",
                "acosh",
                "atanh",
                "exp2",
                "log2",
                "inversesqrt",
                "trunc",
                "round",
                "roundEven",
                "modf",
                "isnan",
                "isinf",
                "floatBitsToInt",
                "floatBitsToUint",
                "intBitsToFloat",
                "uintBitsToFloat",
                "packSnorm2x16",
                "unpackSnorm2x16",
                "packUnorm2x16",
                "unpackUnorm2x16",
                "packHalf2x16",
                "unpackHalf2x16",
                "faceforward",
                "outerProduct",
                "transpose",
                "determinant",
                "inverse",
                "lessThanEqual",
                "greaterThanEqual",
                "textureQueryLevels",
                "textureProj",
                "textureOffset",
                "texelFetchOffset",
                "textureProjOffset",
                "textureLodOffset",
                "textureProjLod",
                "textureProjLodOffset",
                "textureGrad",
                "textureGradOffset",
                "textureProjGrad",
                "textureProjGradOffset"};
            return !reserved.contains(name) && !builtins.contains(name);
        }
        void named(std::string_view name) {
            require(identifier(name), "invalid or reserved identifier: " + std::string(name));
        }
        void label(std::string_view name) {
            require(!name.empty() && name.find('\0') == name.npos, "empty or invalid name");
        }
        void resource_name(std::string_view name) {
            label(name);
            require(name != "source" && name != "output" && !name.ends_with(".read") &&
                        !name.ends_with(".write"),
                "reserved resource name or side suffix");
        }
        std::string uncomment(std::string_view body) {
            std::string result;
            result.reserve(body.size());
            bool line = false, block = false;
            for (std::size_t i = 0; i < body.size(); ++i) {
                // GLSL splices continued lines before processing comments/directives.
                if (body[i] == '\\' && i + 1 < body.size() && body[i + 1] == '\n') {
                    ++i;
                    continue;
                }
                if (body[i] == '\\' && i + 2 < body.size() && body[i + 1] == '\r' &&
                    body[i + 2] == '\n') {
                    i += 2;
                    continue;
                }
                if (line) {
                    if (body[i] == '\n') {
                        line = false;
                        result += '\n';
                    }
                    continue;
                }
                if (block) {
                    if (body[i] == '*' && i + 1 < body.size() && body[i + 1] == '/') {
                        block = false;
                        ++i;
                        result += ' ';
                    } else if (body[i] == '\n')
                        result += '\n';
                    continue;
                }
                if (body[i] == '/' && i + 1 < body.size() && body[i + 1] == '/') {
                    line = true;
                    ++i;
                    continue;
                }
                if (body[i] == '/' && i + 1 < body.size() && body[i + 1] == '*') {
                    block = true;
                    ++i;
                    result += ' ';
                    continue;
                }
                result += body[i];
            }
            return result;
        }
        void validate_stage(const Package &package, std::string_view source) {
            require(source.find('\0') == source.npos, "NUL in shader source");
            const auto clean = uncomment(source);
            static const std::regex version(R"((^|\n)\s*#\s*version\b)");
            require(!std::regex_search(clean, version), "shader bodies must omit #version");
            static const std::regex extension(R"((^|\n)\s*#\s*extension\b)");
            require(!std::regex_search(clean, extension),
                "shader extensions are outside the native GLES 3.0 interface");
            // Uniform declarations may contain precision qualifiers and comma-separated names.
            static const std::regex uniforms(R"(\buniform\b[^;{]*;)");
            for (auto it = std::sregex_iterator(clean.begin(), clean.end(), uniforms);
                it != std::sregex_iterator();
                ++it) {
                const auto declaration = it->str();
                for (const auto &p : package.parameters) {
                    const std::regex name("\\b" + p.name + "\\b");
                    require(!std::regex_search(declaration, name),
                        "parameter uniform redeclared: " + p.name);
                }
            }
        }
    }  // namespace
    void validate(const Package &p) {
        require(!p.events.empty() && p.events.size() <= 6, "invalid events");
        std::set<Event> events;
        for (auto e : p.events)
            require(e >= Event::Open && e <= Event::Wallpaper && events.insert(e).second,
                "invalid or duplicate event");
        const auto &env = p.environment;
        const bool wallpaper = events.contains(Event::Wallpaper);
        if (env.used() || !env.channel.empty() || env.settle_ms) {
            require(!events.contains(Event::Workspace) && (!wallpaper || events.size() == 1),
                "environment resources require a window or a wallpaper-only package");
            require((!env.interactions && !env.publish && !env.state && env.settle_ms == 0) ||
                        wallpaper,
                "environment publishing and interactions require a wallpaper package");
            require(!wallpaper || (!env.sample_state && !env.processed && !env.emit),
                "wallpaper cannot sample its own output or emit to itself");
            require(env.settle_ms <= 60000 && (!env.interactions || env.settle_ms > 0),
                "reactive wallpaper requires a bounded positive settle_ms");
            require(!env.settle_ms || env.interactions, "settle_ms requires interactions");
            require(!(env.state || env.emit || env.sample_state) || !env.channel.empty(),
                "environment state and emissions require a channel");
            if (!env.channel.empty()) {
                named(env.channel);
                require(env.channel.size() <= 64 &&
                            (env.interactions || env.state || env.emit || env.sample_state),
                    "invalid or unused environment channel");
            }
        }
        for (float bound : p.bounds)
            require(std::isfinite(bound) && bound >= 0 && bound <= 2, "bounds must be in [0,2]");
        require(p.parameters.size() <= MAX_PARAMETERS && p.textures.size() <= MAX_TEXTURES &&
                    p.fields.size() + p.textures.size() <= MAX_TEXTURES &&
                    p.buffers.size() <= MAX_BUFFERS && p.meshes.size() <= MAX_MESHES &&
                    !p.passes.empty() && p.passes.size() <= MAX_PASSES,
            "package count limit exceeded");
        std::set<std::string> names, parameters, mesh_names, pass_names;
        for (const auto &parameter : p.parameters) {
            named(parameter.name);
            require(parameters.insert(parameter.name).second, "duplicate parameter");
            range(parameter, parameter.initial);
        }
        std::size_t bytes = 0, decoded = 0, source_bytes = 0;
        for (const auto &field : p.fields) {
            resource_name(field.name);
            named(field.name);
            require(names.insert(field.name).second, "duplicate resource name");
            require(!compute::type_name(field.type).empty(), "invalid field type");
            require(field.lifetime == Lifetime::Persistent || field.lifetime == Lifetime::Transient,
                "invalid field lifetime");
            require(field.domain.has_value() != field.scale.has_value(),
                "field requires exactly one of domain or scale");
            if (field.domain) {
                const auto layout = compute::field_layout({field.type, *field.domain});
                require(bool(layout), "invalid field domain");
                bytes += layout->storage_bytes * (field.ping_pong ? 2 : 1);
            } else
                require(std::isfinite(*field.scale) && *field.scale > 0 && *field.scale <= 1,
                    "invalid field scale");
        }
        for (const auto &t : p.textures) {
            resource_name(t.name);
            require(names.insert(t.name).second, "duplicate resource name");
            require(t.lifetime == Lifetime::Persistent || t.lifetime == Lifetime::Transient,
                "invalid texture lifetime");
            require(std::isfinite(t.size.scale) && t.size.scale > 0 && t.size.scale <= 1 &&
                        (t.size.width == 0) == (t.size.height == 0) && t.size.width <= 4096 &&
                        t.size.height <= 4096,
                "invalid texture size");
            require(!t.size.width || t.size.scale == 1, "explicit size and scale are exclusive");
            std::size_t size =
                static_cast<std::size_t>(t.size.width) * t.size.height * pixel_bytes(t.format);
            if (!t.image.empty()) {
                require(t.image_width && t.image_height && t.image_width <= 4096 &&
                            t.image_height <= 4096 &&
                            t.image.size() ==
                                static_cast<std::size_t>(t.image_width) * t.image_height * 4,
                    "invalid decoded PNG");
                require(!t.ping_pong && t.lifetime == Lifetime::Persistent &&
                            t.format == Format::RGBA8 && !t.size.width && t.size.scale == 1,
                    "invalid PNG storage properties");
                size = t.image.size();
                decoded += size;
            } else
                require(!t.image_width && !t.image_height, "missing PNG pixels");
            bytes += size * (t.ping_pong ? 2 : 1);
        }
        require(decoded <= 64 * MiB, "decoded PNG assets exceed 64 MiB");
        for (const auto &b : p.buffers) {
            resource_name(b.name);
            require(names.insert(b.name).second, "duplicate resource name");
            require(b.lifetime == Lifetime::Persistent || b.lifetime == Lifetime::Transient,
                "invalid buffer lifetime");
            require(b.bytes && b.bytes <= 32 * MiB && b.initial.size() <= b.bytes,
                "invalid buffer size/data");
            bytes += b.bytes * (b.ping_pong ? 2 : 1);
        }
        require(bytes <= MAX_INSTANCE_BYTES, "instance storage exceeds 128 MiB");
        const auto validate_export = [&](Ref ref, bool impulse) {
            require(ref.kind == ResourceKind::Texture && ref.index < p.textures.size(),
                "environment export requires a declared texture");
            const auto &t = p.textures[ref.index];
            require(t.image.empty() && t.lifetime == Lifetime::Persistent &&
                        ref.side == (t.ping_pong ? Side::Read : Side::Single),
                "environment export requires a readable persistent texture");
            require(
                !impulse || (t.size.width == 1 && t.size.height == 1 && t.format == Format::RGBA8),
                "environment emission requires a 1x1 RGBA8 texture");
            require(std::ranges::any_of(p.passes,
                        [&](const Pass &pass) {
                            return pass.phase == Phase::Update &&
                                   std::ranges::any_of(pass.outputs, [&](Ref out) {
                                       return out.kind == ref.kind && out.index == ref.index;
                                   });
                        }),
                "environment export requires an update producer");
        };
        if (env.state) validate_export(*env.state, false);
        if (env.emit) validate_export(*env.emit, true);
        auto info = [&](Ref ref) -> std::pair<bool, Lifetime> {
            bool ping = false;
            Lifetime life = Lifetime::Persistent;
            switch (ref.kind) {
                case ResourceKind::Texture:
                    require(ref.index < p.textures.size(), "invalid texture ref");
                    ping = p.textures[ref.index].ping_pong;
                    life = p.textures[ref.index].lifetime;
                    break;
                case ResourceKind::Field:
                    require(ref.index < p.fields.size(), "invalid field ref");
                    ping = p.fields[ref.index].ping_pong;
                    life = p.fields[ref.index].lifetime;
                    break;
                case ResourceKind::Buffer:
                    require(ref.index < p.buffers.size(), "invalid buffer ref");
                    ping = p.buffers[ref.index].ping_pong;
                    life = p.buffers[ref.index].lifetime;
                    break;
                case ResourceKind::Source:
                case ResourceKind::Output:
                    require(ref.index == 0, "invalid host ref");
                    break;
                default:
                    fail("invalid resource kind");
            }
            require(ping ? (ref.side == Side::Read || ref.side == Side::Write)
                         : ref.side == Side::Single,
                "resource side does not match ping_pong declaration");
            return {ping, life};
        };
        for (const auto &mesh : p.meshes) {
            label(mesh.name);
            require(mesh_names.insert(mesh.name).second, "duplicate mesh");
            require(
                mesh.primitive >= Primitive::Points && mesh.primitive <= Primitive::TriangleStrip,
                "invalid mesh primitive");
            require(mesh.count && mesh.instances && mesh.count <= MaxInvocations &&
                        mesh.indices.size() <= MaxInvocations,
                "invalid mesh count");
            const auto count = mesh.indices.empty() ? mesh.count : mesh.indices.size();
            require(count * static_cast<std::uint64_t>(mesh.instances) <= MaxInvocations,
                "vertex invocation budget exceeded");
            unsigned maximum = mesh.count - 1;
            if (!mesh.indices.empty()) {
                maximum = *std::max_element(mesh.indices.begin(), mesh.indices.end());
                require(maximum < mesh.count, "index exceeds vertex count");
            }
            require(mesh.attributes.size() <= 16, "too many attributes");
            std::set<unsigned> locations;
            for (const auto &a : mesh.attributes) {
                info(a.buffer);
                require(a.buffer.kind == ResourceKind::Buffer && a.buffer.side != Side::Write,
                    "attribute requires readable buffer");
                require(a.location < 16 && locations.insert(a.location).second &&
                            a.components >= 1 && a.components <= 4 && a.stride &&
                            a.stride <= static_cast<unsigned>(std::numeric_limits<int>::max()) &&
                            a.stride % 4 == 0 && a.offset % 4 == 0,
                    "invalid attribute layout");
                const auto last = a.divisor ? (mesh.instances - 1) / a.divisor : maximum;
                const auto end = static_cast<std::uint64_t>(a.offset) +
                                 static_cast<std::uint64_t>(last) * a.stride + a.components * 4;
                require(end <= p.buffers[a.buffer.index].bytes, "attribute fetch exceeds buffer");
            }
        }
        Phase phase = Phase::Initialize;
        bool presentation = false;
        std::set<std::pair<ResourceKind, unsigned>> produced;
        for (const auto &pass : p.passes) {
            label(pass.name);
            require(pass_names.insert(pass.name).second, "duplicate pass name");
            require(pass.phase >= Phase::Initialize && pass.phase <= Phase::Render &&
                        pass.phase >= phase,
                "phases must be ordered initialize, update, render");
            if (pass.phase != phase) {
                produced.clear();
                phase = pass.phase;
            }
            const bool compute_pass = pass.compute;
            require(pass.iterations >= 1 && pass.iterations <= MAX_ITERATIONS,
                "invalid pass iterations");
            require(pass.mesh >= -1 &&
                        (pass.mesh < 0 || static_cast<std::size_t>(pass.mesh) < p.meshes.size()),
                "invalid pass mesh");
            require(pass.inputs.size() <= 32 && pass.blocks.size() <= 32, "too many pass bindings");
            source_bytes += pass.kernel.size();
            std::string field_helpers;
            if (!compute_pass) {
                for (const auto &binding : pass.inputs) {
                    if (binding.resource.kind != ResourceKind::Field) continue;
                    require(binding.resource.index < p.fields.size(), "invalid render field ref");
                    auto helper = compute::sample_source(binding.name,
                        p.fields[binding.resource.index].type);
                    require(bool(helper), helper ? "" : helper.error());
                    field_helpers += *helper;
                }
            }
            if (compute_pass) {
                require(pass.phase != Phase::Render && !pass.kernel.empty() && pass.vertex.empty() &&
                            pass.fragment.empty() && pass.blocks.empty() && pass.mesh < 0 &&
                            !pass.transform_feedback && !pass.blend && !pass.clear &&
                            pass.varyings.empty(),
                    "compute passes require a kernel, initialize/update phase, and no raster state");
                require(std::ranges::all_of(pass.inputs, [&](const Binding &binding) {
                            return binding.resource.kind == ResourceKind::Field &&
                                   binding.resource.index < p.fields.size();
                        }) &&
                            std::ranges::all_of(pass.outputs, [&](Ref ref) {
                                return ref.kind == ResourceKind::Field && ref.index < p.fields.size();
                            }),
                    "compute passes may access only declared fields");
                const auto descriptor = kernel_descriptor(p, pass);
                auto valid_kernel = compute::validate_kernel(descriptor);
                require(bool(valid_kernel), valid_kernel ? "" : valid_kernel.error());
            } else {
                require(pass.kernel.empty(), "kernel source requires a compute pass");
                require(!pass.vertex.empty(), "missing vertex shader");
                source_bytes += shader_source(p, pass.vertex, true, field_helpers).size();
                source_bytes += shader_source(p, pass.fragment, false, field_helpers).size();
                validate_stage(p, pass.vertex);
                validate_stage(p, pass.fragment);
            }
            require(source_bytes <= MaxSource, "assembled shader source exceeds 4 MiB");
            std::vector<Ref> reads, writes = pass.outputs;
            std::set<std::string> bindings;
            for (const auto &binding : pass.inputs) {
                named(binding.name);
                require(!parameters.contains(binding.name) && bindings.insert(binding.name).second &&
                            binding.name != "hypr_delta" && binding.name != "hypr_elapsed" &&
                            binding.name != "hypr_progress" && binding.name != "hypr_iteration" &&
                            binding.name != "hypr_iterations" && binding.name != "hypr_seed",
                    "duplicate/colliding sampler binding");
                require(binding.resource.kind == ResourceKind::Source ||
                            binding.resource.kind == ResourceKind::Texture ||
                            binding.resource.kind == ResourceKind::Field,
                    "sampler requires a texture or field");
                require(compute_pass ? binding.resource.kind == ResourceKind::Field
                                     : binding.resource.kind != ResourceKind::Field ||
                                           pass.phase == Phase::Render,
                    "fields are readable only by kernels and render passes");
                if (binding.resource.kind == ResourceKind::Field && !compute_pass) {
                    require(binding.resource.index < p.fields.size(), "invalid render field ref");
                    auto helper = compute::sample_source(binding.name,
                        p.fields[binding.resource.index].type);
                    require(bool(helper), helper ? "" : helper.error());
                    for (const auto &parameter : p.parameters)
                        require(parameter.name != "hc_tex_" + binding.name &&
                                    parameter.name != "hc_size_" + binding.name &&
                                    parameter.name != "hc_read_" + binding.name,
                            "field helper collides with parameter: " + binding.name);
                }
                reads.push_back(binding.resource);
            }
            for (const auto &binding : pass.blocks) {
                named(binding.name);
                require(!parameters.contains(binding.name) && bindings.insert(binding.name).second,
                    "duplicate/colliding block binding");
                require(binding.resource.kind == ResourceKind::Buffer, "block requires buffer");
                reads.push_back(binding.resource);
            }
            if (pass.mesh >= 0)
                for (const auto &a : p.meshes[pass.mesh].attributes) reads.push_back(a.buffer);
            if (pass.transform_feedback) {
                require(pass.phase != Phase::Render && pass.outputs.empty() &&
                            pass.fragment.empty() && !pass.varyings.empty() &&
                            pass.varyings.size() <= 64 && !pass.blend,
                    "invalid transform feedback pass");
                require(pass.mesh >= 0, "transform feedback requires a named mesh");
                if (pass.mesh >= 0) {
                    const auto &mesh = p.meshes[pass.mesh];
                    require(mesh.indices.empty() && mesh.primitive != Primitive::TriangleStrip,
                        "transform feedback requires a non-indexed points, lines, or triangles "
                        "mesh");
                }
                require(pass.feedback.kind == ResourceKind::Buffer, "feedback requires buffer");
                writes.push_back(pass.feedback);
                std::set<std::string> varyings;
                for (const auto &v : pass.varyings) {
                    named(v);
                    require(varyings.insert(v).second, "duplicate feedback varying");
                }
            } else if (!compute_pass) {
                require(!pass.fragment.empty() && !writes.empty() && pass.varyings.empty(),
                    "raster pass requires fragment and outputs");
            }
            if (compute_pass) {
                const auto &first = p.fields[pass.outputs.front().index];
                for (const auto output : pass.outputs) {
                    const auto &field = p.fields[output.index];
                    require(field.domain == first.domain && field.scale == first.scale,
                        "compute outputs must have equal logical domains");
                }
            }
            require(writes.size() <= MAX_TEXTURES, "too many outputs");
            for (auto read : reads) {
                const auto [ping, life] = info(read);
                (void)ping;
                require(read.kind != ResourceKind::Output && read.side != Side::Write,
                    "invalid read reference");
                if (life == Lifetime::Transient)
                    require(produced.contains({read.kind, read.index}),
                        "transient resource read before producer in same phase");
            }
            std::set<std::pair<ResourceKind, unsigned>> outputs;
            for (auto write : writes) {
                const auto [ping, life] = info(write);
                (void)ping;
                require(write.kind != ResourceKind::Source && write.side != Side::Read,
                    "invalid write reference");
                require(outputs.insert({write.kind, write.index}).second, "duplicate pass output");
                if (write.kind == ResourceKind::Output) {
                    require(pass.phase == Phase::Render && writes.size() == 1,
                        "output must be sole render attachment");
                    presentation = true;
                } else
                    require(pass.phase != Phase::Render || life == Lifetime::Transient,
                        "render cannot write persistent resources");
                if (compute_pass) {
                    require(write.kind == ResourceKind::Field,
                        "compute outputs must be fields");
                } else if (!pass.transform_feedback) {
                    require(write.kind == ResourceKind::Output || write.kind == ResourceKind::Texture,
                        "raster outputs require textures");
                    if (write.kind == ResourceKind::Texture) {
                        const auto &t = p.textures[write.index];
                        require(t.image.empty(), "PNG asset is read-only");
                        if (life == Lifetime::Transient) {
                            require(pass.clear || (!t.ping_pong &&
                                                      produced.contains({write.kind, write.index})),
                                "transient attachment must be cleared before preserving its "
                                "contents");
                        }
                    }
                }
                for (auto read : reads)
                    if (read.kind == write.kind && read.index == write.index)
                        require(read.side == Side::Read && write.side == Side::Write,
                            "resource feedback requires opposite ping sides");
            }
            // A pass's own writes do not satisfy its first iteration's dependencies.
            produced.insert(outputs.begin(), outputs.end());
        }
        require(presentation, "package requires a render pass writing output");
    }
}  // namespace hyprimate::shader::package_detail
namespace hyprimate::shader {
    using namespace package_detail;
    std::expected<void, std::string> validate_package(const Package &package) {
        try {
            validate(package);
            return {};
        } catch (const std::exception &error) {
            return std::unexpected(error.what());
        }
    }
    std::expected<void, std::string> validate_values(const Package &package, const Values &values) {
        try {
            require(values.size() == package.parameters.size(), "parameter value count mismatch");
            for (std::size_t i = 0; i < values.size(); ++i) range(package.parameters[i], values[i]);
            return {};
        } catch (const std::exception &error) {
            return std::unexpected(error.what());
        }
    }
}  // namespace hyprimate::shader
