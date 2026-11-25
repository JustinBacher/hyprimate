#include "GlesState.hpp"

#include "GlesRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hyprimate::shader {
    using namespace gles_detail;
    namespace {
        constexpr unsigned MAX_EXTENT = 8192;
        constexpr std::uint64_t MAX_PIXELS = 16'777'216;
        constexpr std::array<GLfloat, 4> TRANSPARENT{};
        constexpr std::array<std::uint8_t, MAX_EXTENT * 16> ZEROS{};
        constexpr Statistics EMPTY_STATS{};




        struct PixelFormat {
            GLenum internal, layout, type;
            unsigned bytes;
        };

        constexpr PixelFormat pixel_format(Format format) {
            switch (format) {
                case Format::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
                case Format::RG8: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
                case Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                case Format::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT, 2};
                case Format::RG16F: return {GL_RG16F, GL_RG, GL_HALF_FLOAT, 4};
                case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
                case Format::R32F: return {GL_R32F, GL_RED, GL_FLOAT, 4};
                case Format::RG32F: return {GL_RG32F, GL_RG, GL_FLOAT, 8};
                case Format::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
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

        Extent texture_extent(const Size &size, Extent capture) {
            if (size.width != 0) {
                return {size.width, size.height};
            }
            return {std::max(1U, static_cast<unsigned>(std::ceil(capture.width * double{size.scale}))),
                std::max(1U, static_cast<unsigned>(std::ceil(capture.height * double{size.scale})))};
        }

        constexpr GLenum draw_primitive(Primitive primitive) {
            switch (primitive) {
                case Primitive::Points: return GL_POINTS;
                case Primitive::Lines: return GL_LINES;
                case Primitive::Triangles: return GL_TRIANGLES;
                case Primitive::TriangleStrip: return GL_TRIANGLE_STRIP;
            }
            return GL_TRIANGLES;
        }


        void texture_storage(GLuint &name, const Texture &texture, Extent extent) {
            glGenTextures(1, &name);
            glBindTexture(GL_TEXTURE_2D, name);
            glTexStorage2D(GL_TEXTURE_2D, 1, pixel_format(texture.format).internal, extent.width,
                extent.height);
            const GLint filter = texture.linear ? GL_LINEAR : GL_NEAREST;
            const GLint wrap = texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        }

        enum Uniform : std::size_t {
            Progress, Elapsed, Delta, Duration, Scale, SizeXY, TargetSize, Pointer, Seed,
            SourceUV, Domain, EventID, PointerValid, Fullscreen, Iteration, Iterations,
            DestinationUV, Geometry, PreviousGeometry, Velocity, Direction, Held, Settling,
            BlurMask, OutputSize, OutputOrigin, OutputRect, OriginalUV, OriginalAvailable,
            ProcessedAvailable, StateAvailable, DisturbancesAvailable, InteractionCount, UniformCount
        };
        struct Builtin {
            std::string_view name;
            GLenum type;
        };
        constexpr std::array<Builtin, UniformCount> BUILTINS{{
            {"hypr_progress", GL_FLOAT}, {"hypr_elapsed", GL_FLOAT}, {"hypr_delta", GL_FLOAT},
            {"hypr_duration", GL_FLOAT}, {"hypr_scale", GL_FLOAT}, {"hypr_size", GL_FLOAT_VEC2},
            {"hypr_target_size", GL_FLOAT_VEC2}, {"hypr_pointer", GL_FLOAT_VEC2},
            {"hypr_seed", GL_FLOAT_VEC4}, {"hypr_source_uv", GL_FLOAT_VEC4},
            {"hypr_domain", GL_FLOAT_VEC4}, {"hypr_event", GL_INT},
            {"hypr_pointer_valid", GL_BOOL}, {"hypr_fullscreen", GL_BOOL},
            {"hypr_iteration", GL_INT}, {"hypr_iterations", GL_INT},
            {"hypr_destination_uv", GL_FLOAT_VEC4}, {"hypr_geometry", GL_FLOAT_VEC4},
            {"hypr_previous_geometry", GL_FLOAT_VEC4}, {"hypr_velocity", GL_FLOAT_VEC4},
            {"hypr_direction", GL_FLOAT}, {"hypr_held", GL_BOOL}, {"hypr_settling", GL_BOOL},
            {"hypr_blur_mask", GL_BOOL}, {"hypr_output_size", GL_FLOAT_VEC2},
            {"hypr_output_origin", GL_FLOAT_VEC2}, {"hypr_output_rect", GL_FLOAT_VEC4},
            {"hypr_wallpaper_uv", GL_FLOAT_VEC4}, {"hypr_wallpaper_original_available", GL_BOOL},
            {"hypr_wallpaper_processed_available", GL_BOOL}, {"hypr_environment_state_available", GL_BOOL},
            {"hypr_disturbances_available", GL_BOOL}, {"hypr_interaction_count", GL_INT}}};
        bool linear_host_image(ResourceKind kind) noexcept {
            return kind == ResourceKind::Source || kind == ResourceKind::Destination ||
                kind == ResourceKind::Original || kind == ResourceKind::Processed;
        }
        GLuint environmental_texture(ResourceKind kind, const EnvironmentInputs &inputs) noexcept {
            switch (kind) {
                case ResourceKind::Original: return inputs.original;
                case ResourceKind::Processed: return inputs.processed;
                case ResourceKind::EnvironmentState: return inputs.state;
                case ResourceKind::Disturbances: return inputs.disturbances;
                default: return 0;
            }
        }
        constexpr std::array<GLenum, 6> VALUE_TYPES{
            GL_FLOAT, GL_INT, GL_BOOL, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};

        unsigned float_columns(GLenum type) {
            switch (type) {
                case GL_FLOAT:
                case GL_FLOAT_VEC2:
                case GL_FLOAT_VEC3:
                case GL_FLOAT_VEC4: return 1;
                case GL_FLOAT_MAT2:
                case GL_FLOAT_MAT2x3:
                case GL_FLOAT_MAT2x4: return 2;
                case GL_FLOAT_MAT3:
                case GL_FLOAT_MAT3x2:
                case GL_FLOAT_MAT3x4: return 3;
                case GL_FLOAT_MAT4:
                case GL_FLOAT_MAT4x2:
                case GL_FLOAT_MAT4x3: return 4;
                default: return 0;
            }
        }

        unsigned float_components(GLenum type) {
            switch (type) {
                case GL_FLOAT: return 1;
                case GL_FLOAT_VEC2: return 2;
                case GL_FLOAT_VEC3: return 3;
                case GL_FLOAT_VEC4:
                case GL_FLOAT_MAT2: return 4;
                case GL_FLOAT_MAT2x3:
                case GL_FLOAT_MAT3x2: return 6;
                case GL_FLOAT_MAT2x4:
                case GL_FLOAT_MAT4x2: return 8;
                case GL_FLOAT_MAT3: return 9;
                case GL_FLOAT_MAT3x4:
                case GL_FLOAT_MAT4x3: return 12;
                case GL_FLOAT_MAT4: return 16;
                default: return 0;
            }
        }

        void set_value(GLint location, const Value &value) {
            switch (value.type) {
                case ValueType::Float: glUniform1f(location, value.numbers[0]); break;
                case ValueType::Int:
                case ValueType::Bool: glUniform1i(location, value.integer); break;
                case ValueType::Vec2: glUniform2fv(location, 1, value.numbers.data()); break;
                case ValueType::Vec3: glUniform3fv(location, 1, value.numbers.data()); break;
                case ValueType::Vec4: glUniform4fv(location, 1, value.numbers.data()); break;
            }
        }

        struct Block {
            Ref buffer;
            GLsizeiptr bytes;
        };

        struct CompiledPass {
            GLuint program = 0;
            std::array<GLint, UniformCount> uniforms;
            std::array<Ref, MAX_SHADER_SAMPLERS> samplers{};
            GLint interactions = -1;
            GLint interaction_vectors = 0;
            bool uses_environment = false;
            std::array<Block, MAX_BLOCK_BINDINGS> blocks{};
            unsigned sampler_count = 0, block_count = 0;

            CompiledPass() { uniforms.fill(-1); }
        };

        std::string shader_log(GLuint name, bool program) {
            GLint size = 0;
            if (program) {
                glGetProgramiv(name, GL_INFO_LOG_LENGTH, &size);
            } else {
                glGetShaderiv(name, GL_INFO_LOG_LENGTH, &size);
            }
            std::string log(static_cast<std::size_t>(std::clamp(size, 1, 64 * 1024)), '\0');
            GLsizei length = 0;
            if (program) {
                glGetProgramInfoLog(name, static_cast<GLsizei>(log.size()), &length, log.data());
            } else {
                glGetShaderInfoLog(name, static_cast<GLsizei>(log.size()), &length, log.data());
            }
            log.resize(static_cast<std::size_t>(length));
            return log;
        }

        GLuint compile(GLenum stage, const std::string &source, std::string &error) {
            const auto shader = glCreateShader(stage);
            if (shader == 0) {
                error = "cannot create shader";
                return 0;
            }
            const auto *body = source.c_str();
            const auto bytes = static_cast<GLint>(source.size());
            glShaderSource(shader, 1, &body, &bytes);
            glCompileShader(shader);
            GLint status = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (status == GL_TRUE) {
                return shader;
            }
            error = (stage == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") +
                    shader_log(shader, false);
            glDeleteShader(shader);
            return 0;
        }

        bool link(const Package &package, const Pass &pass, CompiledPass &gpu, std::string &error) {
            const auto vertex =
                compile(GL_VERTEX_SHADER, shader_source(package, pass.vertex, true), error);
            if (vertex == 0) {
                return false;
            }
            // Do not inject raster ABI inputs into the inert feedback fragment.
            const auto fragment = compile(GL_FRAGMENT_SHADER,
                pass.transform_feedback ? "#version 300 es\nvoid main() {}\n" :
                                          shader_source(package, pass.fragment, false), error);
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
                glTransformFeedbackVaryings(gpu.program, static_cast<GLsizei>(names.size()),
                    names.data(), GL_INTERLEAVED_ATTRIBS);
            }
            glLinkProgram(gpu.program);
            glDetachShader(gpu.program, vertex);
            glDetachShader(gpu.program, fragment);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint status = GL_FALSE;
            glGetProgramiv(gpu.program, GL_LINK_STATUS, &status);
            return status == GL_TRUE || fail(error, "link: " + shader_log(gpu.program, true));
        }

        const Mesh *pass_mesh(const Package &package, const Pass &pass) {
            return pass.mesh < 0 ? nullptr : &package.meshes[static_cast<unsigned>(pass.mesh)];
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
                glGetActiveAttrib(program, static_cast<GLuint>(i), max_name, &length, &elements,
                    &type, name.data());
                const std::string_view attribute(name.data(), length);
                if (attribute.starts_with("gl_")) {
                    continue;
                }
                const auto columns = float_columns(type);
                const GLint location = glGetAttribLocation(program, name.data());
                if (!mesh || columns == 0 || location < 0 || elements != 1) {
                    return fail(error, "unbound or non-float vertex attribute: " +
                                           std::string(attribute));
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

        bool reflect_feedback(const Package &package, const Pass &pass, const Mesh *mesh,
            GLuint program, const Limits &limits, std::string &error) {
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
                glGetTransformFeedbackVarying(program, static_cast<GLuint>(i), 1, nullptr,
                    &elements, &type, ignored_name);
                const auto width = float_components(type);
                if (width == 0 || elements < 1) {
                    return fail(error, "transform feedback requires float varying data");
                }
                components += static_cast<std::size_t>(elements) * width;
            }
            if (components > static_cast<std::size_t>(limits.feedback_components)) {
                return fail(error, "transform feedback exceeds GL interleaved component limit");
            }
            const unsigned primitive_size = mesh->primitive == Primitive::Triangles ? 3U :
                                            mesh->primitive == Primitive::Lines ? 2U : 1U;
            if (mesh->count % primitive_size != 0) {
                return fail(error, "transform feedback mesh must contain complete primitives");
            }
            const auto bytes = std::uint64_t{mesh->count} * mesh->instances * components * sizeof(float);
            const auto &destination = package.buffers[pass.feedback.index];
            if (bytes > destination.bytes) {
                return fail(error, "transform feedback output buffer is too small");
            }
            if (destination.lifetime == Lifetime::Transient && bytes != destination.bytes) {
                return fail(error, "transform feedback must write every byte of a transient buffer");
            }
            return true;
        }

        bool reflect_uniforms(const Package &package, const Pass &pass, const Values &values,
            const Limits &limits, CompiledPass &gpu, std::string &error) {
            GLint count = 0, max_name = 0;
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORMS, &count);
            glGetProgramiv(gpu.program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name);
            std::string name(static_cast<std::size_t>(max_name), '\0');
            // GLES links per-stage sampler/UBO limits. The v3.0 uniform interface exposes
            // combined active samplers, not their originating shader stage.
            glUseProgram(gpu.program);
            for (GLuint i = 0; i < static_cast<GLuint>(count); ++i) {
                GLint block = -1;
                glGetActiveUniformsiv(gpu.program, 1, &i, GL_UNIFORM_BLOCK_INDEX, &block);
                if (block >= 0) {
                    continue;
                }
                GLsizei length = 0;
                GLint elements = 0;
                GLenum type = 0;
                glGetActiveUniform(gpu.program, i, max_name, &length, &elements, &type, name.data());
                const std::string_view uniform(name.data(), length);
                const auto location = glGetUniformLocation(gpu.program, name.data());
                gpu.uses_environment |= package.environment.used();
                if (uniform == "hypr_interactions[0]" && package.environment.interactions &&
                    type == GL_FLOAT_VEC4 && elements > 0 && elements <= int(MAX_INTERACTIONS * 4)) {
                    gpu.interactions = location;
                    gpu.interaction_vectors = elements;
                    continue;
                }
                const auto builtin = std::ranges::find(BUILTINS, uniform, &Builtin::name);
                if (builtin != BUILTINS.end() && type == builtin->type && elements == 1) {
                    gpu.uniforms[static_cast<std::size_t>(builtin - BUILTINS.begin())] = location;
                    continue;
                }
                const auto parameter = std::ranges::find(package.parameters, uniform, &Parameter::name);
                if (parameter != package.parameters.end() && elements == 1 &&
                    type == VALUE_TYPES[static_cast<unsigned>(parameter->initial.type)]) {
                    set_value(location, values[static_cast<std::size_t>(parameter -
                                            package.parameters.begin())]);
                    continue;
                }
                const auto input = std::ranges::find(pass.inputs, uniform, &Binding::name);
                const auto &environment = package.environment;
                std::optional<Ref> sampler;
                if (uniform == "hypr_source") sampler = Ref{};
                else if (uniform == "hypr_destination") sampler = Ref{ResourceKind::Destination};
                else if (uniform == "hypr_wallpaper_original" && environment.original) sampler = Ref{ResourceKind::Original};
                else if (uniform == "hypr_wallpaper_processed" && environment.processed) sampler = Ref{ResourceKind::Processed};
                else if (uniform == "hypr_environment_state" && environment.sample_state) sampler = Ref{ResourceKind::EnvironmentState};
                else if (uniform == "hypr_disturbances" && environment.interactions && !environment.channel.empty()) sampler = Ref{ResourceKind::Disturbances};
                else if (input != pass.inputs.end()) sampler = input->resource;
                if (type != GL_SAMPLER_2D || elements != 1 || !sampler) {
                    return fail(error, "unsupported or unmapped uniform: " + std::string(uniform));
                }
                const auto ref = *sampler;
                if (ref.kind >= ResourceKind::Original && !package.supports(Event::Wallpaper) &&
                    pass.phase != Phase::Render) {
                    return fail(error, "window packages can only sample the environment in their render phase");
                }
                unsigned unit = 0;
                while (unit < gpu.sampler_count && gpu.samplers[unit] != ref) {
                    ++unit;
                }
                if (unit == gpu.sampler_count) {
                    if (unit >= gpu.samplers.size() || unit >= static_cast<unsigned>(limits.samplers)) {
                        return fail(error, "active sampler bindings exceed the GLES texture unit limit");
                    }
                    gpu.samplers[gpu.sampler_count++] = ref;
                }
                glUniform1i(location, static_cast<GLint>(unit));
            }
            return true;
        }

        bool reflect_blocks(const Package &package, const Pass &pass, const Limits &limits,
            CompiledPass &gpu, std::string &error) {
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
                    static_cast<std::size_t>(bytes) > package.buffers[binding->resource.index].bytes) {
                    return fail(error, "uniform block exceeds its GLES/buffer size: " +
                                           std::string(block));
                }
                gpu.blocks[gpu.block_count++] = {binding->resource, bytes};
                glUniformBlockBinding(gpu.program, i, i);
            }
            return true;
        }

        // ES 3.0 can query a named output's location but cannot enumerate or
        // query its type. Accept a deliberately small output declaration grammar.
        bool token_word(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
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
                        source.front() == '\n' || source.front() == '\v' || source.front() == '\f') {
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
                                return fail(error, "fragment macros cannot introduce output declarations");
                            }
                        }
                        if (!directive.valid) {
                            return fail(error, "fragment macros must fit on one logical line");
                        }
                    } else if (kind != "elif" && kind != "else" && kind != "line" && kind != "pragma") {
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
                        const auto result = std::from_chars(number.data(), number.data() + number.size(), slot);
                        auto type = lex.next();
                        if (std::ranges::find(PRECISION, type) != PRECISION.end()) {
                            type = lex.next();
                        }
                        const auto name = lex.next();
                        if (result.ec != std::errc{} || result.ptr != number.data() + number.size() ||
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
                    glGetFragDataLocation(program, std::string(name).c_str()) != static_cast<GLint>(i)) {
                    return fail(error, "fragment output is missing, inactive, or macro-defined at location " +
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

            bool check(const Package &package, const Pass &pass, Statistics &stats, std::string &error) {
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
                    return fail(error, "unsupported GLES framebuffer format/MRT combination: " + pass.name);
                }
                return gl_ok(error, "checking GLES framebuffer format probe");
            }
        };
    }  // namespace

    struct Program::Impl {
        const Package package;
        Limits limits;
        IndexedColor color;
        Statistics stats;
        std::array<CompiledPass, MAX_PASSES> passes;
        std::array<GLuint, MAX_TEXTURES> assets{};
        std::array<GLuint, MAX_MESHES> elements{};
        GLuint source_sampler = 0;
        unsigned sampler_count = 0, block_count = 0, max_outputs = 1;
        bool has_feedback = false, samples_borrowed_target = false, blends_final = false, armed = true;
        unsigned final_draws = 0;
        bool final_is_last = false, uses_frag_coord = false, uses_destination = false;
        bool final_source_over = true;
        bool direct() const noexcept {
            return final_draws > 0 && final_is_last && final_source_over && !uses_frag_coord;
        }

        explicit Impl(Package spec) : package(std::move(spec)) {}

        ~Impl() {
            if (!armed) {
                return;
            }
            for (const auto &pass : passes) {
                if (pass.program != 0) {
                    glDeleteProgram(pass.program);
                }
            }
            glDeleteTextures(static_cast<GLsizei>(assets.size()), assets.data());
            glDeleteBuffers(static_cast<GLsizei>(elements.size()), elements.data());
            if (source_sampler != 0) {
                glDeleteSamplers(1, &source_sampler);
            }
        }

        bool prepare(const Values &values, GlResolver resolver, std::string &error) {
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
                    return fail(error, "32-bit float linear texture filtering is unsupported: " +
                                           texture.name);
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
            for (std::size_t i = 0; i < spec.passes.size(); ++i) {
                const auto &pass = spec.passes[i];
                auto &gpu = passes[i];
                const auto *mesh = pass_mesh(spec, pass);
                if (pass.outputs.size() > static_cast<std::size_t>(limits.draw_buffers) ||
                    pass.outputs.size() > static_cast<std::size_t>(limits.color_attachments)) {
                    return fail(error, "MRT exceeds GLES draw buffer / attachment limit: " + pass.name);
                }
                if (pass.transform_feedback &&
                    (!mesh || !mesh->indices.empty() || mesh->primitive == Primitive::TriangleStrip)) {
                    return fail(error, "GLES 3.0 feedback requires non-indexed points, lines or triangles: " +
                                           pass.name);
                }
                if (pass.blend && !limits.float_blend &&
                    std::ranges::any_of(pass.outputs, [&](const Ref &output) {
                        return output.kind == ResourceKind::Texture &&
                               pixel_format(spec.textures[output.index].format).type == GL_FLOAT;
                    })) {
                    return fail(error, "GLES 32-bit float target cannot blend: " + pass.name);
                }
                if (!link(spec, pass, gpu, error) ||
                    !reflect_outputs(pass, gpu.program, error) ||
                    !reflect_attributes(mesh, gpu.program, error) ||
                    !reflect_feedback(spec, pass, mesh, gpu.program, limits, error) ||
                    !reflect_uniforms(spec, pass, values, limits, gpu, error) ||
                    !reflect_blocks(spec, pass, limits, gpu, error) || !gl_ok(error, "preparing pass")) {
                    error = pass.name + ": " + error;
                    return false;
                }
                sampler_count = std::max(sampler_count, gpu.sampler_count);
                block_count = std::max(block_count, gpu.block_count);
                max_outputs = std::max(max_outputs, static_cast<unsigned>(pass.outputs.size()));
                has_feedback |= pass.transform_feedback;
                uses_destination |= std::ranges::any_of(
                    std::views::counted(gpu.samplers.begin(), gpu.sampler_count),
                    [](Ref ref) { return ref.kind == ResourceKind::Destination; });
                if (!pass.outputs.empty() && pass.outputs.front().kind == ResourceKind::Output) {
                    final_is_last = i + 1 == spec.passes.size();
                    const auto builtin = default_vertex();
                    const bool fullscreen = pass.mesh < 0 &&
                        (pass.vertex == builtin ||
                            (pass.vertex.size() == builtin.size() + 1 &&
                                pass.vertex.front() == '\n' && pass.vertex.ends_with(builtin)));
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

        bool preflight_formats(std::string &error) {
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

        bool upload(std::string &error) {
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
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), indices.data(), GL_STATIC_DRAW);
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
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width, size.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, desc.image.data());
                count_storage(stats, desc.image.size());
                stats.upload_bytes += desc.image.size();
            }
            const bool source_used = std::ranges::any_of(passes, [](const CompiledPass &pass) {
                return std::ranges::any_of(std::views::counted(pass.samplers.begin(), pass.sampler_count),
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
    };

    Program::Program() = default;
    Program::~Program() = default;

    bool Program::initialize(Package package, const Values &values, std::string &error,
        GlResolver resolver) {
        error.clear();
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
        auto prepared = std::make_shared<Impl>(std::move(package));
        const bool ok = prepared->prepare(values, resolver, error);
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

    void Program::release() noexcept { m_impl.reset(); }

    void Program::abandon() noexcept {
        if (m_impl) {
            m_impl->armed = false;
            m_impl.reset();
        }
    }

    const Package &Program::package() const { return m_impl->package; }

    bool Program::can_composite_direct() const noexcept { return m_impl && m_impl->direct(); }

    const Statistics &Program::statistics() const noexcept { return m_impl ? m_impl->stats : EMPTY_STATS; }

    namespace {

        // UBO indexed bindings are context state; TF indexed bindings belong to
        // the selected TF object and are preserved by reselecting the caller's object.


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


        GLint attachment(GLenum point, GLenum parameter) {
            GLint value = 0;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, point, parameter, &value);
            return value;
        }

        bool valid_source_uv(const std::array<float, 4> &r) {
            // Permit float rounding at the far edge of a host-computed rect.
            constexpr float ONE = 1.000001F;
            return std::ranges::all_of(r, [](float f) { return std::isfinite(f); }) &&
                   r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 && r[0] + r[2] <= ONE &&
                   r[1] + r[3] <= ONE;
        }

        std::array<float, 4> seed_values(std::uint64_t seed) {
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

        void frame_uniforms(const CompiledPass &gpu, const Timing &time, const Source &source,
            Extent extent, const std::array<float, 4> &domain, unsigned iter, unsigned iterations,
            bool blur_mask) {
            const auto &u = gpu.uniforms;
            const auto &f = time.frame;
            // Skip optimized-out uniforms rather than issuing no-op driver calls.
            if (u[Progress] >= 0) glUniform1f(u[Progress], f.progress);
            if (u[Elapsed] >= 0) glUniform1f(u[Elapsed], f.elapsed);
            if (u[Delta] >= 0) glUniform1f(u[Delta], f.delta);
            if (u[Duration] >= 0) glUniform1f(u[Duration], f.duration);
            if (u[Scale] >= 0) glUniform1f(u[Scale], f.scale);
            if (u[SizeXY] >= 0) glUniform2f(u[SizeXY], f.width, f.height);
            if (u[TargetSize] >= 0) glUniform2f(u[TargetSize], static_cast<GLfloat>(extent.width),
                static_cast<GLfloat>(extent.height));
            if (u[Pointer] >= 0) glUniform2f(u[Pointer], f.pointer_x, f.pointer_y);
            if (u[Seed] >= 0) glUniform4fv(u[Seed], 1, time.seed.data());
            if (u[SourceUV] >= 0) glUniform4fv(u[SourceUV], 1, source.uv.data());
            if (u[Domain] >= 0) glUniform4fv(u[Domain], 1, domain.data());
            if (u[EventID] >= 0) glUniform1i(u[EventID], static_cast<GLint>(f.event));
            if (u[PointerValid] >= 0) glUniform1i(u[PointerValid], f.pointer_valid);
            if (u[Fullscreen] >= 0) glUniform1i(u[Fullscreen], f.fullscreen);
            if (u[Iteration] >= 0) glUniform1i(u[Iteration], static_cast<GLint>(iter));
            if (u[Iterations] >= 0) glUniform1i(u[Iterations], static_cast<GLint>(iterations));
            if (u[DestinationUV] >= 0) glUniform4fv(u[DestinationUV], 1, source.destination_uv.data());
            if (u[Geometry] >= 0) glUniform4fv(u[Geometry], 1, f.geometry.data());
            if (u[PreviousGeometry] >= 0) glUniform4fv(u[PreviousGeometry], 1, f.previous_geometry.data());
            if (u[Velocity] >= 0) glUniform4fv(u[Velocity], 1, f.velocity.data());
            if (u[Direction] >= 0) glUniform1f(u[Direction], f.direction);
            if (u[Held] >= 0) glUniform1i(u[Held], f.held);
            if (u[Settling] >= 0) glUniform1i(u[Settling], f.settling);
            if (u[BlurMask] >= 0) glUniform1i(u[BlurMask], blur_mask);
            if (!gpu.uses_environment) return;
            const auto &env = source.environment;
            if (u[OutputSize] >= 0) glUniform2fv(u[OutputSize], 1, env.output_size.data());
            if (u[OutputOrigin] >= 0) glUniform2fv(u[OutputOrigin], 1, env.output_origin.data());
            if (u[OutputRect] >= 0) glUniform4fv(u[OutputRect], 1, env.output_rect.data());
            if (u[OriginalUV] >= 0) glUniform4fv(u[OriginalUV], 1, env.original_uv.data());
            if (u[OriginalAvailable] >= 0) glUniform1i(u[OriginalAvailable], env.original != 0);
            if (u[ProcessedAvailable] >= 0) glUniform1i(u[ProcessedAvailable], env.processed != 0);
            if (u[StateAvailable] >= 0) glUniform1i(u[StateAvailable], env.state != 0);
            if (u[DisturbancesAvailable] >= 0) glUniform1i(u[DisturbancesAvailable], env.disturbances != 0);
            if (u[InteractionCount] >= 0) glUniform1i(u[InteractionCount], static_cast<GLint>(env.interactions.size()));
        }
    }  // namespace

    struct Instance::Impl {
        std::shared_ptr<Program::Impl> owner;
        Extent capture;
        Statistics stats;
        std::array<TexturePair, MAX_TEXTURES> textures;
        std::array<BufferPair, MAX_BUFFERS> buffers;
        std::array<DrawFramebuffer, MAX_PASSES> framebuffers;
        std::array<VertexArray, MAX_MESHES> vaos;
        std::vector<GLenum> saved_draw_buffers;
        GLuint empty_vao = 0, feedback = 0;
        Timing pending_frame, committed_frame;
        bool have_tick = false, pending = false, primed = false, faulted = false, armed = true;

        ~Impl() {
            if (!armed) {
                return;
            }
            if (feedback != 0) {
                glDeleteTransformFeedbacks(1, &feedback);
            }
            if (empty_vao != 0) {
                glDeleteVertexArrays(1, &empty_vao);
            }
            for (const auto &vao : vaos) {
                if (vao.name != 0) {
                    glDeleteVertexArrays(1, &vao.name);
                }
            }
            for (const auto &fbo : framebuffers) {
                if (fbo.name != 0) {
                    glDeleteFramebuffers(1, &fbo.name);
                }
            }
            for (const auto &buffer : buffers) {
                if (buffer.names[0] != 0) {
                    glDeleteBuffers(static_cast<GLsizei>(buffer.names.size()), buffer.names.data());
                }
            }
            for (const auto &texture : textures) {
                if (texture.names[0] != 0) {
                    glDeleteTextures(static_cast<GLsizei>(texture.names.size()), texture.names.data());
                }
            }
        }

        bool size_storage(std::string &error) {
            if (!capture.bounded() || !owner->limits.viewport_fits(capture.width, capture.height)) {
                return fail(error, "capture dimensions exceed the GLES/bounded extent");
            }
            const auto &package = owner->package;
            std::uint64_t bytes = 0;
            for (std::size_t i = 0; i < package.textures.size(); ++i) {
                const auto &desc = package.textures[i];
                if (!desc.image.empty()) {
                    continue;
                }
                const auto size = texture_extent(desc.size, capture);
                if (!size.bounded() || size.width > static_cast<unsigned>(owner->limits.texture) ||
                    size.height > static_cast<unsigned>(owner->limits.texture)) {
                    return fail(error, "texture dimensions exceed the GLES/bounded extent: " + desc.name);
                }
                textures[i].size = size;
                bytes += std::uint64_t{size.width} * size.height * pixel_format(desc.format).bytes *
                         (desc.ping_pong ? 2 : 1);
            }
            for (const auto &buffer : package.buffers) {
                bytes += buffer.bytes * (buffer.ping_pong ? 2 : 1);
            }
            if (bytes > MAX_INSTANCE_BYTES) {
                return fail(error, "shader instance exceeds its 128 MiB storage budget");
            }
            for (std::size_t i = 0; i < package.passes.size(); ++i) {
                const auto &outputs = package.passes[i].outputs;
                if (outputs.empty() || outputs.front().kind != ResourceKind::Texture) {
                    continue;
                }
                const auto size = textures[outputs.front().index].size;
                if (!owner->limits.viewport_fits(size.width, size.height) ||
                    std::ranges::any_of(outputs, [&](Ref ref) { return textures[ref.index].size != size; })) {
                    return fail(error, "incompatible GLES output dimensions: " + package.passes[i].name);
                }
                framebuffers[i].size = size;
            }
            return true;
        }

        GLuint texture(Ref ref, const Source &source) const {
            if (ref.kind == ResourceKind::Source) {
                return source.texture;
            }
            if (ref.kind == ResourceKind::Destination) {
                return source.destination_texture;
            }
            if (ref.kind >= ResourceKind::Original) return environmental_texture(ref.kind, source.environment);
            if (owner->assets[ref.index] != 0) {
                return owner->assets[ref.index];
            }
            const auto &pair = textures[ref.index];
            return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
        }

        GLuint buffer(Ref ref) const {
            const auto &pair = buffers[ref.index];
            return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
        }

        void configure_mesh(unsigned index, bool create) {
            const auto &mesh = owner->package.meshes[index];
            auto &vao = vaos[index];
            glBindVertexArray(vao.name);
            if (create && owner->elements[index] != 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, owner->elements[index]);
            }
            for (std::size_t i = 0; i < mesh.attributes.size(); ++i) {
                const auto &attr = mesh.attributes[i];
                const auto name = buffer(attr.buffer);
                if (vao.attached[i] == name) {
                    continue;
                }
                glBindBuffer(GL_ARRAY_BUFFER, name);
                glVertexAttribPointer(attr.location, attr.components, GL_FLOAT, GL_FALSE, attr.stride,
                    reinterpret_cast<const void *>(static_cast<std::uintptr_t>(attr.offset)));
                if (create) {
                    glEnableVertexAttribArray(attr.location);
                    glVertexAttribDivisor(attr.location, attr.divisor);
                }
                vao.attached[i] = name;
            }
        }

        void attach_outputs(DrawFramebuffer &fbo, const Pass &pass) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo.name);
            for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
                const auto name = texture(pass.outputs[i], {});
                if (fbo.attached[i] != name) {
                    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                        GL_TEXTURE_2D, name, 0);
                    fbo.attached[i] = name;
                }
            }
        }

        bool clear_texture(PrepareState &saved, GLuint name, PixelFormat format, Extent size,
            std::string &error) {
            if (saved.zero_framebuffer == 0) {
                glGenFramebuffers(1, &saved.zero_framebuffer);
                if (saved.zero_framebuffer == 0) {
                    return fail(error, "cannot create texture initialization framebuffer");
                }
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved.zero_framebuffer);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, name, 0);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glClearBufferfv(GL_COLOR, 0, TRANSPARENT.data());
            } else {
                // A legal sampled floating-point format need not be color-renderable.
                const auto row = std::size_t{size.width} * format.bytes;
                const auto batch = static_cast<unsigned>(ZEROS.size() / row);
                for (unsigned y = 0; y < size.height;) {
                    const auto rows = std::min(batch, size.height - y);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, size.width, rows, format.layout,
                        format.type, ZEROS.data());
                    stats.upload_bytes += row * rows;
                    y += rows;
                }
            }
            return gl_ok(error, "zeroing persistent texture");
        }

        bool allocate(std::string &error) {
            if (!size_storage(error)) {
                return false;
            }
            const auto &package = owner->package;
            PrepareState saved(owner->color);
            saved_draw_buffers.resize(static_cast<std::size_t>(owner->limits.draw_buffers));
            for (std::size_t i = 0; i < package.textures.size(); ++i) {
                const auto &desc = package.textures[i];
                if (!desc.image.empty()) {
                    continue;
                }
                auto &pair = textures[i];
                const auto format = pixel_format(desc.format);
                const auto size = pair.size;
                for (unsigned side = 0; side < (desc.ping_pong ? 2U : 1U); ++side) {
                    auto &name = pair.names[side];
                    texture_storage(name, desc, size);
                    if (!gl_ok(error, "allocating texture")) {
                        return false;
                    }
                    count_storage(stats, std::size_t{size.width} * size.height * format.bytes);
                    if (desc.lifetime == Lifetime::Persistent &&
                        !clear_texture(saved, name, format, size, error)) {
                        return false;
                    }
                }
            }
            for (std::size_t i = 0; i < package.buffers.size(); ++i) {
                const auto &desc = package.buffers[i];
                auto &pair = buffers[i];
                for (unsigned side = 0; side < (desc.ping_pong ? 2U : 1U); ++side) {
                    auto &name = pair.names[side];
                    glGenBuffers(1, &name);
                    glBindBuffer(GL_ARRAY_BUFFER, name);
                    const auto *initial = desc.initial.size() == desc.bytes ? desc.initial.data() : nullptr;
                    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.bytes), initial,
                        GL_DYNAMIC_COPY);
                    if (!gl_ok(error, "allocating buffer")) {
                        return false;
                    }
                    count_storage(stats, desc.bytes);
                    if (initial) {
                        stats.upload_bytes += desc.bytes;
                        continue;
                    }
                    if (!desc.initial.empty()) {
                        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(desc.initial.size()),
                            desc.initial.data());
                        stats.upload_bytes += desc.initial.size();
                    }
                    for (std::size_t at = desc.initial.size(); at < desc.bytes;) {
                        const auto bytes = std::min(ZEROS.size(), desc.bytes - at);
                        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(at),
                            static_cast<GLsizeiptr>(bytes), ZEROS.data());
                        at += bytes;
                        stats.upload_bytes += bytes;
                    }
                    if (!gl_ok(error, "initializing buffer")) {
                        return false;
                    }
                }
            }
            for (std::size_t i = 0; i < package.passes.size(); ++i) {
                auto &fbo = framebuffers[i];
                if (fbo.size.width == 0) {
                    continue;
                }
                const auto &pass = package.passes[i];
                glGenFramebuffers(1, &fbo.name);
                if (fbo.name == 0) {
                    return fail(error, "cannot create shader output framebuffer");
                }
                attach_outputs(fbo, pass);
                std::array<GLenum, MAX_TEXTURES> slots;
                for (std::size_t j = 0; j < pass.outputs.size(); ++j) {
                    slots[j] = GL_COLOR_ATTACHMENT0 + j;
                }
                glDrawBuffers(static_cast<GLsizei>(pass.outputs.size()), slots.data());
                if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    return fail(error, "incomplete shader framebuffer (unsupported GLES format/MRT): " + pass.name);
                }
            }
            for (unsigned i = 0; i < package.meshes.size(); ++i) {
                glGenVertexArrays(1, &vaos[i].name);
                if (vaos[i].name == 0) {
                    return fail(error, "cannot create shader mesh VAO");
                }
                configure_mesh(i, true);
            }
            if (std::ranges::any_of(package.passes, [](const Pass &pass) { return pass.mesh < 0; })) {
                glGenVertexArrays(1, &empty_vao);
                if (empty_vao == 0) {
                    return fail(error, "cannot create shader fullscreen VAO");
                }
            }
            if (owner->has_feedback) {
                glGenTransformFeedbacks(1, &feedback);
                if (feedback == 0) {
                    return fail(error, "cannot create shader transform feedback object");
                }
            }
            return gl_ok(error, "preparing shader instance");
        }

        bool owns_texture(GLuint name) const {
            return std::ranges::find(owner->assets, name) != owner->assets.end() ||
                   std::ranges::any_of(textures, [name](const TexturePair &pair) {
                       return pair.names[0] == name || pair.names[1] == name;
                   });
        }

        bool validate_target(const Source &source, const Target &target, std::string &error) const {
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                return fail(error, "borrowed output framebuffer is incomplete");
            }
            if (target.framebuffer == 0) {
                return true;
            }
            const auto type = attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
            if (type == GL_NONE) {
                return fail(error, "borrowed output is missing color attachment zero");
            }
            const auto color = type == GL_TEXTURE ?
                static_cast<GLuint>(attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME)) : 0;
            const auto &env = source.environment;
            const auto aliases_environment = [&](GLuint name) {
                return owner->package.environment.used() &&
                    (name == env.original || name == env.processed || name == env.state || name == env.disturbances);
            };
            if ((target.texture != 0 && target.texture != color) || color == source.texture ||
                (color && color == source.destination_texture) ||
                (color != 0 && (owns_texture(color) || aliases_environment(color)))) {
                return fail(error, "borrowed framebuffer color attachment aliases an input or disagrees with its texture");
            }
            // Even a disabled draw slot can form a GLES texture/framebuffer feedback loop.
            if (owner->samples_borrowed_target) {
                const auto aliases = [&](GLenum point) {
                    if (attachment(point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) != GL_TEXTURE) {
                        return false;
                    }
                    const auto name = static_cast<GLuint>(attachment(point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME));
                    return name == source.texture || name == source.destination_texture ||
                        owns_texture(name) || aliases_environment(name);
                };
                for (GLint i = 1; i < owner->limits.color_attachments; ++i) {
                    if (aliases(GL_COLOR_ATTACHMENT0 + i)) {
                        return fail(error, "borrowed output attachments alias a shader input");
                    }
                }
                if (aliases(GL_DEPTH_ATTACHMENT) || aliases(GL_STENCIL_ATTACHMENT)) {
                    return fail(error, "borrowed depth/stencil attachments alias a shader input");
                }
            }
            const auto component = attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
            if (component == GL_INT || component == GL_UNSIGNED_INT) {
                return fail(error, "integer borrowed output is not a native color target");
            }
            if (component == GL_FLOAT && (owner->blends_final || target.composite) && !owner->limits.float_blend &&
                (attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE) == 32)) {
                return fail(error, "borrowed 32-bit float target does not support blending");
            }
            return true;
        }

        void submit_mesh(const Pass &pass) {
            const auto *mesh = pass_mesh(owner->package, pass);
            if (!mesh) {
                glDrawArrays(GL_TRIANGLES, 0, 3);
                return;
            }
            const auto primitive = draw_primitive(mesh->primitive);
            if (!mesh->indices.empty()) {
                if (mesh->instances == 1) {
                    glDrawElements(primitive, static_cast<GLsizei>(mesh->indices.size()), GL_UNSIGNED_INT, nullptr);
                } else {
                    glDrawElementsInstanced(primitive, static_cast<GLsizei>(mesh->indices.size()),
                        GL_UNSIGNED_INT, nullptr, mesh->instances);
                }
            } else if (mesh->instances == 1) {
                glDrawArrays(primitive, 0, mesh->count);
            } else {
                glDrawArraysInstanced(primitive, 0, mesh->count, mesh->instances);
            }
        }

        bool draw(std::size_t index, unsigned iteration, const Timing &time, const Source &source,
            const Target &target, unsigned &active_blocks, bool &clear_final, std::string &error) {
            constexpr std::array<float, 4> FULL_DOMAIN{0, 0, 1, 1};
            const auto &pass = owner->package.passes[index];
            const auto &gpu = owner->passes[index];
            auto &fbo = framebuffers[index];
            const bool final = fbo.name == 0 && !pass.transform_feedback;
            const auto size = final ? Extent{target.width, target.height} :
                pass.transform_feedback ? capture : fbo.size;
            if (final || pass.transform_feedback) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
            } else {
                attach_outputs(fbo, pass);
            }
            if (!pass.transform_feedback) {
                const auto x = final ? target.origin_x : 0;
                const auto y = final ? target.origin_y : 0;
                glViewport(x, y, size.width, size.height);
                enable(GL_SCISSOR_TEST, final);
                if (final) {
                    glScissor(x, y, size.width, size.height);
                }
                const bool composite = final && target.composite;
                owner->color.blend(static_cast<unsigned>(pass.outputs.size()), composite || pass.blend);
                const bool clear = !composite && (pass.clear || (final && clear_final));
                if (final) clear_final = false;
                if (clear) {
                    for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
                        glClearBufferfv(GL_COLOR, static_cast<GLint>(i), TRANSPARENT.data());
                    }
                }
            }
            glUseProgram(gpu.program);
            if (pass.mesh >= 0) {
                configure_mesh(static_cast<unsigned>(pass.mesh), false);
            } else {
                glBindVertexArray(empty_vao);
            }
            for (unsigned i = 0; i < gpu.sampler_count; ++i) {
                const auto ref = gpu.samplers[i];
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, texture(ref, source));
                glBindSampler(i, linear_host_image(ref.kind) ? owner->source_sampler : 0);
            }
            for (unsigned i = 0; i < gpu.block_count; ++i) {
                const auto &block = gpu.blocks[i];
                glBindBufferRange(GL_UNIFORM_BUFFER, i, buffer(block.buffer), 0, block.bytes);
            }
            // A later TF destination may have been bound to a trailing slot by
            // an earlier pass. No stale UBO binding may outlive that pass.
            for (unsigned i = gpu.block_count; i < active_blocks; ++i) {
                glBindBufferBase(GL_UNIFORM_BUFFER, i, 0);
            }
            active_blocks = gpu.block_count;
            frame_uniforms(gpu, time, source, size, final ? target.domain : FULL_DOMAIN, iteration,
                pass.iterations, target.blur_mask);
            if (gpu.interactions >= 0) {
                const auto vectors = std::min(gpu.interaction_vectors,
                    static_cast<GLint>(source.environment.interactions.size() * 4));
                static_assert(sizeof(Interaction) == sizeof(float) * 16);
                // Linked programs are shared across instances. Overwrite the whole reflected
                // array so inactive slots cannot expose an earlier output's interactions.
                std::array<float, MAX_INTERACTIONS * 16> packed{};
                if (vectors > 0) {
                    std::memcpy(packed.data(), source.environment.interactions.data(),
                        static_cast<std::size_t>(vectors) * 4 * sizeof(float));
                }
                glUniform4fv(gpu.interactions, gpu.interaction_vectors, packed.data());
                stats.upload_bytes += static_cast<std::size_t>(gpu.interaction_vectors) * 4 * sizeof(float);
            }
            if (pass.transform_feedback) {
                const auto &mesh = owner->package.meshes[static_cast<unsigned>(pass.mesh)];
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glEnable(GL_RASTERIZER_DISCARD);
                glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, feedback);
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer(pass.feedback));
                glBeginTransformFeedback(draw_primitive(mesh.primitive));
                submit_mesh(pass);
                glEndTransformFeedback();
                // End does not detach the destination. Subsequent VAO and UBO
                // consumers must see sequential, not simultaneous, bindings.
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
                glDisable(GL_RASTERIZER_DISCARD);
            } else {
                if (final && target.composite) {
                    if (!gl_ok(error, "preparing direct shader submission")) return false;
                    for (const auto &clip : target.clips) {
                        glScissor(clip.x, clip.y, clip.width, clip.height);
                        if (target.submitted) *target.submitted = true;
                        submit_mesh(pass);
                        if (!gl_ok(error, "executing direct shader submission")) return false;
                    }
                } else {
                    submit_mesh(pass);
                }
            }
            if (!gl_ok(error, "executing native shader pass")) {
                return false;
            }
            if (pass.transform_feedback && pass.feedback.side == Side::Write) {
                buffers[pass.feedback.index].read ^= 1U;
            }
            for (const Ref output : pass.outputs) {
                if (output.side == Side::Write) {
                    textures[output.index].read ^= 1U;
                }
            }
            stats.draws += final && target.composite ? target.clips.size() : 1;
            switch (pass.phase) {
                case Phase::Initialize: ++stats.initialize_passes; break;
                case Phase::Update: ++stats.update_passes; break;
                case Phase::Render: ++stats.render_passes; break;
            }
            return true;
        }

        bool phase(Phase phase, const Timing &frame, const Source &source, const Target &target,
            unsigned &active_blocks, bool &clear_final, std::string &error) {
            const auto &passes = owner->package.passes;
            for (std::size_t i = 0; i < passes.size(); ++i) {
                if (passes[i].phase != phase) {
                    continue;
                }
                for (unsigned iteration = 0; iteration < passes[i].iterations; ++iteration) {
                    if (!draw(i, iteration, frame, source, target, active_blocks, clear_final, error)) {
                        // Only a partial simulation can invalidate committed state.
                        // Render scratch is produced again on the next invocation.
                        faulted = phase != Phase::Render;
                        return false;
                    }
                }
            }
            return true;
        }
    };

    Instance::Instance() = default;
    Instance::~Instance() = default;

    bool Instance::initialize(Program &program, unsigned width, unsigned height, std::string &error) {
        error.clear();
        if (!program.m_impl || !program.m_impl->armed) {
            return fail(error, "no prepared GLES shader program");
        }
        const Extent capture{width, height};
        if (m_impl && m_impl->owner == program.m_impl && m_impl->capture == capture && !m_impl->faulted) {
            return true;
        }
        if (m_impl && m_impl->owner == program.m_impl && !m_impl->faulted && capture.bounded() &&
            m_impl->owner->limits.viewport_fits(width, height)) {
            const auto &textures = m_impl->owner->package.textures;
            bool same_storage = true;
            for (std::size_t i = 0; i < textures.size(); ++i) {
                if (textures[i].image.empty() &&
                    texture_extent(textures[i].size, capture) != m_impl->textures[i].size) {
                    same_storage = false;
                    break;
                }
            }
            if (same_storage) {
                m_impl->capture = capture;
                return true;
            }
        }
        if (!gl_ok(error, "entering instance preparation")) {
            return false;
        }
        // Allocating GL resources also mutates bindings that the caller's active TF may need.
        if (integer(GL_TRANSFORM_FEEDBACK_ACTIVE) && !integer(GL_TRANSFORM_FEEDBACK_PAUSED)) {
            return fail(error, "caller transform feedback is active and unpaused");
        }
        auto instance = std::make_unique<Impl>();
        instance->owner = program.m_impl;
        instance->capture = capture;
        if (!instance->allocate(error)) {
            discard_errors();
            return false;
        }
        if (!gl_ok(error, "restoring caller state after instance preparation")) {
            return false;
        }
        m_impl = std::move(instance);
        return true;
    }

    void Instance::advance(const Frame &frame) noexcept {
        if (!m_impl || (m_impl->have_tick && frame.tick <= m_impl->pending_frame.frame.tick)) {
            return;
        }
        auto &instance = *m_impl;
        const auto delta = instance.pending ? instance.pending_frame.frame.delta : 0;
        if (frame.seed != instance.pending_frame.frame.seed) {
            instance.pending_frame.seed = seed_values(frame.seed);
        }
        instance.pending_frame.frame = frame;
        instance.pending_frame.frame.delta += delta;
        instance.have_tick = true;
        instance.pending = true;
    }

    GLuint Instance::exported_texture(Ref resource) const noexcept {
        if (!m_impl || !m_impl->primed || m_impl->faulted || !m_impl->owner->armed) return 0;
        const auto &env = m_impl->owner->package.environment;
        return env.state == resource || env.emit == resource ? m_impl->texture(resource, {}) : 0;
    }

    bool Instance::render(const Source &source, const Target &target, bool advance_simulation,
        std::string &error) {
        error.clear();
        if (!m_impl || !m_impl->owner->armed || m_impl->faulted) {
            return fail(error, "no usable shader instance");
        }
        auto &instance = *m_impl;
        const auto &program = *instance.owner;
        if (!advance_simulation && !instance.primed) {
            return fail(error, "read-only shader draw requires a live initialization");
        }
        const Extent extent{target.width, target.height};
        constexpr auto MAX_COORDINATE = std::numeric_limits<GLint>::max();
        if (!extent.bounded() || !program.limits.viewport_fits(extent.width, extent.height) ||
            target.origin_x < 0 || target.origin_y < 0 ||
            std::int64_t{target.origin_x} + extent.width > MAX_COORDINATE ||
            std::int64_t{target.origin_y} + extent.height > MAX_COORDINATE ||
            !std::ranges::all_of(target.domain, [](float f) { return std::isfinite(f); }) ||
            target.domain[2] <= 0 || target.domain[3] <= 0) {
            return fail(error, "invalid borrowed shader target dimensions or domain");
        }
        if (target.composite &&
            (!program.direct() || target.clear_before_render ||
                !std::ranges::all_of(target.clips, [&](const Clip &clip) {
                    return clip.x >= target.origin_x && clip.y >= target.origin_y &&
                        clip.width > 0 && clip.height > 0 &&
                        std::int64_t{clip.x} + clip.width <= std::int64_t{target.origin_x} + extent.width &&
                        std::int64_t{clip.y} + clip.height <= std::int64_t{target.origin_y} + extent.height;
                }))) {
            return fail(error, "incompatible in-place shader presentation");
        }
        if (source.texture == 0 || source.width == 0 || source.height == 0 ||
            source.width > static_cast<unsigned>(program.limits.texture) ||
            source.height > static_cast<unsigned>(program.limits.texture) ||
            source.texture == target.texture || instance.owns_texture(source.texture) ||
            (target.framebuffer == 0 && target.texture != 0) || !valid_source_uv(source.uv) ||
            !glIsTexture(source.texture) || (target.framebuffer != 0 && !glIsFramebuffer(target.framebuffer))) {
            return fail(error, "invalid or aliased borrowed shader texture/framebuffer");
        }
        if (program.uses_destination &&
            (!source.destination_texture || source.destination_texture == target.texture ||
                instance.owns_texture(source.destination_texture) ||
                !valid_source_uv(source.destination_uv) || !glIsTexture(source.destination_texture))) {
            return fail(error, "invalid or aliased borrowed destination texture");
        }
        if (program.package.environment.used() && source.environment.interactions.size() > MAX_INTERACTIONS)
            return fail(error, "environment interaction batch exceeds the per-step limit");
        if (!gl_ok(error, "entering native shader draw")) {
            return false;
        }
        const auto current = integer(GL_CURRENT_PROGRAM);
        if (!can_use_program(current, error)) {
            return false;
        }
        const bool use_feedback = program.has_feedback && advance_simulation &&
                                  (!instance.primed || instance.pending);
        for (const GLenum type : {GL_ANY_SAMPLES_PASSED, GL_ANY_SAMPLES_PASSED_CONSERVATIVE,
                 GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN}) {
            if (type == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN && !use_feedback) {
                continue;
            }
            GLint query = 0;
            glGetQueryiv(type, GL_CURRENT_QUERY, &query);
            if (query != 0) {
                return fail(error, "native draw would contaminate a caller visibility/feedback query");
            }
        }
        const DrawState guard(current, program.sampler_count, program.block_count, use_feedback,
            program.color, program.max_outputs);
        TargetScope scoped_target(target, instance.saved_draw_buffers);
        scoped_target.select();
        if (!instance.validate_target(source, target, error)) {
            return false;
        }
        if (program.source_sampler != 0 || (program.package.environment.used() && program.sampler_count)) {
            // A non-2D texture can pass glIsTexture; catch that before any simulation work.
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, source.texture);
            if (program.uses_destination) glBindTexture(GL_TEXTURE_2D, source.destination_texture);
            if (program.package.environment.used()) {
                for (auto texture : {source.environment.original, source.environment.processed,
                         source.environment.state, source.environment.disturbances}) {
                    if (!texture) continue;
                    if (instance.owns_texture(texture) || !glIsTexture(texture))
                        return fail(error, "invalid or aliased borrowed environmental image");
                    glBindTexture(GL_TEXTURE_2D, texture);
                }
            }
        }
        if (!gl_ok(error, "validating borrowed shader resources")) {
            return false;
        }
        unsigned active_blocks = 0;
        bool clear_final = target.clear_before_render;
        if (advance_simulation) {
            if (!instance.primed &&
                !instance.phase(Phase::Initialize, instance.pending_frame, source, target,
                    active_blocks, clear_final, error)) {
                return false;
            }
            if (instance.pending &&
                !instance.phase(Phase::Update, instance.pending_frame, source, target,
                    active_blocks, clear_final, error)) {
                return false;
            }
            if (instance.pending || !instance.primed) {
                instance.committed_frame = instance.pending_frame;
                instance.pending = false;
                instance.primed = true;
            }
        }
        return instance.phase(Phase::Render, instance.committed_frame, source, target,
            active_blocks, clear_final, error);
    }

    void Instance::release() noexcept { m_impl.reset(); }

    void Instance::abandon() noexcept {
        if (m_impl) {
            m_impl->armed = false;
            if (m_impl->owner.use_count() == 1) {
                m_impl->owner->armed = false;
            }
            m_impl.reset();
        }
    }

    const Statistics &Instance::statistics() const noexcept { return m_impl ? m_impl->stats : EMPTY_STATS; }
}  // namespace hyprimate::shader
