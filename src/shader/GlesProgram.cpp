#include <algorithm>
#include <charconv>
#include <cmath>
#include <ranges>
#include <string_view>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GlesRuntimePrivate.hpp"
#include "gl/ShaderCompile.hpp"

namespace hyprimate::shader {
    using namespace gles_detail;

    namespace {
        struct Builtin {
            std::string_view name;
            GLenum type;
        };
        constexpr std::array<Builtin, UniformCount> BUILTINS{{{"hypr_progress", GL_FLOAT},
            {"hypr_elapsed", GL_FLOAT},
            {"hypr_delta", GL_FLOAT},
            {"hypr_duration", GL_FLOAT},
            {"hypr_scale", GL_FLOAT},
            {"hypr_size", GL_FLOAT_VEC2},
            {"hypr_target_size", GL_FLOAT_VEC2},
            {"hypr_pointer", GL_FLOAT_VEC2},
            {"hypr_seed", GL_FLOAT_VEC4},
            {"hypr_source_uv", GL_FLOAT_VEC4},
            {"hypr_domain", GL_FLOAT_VEC4},
            {"hypr_event", GL_INT},
            {"hypr_pointer_valid", GL_BOOL},
            {"hypr_fullscreen", GL_BOOL},
            {"hypr_iteration", GL_INT},
            {"hypr_iterations", GL_INT},
            {"hypr_destination_uv", GL_FLOAT_VEC4},
            {"hypr_geometry", GL_FLOAT_VEC4},
            {"hypr_previous_geometry", GL_FLOAT_VEC4},
            {"hypr_velocity", GL_FLOAT_VEC4},
            {"hypr_direction", GL_FLOAT},
            {"hypr_held", GL_BOOL},
            {"hypr_settling", GL_BOOL},
            {"hypr_blur_mask", GL_BOOL},
            {"hypr_output_size", GL_FLOAT_VEC2},
            {"hypr_output_origin", GL_FLOAT_VEC2},
            {"hypr_output_rect", GL_FLOAT_VEC4},
            {"hypr_wallpaper_uv", GL_FLOAT_VEC4},
            {"hypr_wallpaper_original_available", GL_BOOL},
            {"hypr_wallpaper_processed_available", GL_BOOL},
            {"hypr_environment_state_available", GL_BOOL},
            {"hypr_disturbances_available", GL_BOOL},
            {"hypr_interaction_count", GL_INT}}};
        constexpr std::array<GLenum, 6> VALUE_TYPES{
            GL_FLOAT, GL_INT, GL_BOOL, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};
        unsigned float_columns(GLenum type) {
            switch (type) {
                case GL_FLOAT:
                case GL_FLOAT_VEC2:
                case GL_FLOAT_VEC3:
                case GL_FLOAT_VEC4:
                    return 1;
                case GL_FLOAT_MAT2:
                case GL_FLOAT_MAT2x3:
                case GL_FLOAT_MAT2x4:
                    return 2;
                case GL_FLOAT_MAT3:
                case GL_FLOAT_MAT3x2:
                case GL_FLOAT_MAT3x4:
                    return 3;
                case GL_FLOAT_MAT4:
                case GL_FLOAT_MAT4x2:
                case GL_FLOAT_MAT4x3:
                    return 4;
                default:
                    return 0;
            }
        }

        unsigned float_components(GLenum type) {
            switch (type) {
                case GL_FLOAT:
                    return 1;
                case GL_FLOAT_VEC2:
                    return 2;
                case GL_FLOAT_VEC3:
                    return 3;
                case GL_FLOAT_VEC4:
                case GL_FLOAT_MAT2:
                    return 4;
                case GL_FLOAT_MAT2x3:
                case GL_FLOAT_MAT3x2:
                    return 6;
                case GL_FLOAT_MAT2x4:
                case GL_FLOAT_MAT4x2:
                    return 8;
                case GL_FLOAT_MAT3:
                    return 9;
                case GL_FLOAT_MAT3x4:
                case GL_FLOAT_MAT4x3:
                    return 12;
                case GL_FLOAT_MAT4:
                    return 16;
                default:
                    return 0;
            }
        }

        void set_value(GLint location, const Value &value) {
            switch (value.type) {
                case ValueType::Float:
                    glUniform1f(location, value.numbers[0]);
                    break;
                case ValueType::Int:
                case ValueType::Bool:
                    glUniform1i(location, value.integer);
                    break;
                case ValueType::Vec2:
                    glUniform2fv(location, 1, value.numbers.data());
                    break;
                case ValueType::Vec3:
                    glUniform3fv(location, 1, value.numbers.data());
                    break;
                case ValueType::Vec4:
                    glUniform4fv(location, 1, value.numbers.data());
                    break;
            }
        }

        std::string field_helpers(const Package &package, const Pass &pass) {
            std::string helpers;
            for (const auto &input : pass.inputs) {
                if (input.resource.kind != ResourceKind::Field) {
                    continue;
                }
                const auto &desc = package.fields[input.resource.index];
                auto generated = compute::sample_source(input.name, desc.type);
                if (generated) {
                    helpers += *generated;
                }
            }
            return helpers;
        }

        GLenum field_sampler_type(compute::Type type) {
            switch (type.scalar) {
                case compute::Element::Float: return GL_UNSIGNED_INT_SAMPLER_2D;
                case compute::Element::Int: return GL_INT_SAMPLER_2D;
                case compute::Element::Uint: return GL_UNSIGNED_INT_SAMPLER_2D;
            }
            return 0;
        }

        std::optional<std::pair<Ref, compute::Type>> field_sampler(
            const Package &package, const Pass &pass, std::string_view name) {
            for (const auto &input : pass.inputs) {
                if (input.resource.kind != ResourceKind::Field) continue;
                if (name == "hc_tex_" + input.name) {
                    return std::pair{input.resource, package.fields[input.resource.index].type};
                }
            }
            return std::nullopt;
        }

        std::optional<std::pair<Ref, compute::Type>> field_size(
            const Package &package, const Pass &pass, std::string_view name) {
            for (const auto &input : pass.inputs) {
                if (input.resource.kind != ResourceKind::Field) continue;
                if (name == "hc_size_" + input.name) {
                    return std::pair{input.resource, package.fields[input.resource.index].type};
                }
            }
            return std::nullopt;
        }

        bool link(const Package &package, const Pass &pass, CompiledPass &gpu, std::string &error) {
            const auto helpers = field_helpers(package, pass);
            const auto vertex =
                gl::compile_shader(GL_VERTEX_SHADER, shader_source(package, pass.vertex, true, helpers), error);
            if (vertex == 0) {
                return false;
            }
            // Do not inject raster ABI inputs into the inert feedback fragment.
            const auto fragment = gl::compile_shader(GL_FRAGMENT_SHADER,
                pass.transform_feedback ? "#version 300 es\nvoid main() {}\n"
                                        : shader_source(package, pass.fragment, false, helpers),
                error);
            if (fragment == 0) {
                glDeleteShader(vertex);
                return false;
            }
            gpu.program = glCreateProgram();
            if (gpu.program == 0) {
                glDeleteShader(vertex);
                glDeleteShader(fragment);
                return fail(error, "cannot create shader program");
            }
            glAttachShader(gpu.program, vertex);
            glAttachShader(gpu.program, fragment);
            if (pass.transform_feedback) {
                std::vector<const GLchar *> names;
                names.reserve(pass.varyings.size());
                for (const auto &varying : pass.varyings) {
                    names.push_back(varying.c_str());
                }
                glTransformFeedbackVaryings(gpu.program,
                    static_cast<GLsizei>(names.size()),
                    names.data(),
                    GL_INTERLEAVED_ATTRIBS);
            }
            glLinkProgram(gpu.program);
            glDetachShader(gpu.program, vertex);
            glDetachShader(gpu.program, fragment);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint status = GL_FALSE;
            glGetProgramiv(gpu.program, GL_LINK_STATUS, &status);
            return status == GL_TRUE || fail(error, "link: " + gl::shader_log(gpu.program, true));
        }

        bool reflect_attributes(const Mesh *mesh, GLuint program, std::string &error) {
            GLint count = 0, max_name = 0;
            glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
            glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_name);
            std::string name(static_cast<std::size_t>(max_name), '\0');
            for (GLint i = 0; i < count; ++i) {
                GLsizei length = 0;
                GLint elements = 0;
                GLenum type = 0;
                glGetActiveAttrib(program,
                    static_cast<GLuint>(i),
                    max_name,
                    &length,
                    &elements,
                    &type,
                    name.data());
                const std::string_view attribute(name.data(), length);
                if (attribute.starts_with("gl_")) {
                    continue;
                }
                const auto columns = float_columns(type);
                const GLint location = glGetAttribLocation(program, name.data());
                if (!mesh || columns == 0 || location < 0 || elements != 1) {
                    return fail(
                        error, "unbound or non-float vertex attribute: " + std::string(attribute));
                }
                for (unsigned column = 0; column < columns; ++column) {
                    if (std::ranges::none_of(mesh->attributes, [&](const Attribute &input) {
                            return input.location == static_cast<unsigned>(location) + column;
                        })) {
                        return fail(error, "missing mesh location for: " + std::string(attribute));
                    }
                }
            }
            return true;
        }

        bool reflect_feedback(const Package &package,
            const Pass &pass,
            const Mesh *mesh,
            GLuint program,
            const Limits &limits,
            std::string &error) {
            if (!pass.transform_feedback) {
                return true;
            }
            GLint count = 0;
            glGetProgramiv(program, GL_TRANSFORM_FEEDBACK_VARYINGS, &count);
            if (count != static_cast<GLint>(pass.varyings.size())) {
                return fail(error, "transform feedback varying count does not match the manifest");
            }
            std::size_t components = 0;
            for (GLint i = 0; i < count; ++i) {
                GLint elements = 0;
                GLenum type = 0;
                GLchar ignored_name[1]{};
                glGetTransformFeedbackVarying(
                    program, static_cast<GLuint>(i), 1, nullptr, &elements, &type, ignored_name);
                const auto width = float_components(type);
                if (width == 0 || elements < 1) {
                    return fail(error, "transform feedback requires float varying data");
                }
                components += static_cast<std::size_t>(elements) * width;
            }
            if (components > static_cast<std::size_t>(limits.feedback_components)) {
                return fail(error, "transform feedback exceeds GL interleaved component limit");
            }
            const unsigned primitive_size = mesh->primitive == Primitive::Triangles ? 3U
                                            : mesh->primitive == Primitive::Lines   ? 2U
                                                                                    : 1U;
            if (mesh->count % primitive_size != 0) {
                return fail(error, "transform feedback mesh must contain complete primitives");
            }
            const auto bytes =
                std::uint64_t{mesh->count} * mesh->instances * components * sizeof(float);
            const auto &destination = package.buffers[pass.feedback.index];
            if (bytes > destination.bytes) {
                return fail(error, "transform feedback output buffer is too small");
            }
            if (destination.lifetime == Lifetime::Transient && bytes != destination.bytes) {
                return fail(
                    error, "transform feedback must write every byte of a transient buffer");
            }
            return true;
        }

        bool reflect_uniforms(const Package &package,
            const Pass &pass,
            const Values &values,
            const Limits &limits,
            CompiledPass &gpu,
            std::string &error) {
            GLint count = 0, max_name = 0;
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORMS, &count);
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name);
            std::string name(static_cast<std::size_t>(max_name), '\0');
            // GLES links per-stage sampler/UBO limits. The v3.0 uniform interface exposes
            // combined active samplers, not their originating shader stage.
            glUseProgram(gpu.program);
            const auto sampler_unit = [&](Ref ref) -> std::optional<unsigned> {
                unsigned unit = 0;
                while (unit < gpu.sampler_count && gpu.samplers[unit] != ref) ++unit;
                if (unit == gpu.sampler_count) {
                    if (unit >= gpu.samplers.size() ||
                        unit >= static_cast<unsigned>(limits.samplers)) return std::nullopt;
                    gpu.samplers[gpu.sampler_count++] = ref;
                }
                return unit;
            };
            for (GLuint i = 0; i < static_cast<GLuint>(count); ++i) {
                GLint block = -1;
                glGetActiveUniformsiv(gpu.program, 1, &i, GL_UNIFORM_BLOCK_INDEX, &block);
                if (block >= 0) {
                    continue;
                }
                GLsizei length = 0;
                GLint elements = 0;
                GLenum type = 0;
                glGetActiveUniform(
                    gpu.program, i, max_name, &length, &elements, &type, name.data());
                const std::string_view uniform(name.data(), length);
                const auto location = glGetUniformLocation(gpu.program, name.data());
                gpu.uses_environment |= package.environment.used();
                if (uniform == "hypr_interactions[0]" && package.environment.interactions &&
                    type == GL_FLOAT_VEC4 && elements > 0 &&
                    elements <= int(MAX_INTERACTIONS * 4)) {
                    gpu.interactions = location;
                    gpu.interaction_vectors = elements;
                    continue;
                }
                const auto builtin = std::ranges::find(BUILTINS, uniform, &Builtin::name);
                if (builtin != BUILTINS.end() && type == builtin->type && elements == 1) {
                    gpu.uniforms[static_cast<std::size_t>(builtin - BUILTINS.begin())] = location;
                    continue;
                }
                const auto parameter =
                    std::ranges::find(package.parameters, uniform, &Parameter::name);
                if (parameter != package.parameters.end() && elements == 1 &&
                    type == VALUE_TYPES[static_cast<unsigned>(parameter->initial.type)]) {
                    set_value(location,
                        values[static_cast<std::size_t>(parameter - package.parameters.begin())]);
                    continue;
                }
                if (const auto sampled = field_sampler(package, pass, uniform)) {
                    if (type != field_sampler_type(sampled->second) || elements != 1)
                        return fail(error, "typed field sampler mismatch: " + std::string(uniform));
                    const auto unit = sampler_unit(sampled->first);
                    if (!unit) return fail(error, "active sampler bindings exceed the GLES texture unit limit");
                    glUniform1i(location, static_cast<GLint>(*unit));
                    continue;
                }
                if (const auto sized = field_size(package, pass, uniform)) {
                    if (type != GL_INT_VEC3 || elements != 1)
                        return fail(error, "field domain uniform must be ivec3: " + std::string(uniform));
                    const auto unit = sampler_unit(sized->first);
                    if (!unit) return fail(error, "active sampler bindings exceed the GLES texture unit limit");
                    if (!gpu.field_sizes) {
                        gpu.field_sizes = std::make_unique<std::array<GLint, MAX_SHADER_SAMPLERS>>();
                        gpu.field_sizes->fill(-1);
                    }
                    (*gpu.field_sizes)[*unit] = location;
                    continue;
                }
                const auto input = std::ranges::find(pass.inputs, uniform, &Binding::name);
                const auto &environment = package.environment;
                std::optional<Ref> sampler;
                if (uniform == "hypr_source")
                    sampler = Ref{};
                else if (uniform == "hypr_destination")
                    sampler = Ref{ResourceKind::Destination};
                else if (uniform == "hypr_wallpaper_original" && environment.original)
                    sampler = Ref{ResourceKind::Original};
                else if (uniform == "hypr_wallpaper_processed" && environment.processed)
                    sampler = Ref{ResourceKind::Processed};
                else if (uniform == "hypr_environment_state" && environment.sample_state)
                    sampler = Ref{ResourceKind::EnvironmentState};
                else if (uniform == "hypr_disturbances" && environment.interactions &&
                         !environment.channel.empty())
                    sampler = Ref{ResourceKind::Disturbances};
                else if (input != pass.inputs.end())
                    sampler = input->resource;
                if (type != GL_SAMPLER_2D || elements != 1 || !sampler) {
                    return fail(error, "unsupported or unmapped uniform: " + std::string(uniform));
                }
                const auto ref = *sampler;
                const bool environment_ref = ref.kind == ResourceKind::Original ||
                                             ref.kind == ResourceKind::Processed ||
                                             ref.kind == ResourceKind::EnvironmentState ||
                                             ref.kind == ResourceKind::Disturbances;
                if (environment_ref && !package.supports(Event::Wallpaper) &&
                    pass.phase != Phase::Render) {
                    return fail(error,
                        "window packages can only sample the environment in their render phase");
                }
                unsigned unit = 0;
                while (unit < gpu.sampler_count && gpu.samplers[unit] != ref) {
                    ++unit;
                }
                if (unit == gpu.sampler_count) {
                    if (unit >= gpu.samplers.size() ||
                        unit >= static_cast<unsigned>(limits.samplers)) {
                        return fail(
                            error, "active sampler bindings exceed the GLES texture unit limit");
                    }
                    gpu.samplers[gpu.sampler_count++] = ref;
                }
                glUniform1i(location, static_cast<GLint>(unit));
            }
            return true;
        }

        bool reflect_blocks(const Package &package,
            const Pass &pass,
            const Limits &limits,
            CompiledPass &gpu,
            std::string &error) {
            GLint count = 0, max_name = 0;
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORM_BLOCKS, &count);
            if (count > limits.block_bindings || count > static_cast<GLint>(gpu.blocks.size())) {
                return fail(error, "active uniform blocks exceed the GLES binding limit");
            }
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH, &max_name);
            std::string name(static_cast<std::size_t>(max_name), '\0');
            for (GLuint i = 0; i < static_cast<GLuint>(count); ++i) {
                GLsizei length = 0;
                glGetActiveUniformBlockName(gpu.program, i, max_name, &length, name.data());
                const std::string_view block(name.data(), length);
                const auto binding = std::ranges::find(pass.blocks, block, &Binding::name);
                if (binding == pass.blocks.end()) {
                    return fail(error, "unmapped uniform block: " + std::string(block));
                }
                GLint bytes = 0;
                glGetActiveUniformBlockiv(gpu.program, i, GL_UNIFORM_BLOCK_DATA_SIZE, &bytes);
                if (bytes <= 0 || bytes > limits.block_bytes ||
                    static_cast<std::size_t>(bytes) >
                        package.buffers[binding->resource.index].bytes) {
                    return fail(
                        error, "uniform block exceeds its GLES/buffer size: " + std::string(block));
                }
                gpu.blocks[gpu.block_count++] = {binding->resource, bytes};
                glUniformBlockBinding(gpu.program, i, i);
            }
            return true;
        }

        // ES 3.0 can query a named output's location but cannot enumerate or
        // query its type. Accept a deliberately small output declaration grammar.
        bool token_word(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_';
        }

        bool identifier(std::string_view token) {
            return !token.empty() && token_word(token.front()) &&
                   (token.front() < '0' || token.front() > '9');
        }

        struct ShaderTokens {
            std::string_view source;
            bool valid = true;

            std::string_view next() {
                while (!source.empty()) {
                    if (source.front() == ' ' || source.front() == '\t' || source.front() == '\r' ||
                        source.front() == '\n' || source.front() == '\v' ||
                        source.front() == '\f') {
                        source.remove_prefix(1);
                    } else if (source.starts_with("//")) {
                        line();
                    } else if (source.starts_with("/*")) {
                        const auto end = source.find("*/", 2);
                        if (end == source.npos) {
                            valid = false;
                            return {};
                        }
                        source.remove_prefix(end + 2);
                    } else {
                        std::size_t length = 1;
                        if (token_word(source.front())) {
                            while (length < source.size() && token_word(source[length])) {
                                ++length;
                            }
                        }
                        const auto token = source.substr(0, length);
                        source.remove_prefix(length);
                        return token;
                    }
                }
                return {};
            }

            std::string_view line() {
                const auto end = source.find_first_of("\r\n");
                const auto value = source.substr(0, end);
                source.remove_prefix(end == source.npos ? source.size() : end + 1);
                return value;
            }
        };

        bool reflect_outputs(const Pass &pass, GLuint program, std::string &error) {
            if (pass.transform_feedback) {
                return true;
            }
            constexpr std::string_view BAD_OUTPUT =
                "fragment outputs require unconditional layout(location=N) out float/vecN name;";
            constexpr std::array<std::string_view, 4> TYPES{"float", "vec2", "vec3", "vec4"};
            constexpr std::array<std::string_view, 3> PRECISION{"lowp", "mediump", "highp"};
            const auto is_type = [&](std::string_view type) {
                return std::ranges::find(TYPES, type) != TYPES.end();
            };
            if (pass.fragment.find('\\') != std::string::npos) {
                return fail(error, "fragment source does not support line splicing");
            }
            ShaderTokens lex{pass.fragment};
            std::unordered_set<std::string_view> macros;
            std::array<std::string_view, MAX_TEXTURES> names{};
            std::array<std::string_view, 6> previous{};
            std::string_view paren_owner;
            unsigned braces = 0, parens = 0, conditional = 0;
            for (auto token = lex.next(); !token.empty(); token = lex.next()) {
                if (token == "#") {
                    ShaderTokens directive{lex.line()};
                    const auto kind = directive.next();
                    if (kind == "if" || kind == "ifdef" || kind == "ifndef") {
                        ++conditional;
                    } else if (kind == "endif" && conditional != 0) {
                        --conditional;
                    } else if (kind == "define" || kind == "undef") {
                        const auto macro = directive.next();
                        if (!identifier(macro) || macro == "out" || macro == "layout" ||
                            macro == "location" || is_type(macro) ||
                            std::ranges::find(PRECISION, macro) != PRECISION.end()) {
                            return fail(error, "fragment macros cannot redefine output grammar");
                        }
                        macros.insert(macro);
                        for (auto t = directive.next(); !t.empty(); t = directive.next()) {
                            if (t == "out" || t == "#") {
                                return fail(
                                    error, "fragment macros cannot introduce output declarations");
                            }
                        }
                        if (!directive.valid) {
                            return fail(error, "fragment macros must fit on one logical line");
                        }
                    } else if (kind != "elif" && kind != "else" && kind != "line" &&
                               kind != "pragma") {
                        return fail(error, "unsupported fragment preprocessor directive");
                    }
                    previous = {};
                    continue;
                }
                if (token == "out") {
                    if (parens != 0) {
                        // Ordinary function out parameters are not framebuffer declarations.
                        if (braces != 0 || parens != 1 || !identifier(paren_owner) ||
                            macros.contains(paren_owner)) {
                            return fail(error, BAD_OUTPUT);
                        }
                    } else {
                        if (braces != 0 || conditional != 0 || previous[0] != "layout" ||
                            previous[1] != "(" || previous[2] != "location" || previous[3] != "=" ||
                            previous[5] != ")" || previous[4].empty()) {
                            return fail(error, BAD_OUTPUT);
                        }
                        unsigned slot = 0;
                        const auto number = previous[4];
                        const auto result =
                            std::from_chars(number.data(), number.data() + number.size(), slot);
                        auto type = lex.next();
                        if (std::ranges::find(PRECISION, type) != PRECISION.end()) {
                            type = lex.next();
                        }
                        const auto name = lex.next();
                        if (result.ec != std::errc{} ||
                            result.ptr != number.data() + number.size() ||
                            slot >= pass.outputs.size() || !names[slot].empty() || !is_type(type) ||
                            !identifier(name) || lex.next() != ";") {
                            return fail(error, BAD_OUTPUT);
                        }
                        names[slot] = name;
                        token = ";";
                    }
                } else if (token == "(") {
                    if (parens++ == 0) {
                        paren_owner = previous.back();
                    }
                } else if (token == ")" && parens != 0) {
                    --parens;
                } else if (token == "{") {
                    ++braces;
                } else if (token == "}" && braces != 0) {
                    --braces;
                }
                std::shift_left(previous.begin(), previous.end(), 1);
                previous.back() = token;
            }
            for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
                const auto name = names[i];
                if (!lex.valid || name.empty() || macros.contains(name) ||
                    glGetFragDataLocation(program, std::string(name).c_str()) !=
                        static_cast<GLint>(i)) {
                    return fail(error,
                        "fragment output is missing, inactive, or macro-defined at location " +
                            std::to_string(i));
                }
            }
            return true;
        }

        // Probe actual framebuffer format combinations with one texel per unique
        // slot/format, before uploading immutable assets or sizing any instance.
        struct FormatProbe {
            static constexpr auto FORMATS = static_cast<unsigned>(Format::RGBA32F) + 1;
            const GLint prior_framebuffer = integer(GL_DRAW_FRAMEBUFFER_BINDING);
            const GLint prior_texture = integer(GL_TEXTURE_BINDING_2D);
            std::array<std::array<GLuint, FORMATS>, MAX_TEXTURES> textures{};
            std::array<GLuint, MAX_TEXTURES> attached{};
            GLuint framebuffer = 0;
            unsigned slots = 0;

            ~FormatProbe() {
                if (framebuffer != 0) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prior_framebuffer);
                    glBindTexture(GL_TEXTURE_2D, prior_texture);
                    glDeleteFramebuffers(1, &framebuffer);
                    for (const auto &row : textures) {
                        glDeleteTextures(FORMATS, row.data());
                    }
                }
            }

            bool check(
                const Package &package, const Pass &pass, Statistics &stats, std::string &error) {
                if (framebuffer == 0) {
                    glGenFramebuffers(1, &framebuffer);
                    if (framebuffer == 0) {
                        return fail(error, "cannot create GLES framebuffer format probe");
                    }
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
                }
                const auto outputs = static_cast<unsigned>(pass.outputs.size());
                std::array<GLenum, MAX_TEXTURES> draw{};
                for (unsigned i = 0; i < std::max(slots, outputs); ++i) {
                    const auto point = GL_COLOR_ATTACHMENT0 + i;
                    GLuint name = 0;
                    if (i < outputs) {
                        const auto format = package.textures[pass.outputs[i].index].format;
                        auto &saved = textures[i][static_cast<unsigned>(format)];
                        if (saved == 0) {
                            const auto pixel = pixel_format(format);
                            glGenTextures(1, &saved);
                            glBindTexture(GL_TEXTURE_2D, saved);
                            glTexStorage2D(GL_TEXTURE_2D, 1, pixel.internal, 1, 1);
                            if (!gl_ok(error, "allocating GLES format probe")) {
                                return false;
                            }
                            count_storage(stats, pixel.bytes);
                        }
                        name = saved;
                        draw[i] = point;
                    }
                    if (name != attached[i]) {
                        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, point, GL_TEXTURE_2D, name, 0);
                        attached[i] = name;
                    }
                }
                slots = outputs;
                glDrawBuffers(static_cast<GLsizei>(slots), draw.data());
                if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    return fail(
                        error, "unsupported GLES framebuffer format/MRT combination: " + pass.name);
                }
                return gl_ok(error, "checking GLES framebuffer format probe");
            }
        };
    }  // namespace
    bool Program::Impl::direct() const noexcept {
        return final_draws > 0 && final_is_last && final_source_over && !uses_frag_coord;
    }
    Program::Impl::Impl(Package spec, const Values &resolved_values, std::shared_ptr<void> owner)
        : package(std::move(spec)), values(resolved_values), compute_owner(std::move(owner)) {}
    Program::Impl::~Impl() {
        compute_device.reset();
        if (!armed) {
            return;
        }
        for (const auto &pass : passes) {
            if (pass.program != 0) glDeleteProgram(pass.program);
        }
        glDeleteTextures(static_cast<GLsizei>(assets.size()), assets.data());
        glDeleteBuffers(static_cast<GLsizei>(elements.size()), elements.data());
        if (source_sampler != 0) glDeleteSamplers(1, &source_sampler);
    }
    bool Program::Impl::prepare(const Values &values, GlResolver resolver,
        compute::GlResolver compute_resolver, compute::Backend backend, std::string &error) {
        limits.query();
        if (!color.configure(limits, resolver)) {
            return fail(error, "indexed GLES draw buffer state requires a GL procedure resolver");
        }
        if (color.indexed()) {
            // Do not mutate indexed host state if an advertised driver API
            // cannot actually query it.
            (void)color.mask(0);
            (void)color.get(GL_BLEND_EQUATION_RGB, 0);
            (void)color.enabled_slot(GL_BLEND, 0);
            if (!gl_ok(error, "querying indexed GLES draw buffer state")) {
                return false;
            }
        }
        const auto &spec = package;
        for (const auto &texture : spec.textures) {
            if (texture.linear && pixel_format(texture.format).type == GL_FLOAT &&
                !limits.float_linear) {
                return fail(
                    error, "32-bit float linear texture filtering is unsupported: " + texture.name);
            }
            if (texture.image_width > static_cast<unsigned>(limits.texture) ||
                texture.image_height > static_cast<unsigned>(limits.texture)) {
                return fail(error, "asset exceeds GLES texture size: " + texture.name);
            }
        }
        for (const auto &mesh : spec.meshes) {
            if (std::ranges::any_of(mesh.attributes, [&](const Attribute &attr) {
                    return attr.location >= static_cast<unsigned>(limits.attributes);
                })) {
                return fail(error, "mesh exceeds GLES vertex attribute limit: " + mesh.name);
            }
            if (!mesh.indices.empty() && *std::ranges::max_element(mesh.indices) > limits.index) {
                return fail(error, "mesh exceeds GLES element index limit: " + mesh.name);
            }
        }
        if (!spec.fields.empty()) {
#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
            if (compute::fixture::cpu_requested()) {
                auto workload = compute::fixture::match_workload(spec);
                if (!workload) return fail(error, workload.error());
                benchmark_workload = *workload;
            }
#endif
            compute_device = std::make_unique<compute::Device>();
            compute_only_simulation = std::ranges::all_of(spec.passes, [](const Pass& pass) {
                return pass.phase == Phase::Render || !pass.kernel.empty();
            });
            compute_kernels.reserve(std::ranges::count_if(spec.passes, [](const Pass& pass) { return !pass.kernel.empty(); }));
            if (auto initialized = compute_device->initialize(compute_resolver, backend); !initialized) {
                return fail(error, "compute device: " + initialized.error());
            }
        }
        for (std::size_t i = 0; i < spec.passes.size(); ++i) {
            const auto &pass = spec.passes[i];
            auto &gpu = passes[i];
            const auto *mesh = pass_mesh(spec, pass);
            if (!pass.kernel.empty()) {
                if (!compute_device) {
                    return fail(error, "compute pass without field resources: " + pass.name);
                }
                auto descriptor = kernel_descriptor(spec, pass);
                auto kernel = compute_device->prepare(descriptor);
                if (!kernel) return fail(error, pass.name + ": " + kernel.error());
                gpu.kernel = std::make_unique<CompiledKernel>(CompiledKernel{std::move(*kernel), compute_kernels.size()});
                compute_kernels.emplace_back(std::cref(gpu.kernel->kernel));
                continue;
            }
            if (pass.outputs.size() > static_cast<std::size_t>(limits.draw_buffers) ||
                pass.outputs.size() > static_cast<std::size_t>(limits.color_attachments)) {
                return fail(error, "MRT exceeds GLES draw buffer / attachment limit: " + pass.name);
            }
            if (pass.transform_feedback &&
                (!mesh || !mesh->indices.empty() || mesh->primitive == Primitive::TriangleStrip)) {
                return fail(error,
                    "GLES 3.0 feedback requires non-indexed points, lines or triangles: " +
                        pass.name);
            }
            if (pass.blend && !limits.float_blend &&
                std::ranges::any_of(pass.outputs, [&](const Ref &output) {
                    return output.kind == ResourceKind::Texture &&
                           pixel_format(spec.textures[output.index].format).type == GL_FLOAT;
                })) {
                return fail(error, "GLES 32-bit float target cannot blend: " + pass.name);
            }
            if (!link(spec, pass, gpu, error) || !reflect_outputs(pass, gpu.program, error) ||
                !reflect_attributes(mesh, gpu.program, error) ||
                !reflect_feedback(spec, pass, mesh, gpu.program, limits, error) ||
                !reflect_uniforms(spec, pass, values, limits, gpu, error) ||
                !reflect_blocks(spec, pass, limits, gpu, error) ||
                !gl_ok(error, "preparing pass")) {
                error = pass.name + ": " + error;
                return false;
            }
            sampler_count = std::max(sampler_count, gpu.sampler_count);
            block_count = std::max(block_count, gpu.block_count);
            max_outputs = std::max(max_outputs, static_cast<unsigned>(pass.outputs.size()));
            has_feedback |= pass.transform_feedback;
            uses_destination |=
                std::ranges::any_of(std::views::counted(gpu.samplers.begin(), gpu.sampler_count),
                    [](Ref ref) { return ref.kind == ResourceKind::Destination; });
            if (!pass.outputs.empty() && pass.outputs.front().kind == ResourceKind::Output) {
                final_is_last = i + 1 == spec.passes.size();
                const auto builtin = default_vertex();
                const bool fullscreen =
                    pass.mesh < 0 &&
                    (pass.vertex == builtin ||
                        (pass.vertex.size() == builtin.size() + 1 && pass.vertex.front() == '\n' &&
                            pass.vertex.ends_with(builtin)));
                // Source-over is associative. The first draw may replace
                // transparent storage only if it cannot overdraw itself;
                // subsequent draws must preserve the accumulated result.
                final_source_over &= final_draws == 0
                                         ? (pass.blend || (fullscreen && pass.iterations == 1)) &&
                                               (!pass.clear || pass.iterations == 1)
                                         : pass.blend && !pass.clear;
                final_draws += pass.iterations;
                // Keep v1 gl_FragCoord relative to its private target.
                uses_frag_coord |= pass.fragment.find("gl_FragCoord") != std::string::npos;
                samples_borrowed_target |= gpu.sampler_count != 0;
                blends_final |= pass.blend;
            } else if (final_draws != 0) {
                // Do not submit to the compositor until all private passes
                // that could fail have completed.
                final_source_over = false;
            }
            // Feedback executes with the already validated borrowed FBO bound.
            samples_borrowed_target |= pass.transform_feedback && gpu.sampler_count != 0;
        }
        if (!preflight_formats(error)) {
            return false;
        }
        return upload(error);
    }
    bool Program::Impl::preflight_formats(std::string &error) {
        if (!std::ranges::any_of(package.passes, [](const Pass &pass) {
                return !pass.outputs.empty() && pass.outputs.front().kind == ResourceKind::Texture;
            })) {
            return true;
        }
        FormatProbe probe;
        for (const auto &pass : package.passes) {
            if (!pass.outputs.empty() && pass.outputs.front().kind == ResourceKind::Texture &&
                !probe.check(package, pass, stats, error)) {
                return false;
            }
        }
        return true;
    }
    bool Program::Impl::upload(std::string &error) {
        const UploadState pixels;
        const auto array_buffer = integer(GL_ARRAY_BUFFER_BINDING);
        for (std::size_t i = 0; i < package.meshes.size(); ++i) {
            const auto &indices = package.meshes[i].indices;
            if (indices.empty()) {
                continue;
            }
            glGenBuffers(1, &elements[i]);
            // Upload through the generic array binding; attaching the EBO is VAO state.
            glBindBuffer(GL_ARRAY_BUFFER, elements[i]);
            const auto bytes = indices.size() * sizeof(indices.front());
            glBufferData(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), indices.data(), GL_STATIC_DRAW);
            count_storage(stats, bytes);
            stats.upload_bytes += bytes;
        }
        glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
        for (std::size_t i = 0; i < package.textures.size(); ++i) {
            const auto &desc = package.textures[i];
            if (desc.image.empty()) {
                continue;
            }
            const Extent size{desc.image_width, desc.image_height};
            texture_storage(assets[i], desc, size);
            glTexSubImage2D(GL_TEXTURE_2D,
                0,
                0,
                0,
                size.width,
                size.height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                desc.image.data());
            count_storage(stats, desc.image.size());
            stats.upload_bytes += desc.image.size();
        }
        const bool source_used = std::ranges::any_of(passes, [](const CompiledPass &pass) {
            return std::ranges::any_of(
                std::views::counted(pass.samplers.begin(), pass.sampler_count),
                [](Ref ref) { return linear_host_image(ref.kind); });
        });
        if (source_used) {
            glGenSamplers(1, &source_sampler);
            glSamplerParameteri(source_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glSamplerParameteri(source_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glSamplerParameteri(source_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glSamplerParameteri(source_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        return gl_ok(error, "uploading immutable shader resources");
    }
    Program::Program() = default;
    Program::~Program() = default;

    bool Program::initialize(Package package, const Values &values, std::string &error,
        GlResolver resolver, compute::GlResolver compute_resolver, compute::Backend backend,
        std::shared_ptr<void> compute_owner) {
        if (const auto valid = validate_package(package); !valid) {
            return fail(error, valid.error());
        }
        if (const auto valid = validate_values(package, values); !valid) {
            return fail(error, valid.error());
        }
        const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        if (!version || !std::string_view(version).starts_with("OpenGL ES ") ||
            integer(GL_MAJOR_VERSION) < 3) {
            return fail(error, "native effects require a current GLES 3.0 context");
        }
        if (!gl_ok(error, "entering program preparation")) {
            return false;
        }
        const auto previous = static_cast<GLuint>(integer(GL_CURRENT_PROGRAM));
        if (!can_use_program(previous, error)) {
            return false;
        }
        auto prepared =
            std::make_shared<Impl>(std::move(package), values, std::move(compute_owner));
        const bool ok = prepared->prepare(values, resolver, compute_resolver, backend, error);
        glUseProgram(previous);
        if (!ok) {
            discard_errors();
            return false;
        }
        if (!gl_ok(error, "restoring caller state after program preparation")) {
            return false;
        }
        m_impl = std::move(prepared);
        return true;
    }

    void Program::release() noexcept {
        m_impl.reset();
    }

    void Program::abandon() noexcept {
        if (m_impl) {
            m_impl->armed = false;
            if (m_impl->compute_device) m_impl->compute_device->abandon();
            m_impl.reset();
        }
    }

    const Package &Program::package() const {
        return m_impl->package;
    }

    bool Program::can_composite_direct() const noexcept {
        return m_impl && m_impl->direct();
    }

    const Statistics &Program::statistics() const noexcept {
        return m_impl ? m_impl->stats : EMPTY_STATS;
    }
    compute::Statistics Program::compute_statistics() const noexcept {
        return m_impl && m_impl->compute_device
                   ? m_impl->compute_device->statistics()
                   : compute::Statistics{};
    }

    const compute::Capabilities *Program::compute_capabilities() const noexcept {
        return m_impl && m_impl->compute_device ? &m_impl->compute_device->capabilities() : nullptr;
    }
}  // namespace hyprimate::shader
