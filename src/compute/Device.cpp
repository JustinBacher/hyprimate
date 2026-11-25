#include "DevicePrivate.hpp"
#include "KernelSource.hpp"
#include "gl/ShaderCompile.hpp"

#include <algorithm>
#include <type_traits>

namespace hyprimate::compute {
    using namespace detail;
    namespace {
        std::string gl_string(GLenum key) {
            const auto* value = glGetString(key);
            return value ? reinterpret_cast<const char*>(value) : "";
        }
        bool has(std::span<const std::string_view> exts, std::string_view name) {
            return std::ranges::find(exts, name) != exts.end();
        }
        unsigned format_index(Type type) { return (type.scalar == Element::Int ? 0 : 2) + (type.lanes > 1); }
        std::string candidate_limit(const StageLimits& limit, const KernelDesc& desc) {
            if (!limit.available) return limit.reason;
            if (desc.inputs.size() > limit.inputs) return "input count exceeds stage sampler limit";
            if (desc.outputs.size() > limit.outputs) return "output count exceeds writable slots";
            const auto components = 4 + desc.inputs.size() * 4;
            if (components > limit.parameter_components) return "input metadata exceeds stage uniform limit";
            auto remaining = limit.parameter_components - components;
            for (const auto& slot : desc.parameters) {
                if (slot.type.lanes > remaining) return "parameter components exceed stage uniform limit";
                remaining -= slot.type.lanes;
            }
            return {};
        }
        void set_domain(GLint location, Domain domain, Statistics& statistics) {
            if (location < 0) return;
            glUniform3i(location, static_cast<GLint>(domain.x), static_cast<GLint>(domain.y), static_cast<GLint>(domain.z));
            ++statistics.uniform_updates;
        }
        void set_parameter(GLint location, const ParameterValue& value, Statistics& statistics) {
            if (location < 0) return;
            std::visit([&](const auto& numbers) {
                using Value = typename std::remove_cvref_t<decltype(numbers)>::value_type;
                if constexpr (std::is_same_v<Value, float>) {
                    constexpr std::array uniforms{glUniform1fv, glUniform2fv, glUniform3fv, glUniform4fv};
                    uniforms[value.lanes - 1](location, 1, numbers.data());
                } else if constexpr (std::is_same_v<Value, std::int32_t>) {
                    constexpr std::array uniforms{glUniform1iv, glUniform2iv, glUniform3iv, glUniform4iv};
                    uniforms[value.lanes - 1](location, 1, numbers.data());
                } else {
                    constexpr std::array uniforms{glUniform1uiv, glUniform2uiv, glUniform3uiv, glUniform4uiv};
                    uniforms[value.lanes - 1](location, 1, numbers.data());
                }
            }, value.data);
            ++statistics.uniform_updates;
        }
    }

    Result<void> State::enter(bool inside_batch) const {
        if (!current()) return std::unexpected("compute device requires its live owner context and thread");
        if (stranded_batch.load(std::memory_order_relaxed)) return std::unexpected("compute batch was destroyed outside its owner context");
        if (inside_batch != scope.active) return std::unexpected(inside_batch ? "compute batch is no longer active" : "operation is not allowed inside a compute batch");
        return {};
    }
    Result<void> State::can_change_program() const {
        if (integer(GL_TRANSFORM_FEEDBACK_ACTIVE) && !integer(GL_TRANSFORM_FEEDBACK_PAUSED))
            return std::unexpected("compute cannot run during active unpaused transform feedback");
        constexpr GLenum pipeline_binding = 0x825A, primitives_generated = 0x8C87;
        if ((caps.major > 3 || caps.minor >= 1) && integer(pipeline_binding))
            return std::unexpected("compute cannot borrow a context with a bound program pipeline");
        const auto program = integer(GL_CURRENT_PROGRAM);
        if (program) {
            GLint pending{};
            glGetProgramiv(static_cast<GLuint>(program), GL_DELETE_STATUS, &pending);
            if (pending) return std::unexpected("compute cannot replace a delete-pending caller program");
            GLint linked{};
            glGetProgramiv(static_cast<GLuint>(program), GL_LINK_STATUS, &linked);
            if (!linked) return std::unexpected("compute cannot restore a caller program after failed relink");
        }
        GLint query{};
        glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &query);
        if (query) return std::unexpected("compute cannot run inside an active visibility query");
        glGetQueryiv(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, GL_CURRENT_QUERY, &query);
        if (query) return std::unexpected("compute cannot run inside an active conservative visibility query");
        glGetQueryiv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_CURRENT_QUERY, &query);
        if (query) return std::unexpected("compute cannot contaminate a transform feedback query");
        if (primitives_query) {
            glGetQueryiv(primitives_generated, GL_CURRENT_QUERY, &query);
            if (query) return std::unexpected("compute cannot contaminate a primitive count query");
        }
        return {};
    }
    bool State::rectangles_active() const {
        constexpr GLenum mode = 0x8F13, count = 0x8F15; // EXT_window_rectangles
        constexpr GLint exclusive = 0x8F11;
        return window_rectangles && (integer(mode) != exclusive || integer(count) != 0);
    }
    void State::end_batch() noexcept {
        if (!scope.active) return;
        if (scope.raster_outputs) end_raster(*raster);
        if (scope.native_outputs) native.restore(std::span(scope.images).first(scope.native_outputs));
        for (unsigned i = 0; i < scope.samplers; ++i) {
            if (!scope.units[i].used) continue;
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(scope.units[i].texture));
            glBindSampler(i, static_cast<GLuint>(scope.units[i].sampler));
        }
        if (scope.uses_samplers) glActiveTexture(scope.active_texture);
        glUseProgram(scope.program);
        scope.active = false;
    }
    void State::release_gl() noexcept {
        end_batch();
        for (const auto& kernel : kernels) glDeleteProgram(kernel->name);
        if (raster) release_raster(*raster);
        if (draw_fb) glDeleteFramebuffers(1, &draw_fb);
        if (read_fb) glDeleteFramebuffers(1, &read_fb);
        for (const auto& name : names) glDeleteTextures(1, &name);
    }
    GLbitfield State::image_hazard(const Storage& storage) const {
        return storage.access > image ? IMAGE_BARRIER : 0;
    }
    GLbitfield State::write_hazard(const Storage& storage, GLbitfield consumer) const {
        const auto visible = consumer == FETCH_BARRIER ? fetch : consumer == FRAMEBUFFER_BARRIER ? framebuffer : update;
        return storage.image_write > visible ? consumer : 0;
    }
    void State::synchronize(GLbitfield bits) {
        if (!bits) return;
        native.barrier(bits);
        ++stats.barriers;
        if (bits & FETCH_BARRIER) { fetch = serial; ++stats.fetch_barriers; }
        if (bits & FRAMEBUFFER_BARRIER) { framebuffer = serial; ++stats.framebuffer_barriers; }
        if (bits & UPDATE_BARRIER) { update = serial; ++stats.update_barriers; }
        if (bits & IMAGE_BARRIER) { image = serial; ++stats.image_barriers; }
    }

    Backend Kernel::backend() const noexcept { return program_ ? program_->backend : Backend::Auto; }
    std::string_view Kernel::fallback_reason() const noexcept { return program_ ? std::string_view(program_->fallback) : std::string_view{}; }

    Batch::Batch() noexcept = default;
    Batch::~Batch() { reset(); }
    Batch::Batch(Batch&&) noexcept = default;
    Batch& Batch::operator=(Batch&& other) noexcept {
        if (this != &other) { reset(); state_ = std::move(other.state_); }
        return *this;
    }
    void Batch::reset() noexcept {
        if (const auto state = state_.lock()) {
            if (state->current()) state->end_batch();
            else state->stranded_batch.store(true, std::memory_order_relaxed);
        }
        state_.reset();
    }
    Result<void> Batch::dispatch(const Kernel& kernel, FieldsIn inputs, FieldsOut outputs, std::span<const ParameterValue> parameters) {
        const auto state = state_.lock();
        if (!state) return std::unexpected("expired compute batch");
        if (auto entered = state->enter(true); !entered) return entered;
        auto& scope = state->scope;
        if (scope.faulted) return std::unexpected("compute batch has faulted");
        if (!kernel.program_ || kernel.state_.lock() != state) return std::unexpected("kernel belongs to another device generation");
        const auto& program = *kernel.program_;
        const auto fail = [&](std::string_view reason) -> Result<void> { return std::unexpected("kernel '" + program.desc.name + "': " + std::string(reason)); };
        const bool native = program.backend == Backend::Native;
        if (inputs.size() != program.desc.inputs.size() || outputs.size() != program.desc.outputs.size() || parameters.size() != program.desc.parameters.size())
            return fail("wrong dispatch arity");
        if (inputs.size() > scope.samplers || outputs.size() > (native ? scope.native_outputs : scope.raster_outputs))
            return fail("bindings exceed the declared batch limits");
        const auto valid_field = [&](const Field* field, Type type) {
            return field && field->storage_ && !field->storage_->faulted && field->desc_.type == type && field->state_.lock() == state;
        };
        GLbitfield barriers{};
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            if (!valid_field(inputs[i], program.desc.inputs[i].type)) return fail("invalid input field type, ownership or liveness");
            if (program.inputs[i].sampler >= 0) barriers |= state->write_hazard(*inputs[i]->storage_, FETCH_BARRIER);
            if (program.inputs[i].sampler >= 0 && !scope.units[i].used) return fail("sampler was not declared by the batch");
        }
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto* field = outputs[i];
            if (!valid_field(field, program.desc.outputs[i].type)) return fail("invalid output field type, ownership or liveness");
            if (i && field->desc_.domain != outputs[0]->desc_.domain) return fail("outputs must have an equal logical domain");
            if (std::ranges::find(inputs, field) != inputs.end() || std::find(outputs.begin(), outputs.begin() + i, field) != outputs.begin() + i)
                return fail("read/write alias or duplicate output");
            barriers |= native ? state->image_hazard(*field->storage_) : state->write_hazard(*field->storage_, FRAMEBUFFER_BARRIER);
            scope.outputs[i] = {field->storage_->name, field->storage_->format};
        }
        for (std::size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i].type() != program.desc.parameters[i].type) return fail("wrong parameter type");

        state->synchronize(barriers);
        glUseProgram(program.name);
        set_domain(program.domain, outputs[0]->desc_.domain, state->stats);
        if (program.width >= 0) {
            glUniform1i(program.width, static_cast<GLint>(outputs[0]->layout_.width));
            ++state->stats.uniform_updates;
        }
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto [sampler, size] = program.inputs[i];
            if (sampler >= 0) {
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
                glBindTexture(GL_TEXTURE_2D, inputs[i]->storage_->name);
                glBindSampler(static_cast<GLuint>(i), 0);
                glUniform1i(sampler, static_cast<GLint>(i));
                ++state->stats.uniform_updates;
            }
            set_domain(size, inputs[i]->desc_.domain, state->stats);
        }
        for (std::size_t i = 0; i < parameters.size(); ++i) set_parameter(program.parameters[i], parameters[i], state->stats);
        const auto bindings = std::span(scope.outputs).first(outputs.size());
        const auto layout = outputs[0]->layout_;
        Result<void> submitted = native ? state->native.submit(layout, bindings, state->caps) : submit_raster(*state->raster, layout, bindings);
        if (!submitted) {
            scope.faulted = true;
            for (auto* field : outputs) field->storage_->faulted = true;
            return fail(submitted.error());
        }
        const auto serial = ++state->serial;
        for (std::size_t i = 0; i < inputs.size(); ++i)
            if (program.inputs[i].sampler >= 0) inputs[i]->storage_->access = serial;
        for (auto* field : outputs) {
            field->storage_->access = serial;
            field->storage_->image_write = native ? serial : 0;
        }
        ++(native ? state->stats.native_dispatches : state->stats.slibs_dispatches);
        return {};
    }

    Device::~Device() {
        if (state_ && state_->current()) (void)release();
        else abandon();
    }
    Device::Device(Device&&) noexcept = default;
    Device& Device::operator=(Device&& other) noexcept {
        if (this != &other) {
            if (state_ && state_->current()) (void)release();
            else abandon();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    Result<void> Device::initialize(GlResolver resolver, Backend preference) {
        if (state_) return std::unexpected("release or abandon the current device before initializing a new generation");
        if (!resolver.is_current || !resolver.is_current(resolver.owner)) return std::unexpected("compute requires an owner-current context predicate");
        if (preference != Backend::Auto && preference != Backend::Native && preference != Backend::Slibs)
            return std::unexpected("invalid compute backend preference");
        auto state = std::make_shared<State>();
        state->resolver = resolver;
        state->thread = std::this_thread::get_id();
        state->preference = preference;
        auto& caps = state->caps;
        caps.version = gl_string(GL_VERSION);
        if (!caps.version.starts_with("OpenGL ES ") || caps.version.size() < 11 || caps.version[10] < '3')
            return std::unexpected("portable compute requires OpenGL ES 3.0+; current API: " + caps.version);
        caps.major = integer(GL_MAJOR_VERSION);
        caps.minor = integer(GL_MINOR_VERSION);
        caps.vendor = gl_string(GL_VENDOR);
        caps.renderer = gl_string(GL_RENDERER);
        caps.texture_size = integer(GL_MAX_TEXTURE_SIZE);
        std::array<GLint, 2> viewport{};
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport.data());
        caps.viewport_width = viewport[0];
        caps.viewport_height = viewport[1];
        caps.width = std::min({caps.texture_size, caps.viewport_width, MAX_EXTENT});
        caps.height = std::min({caps.texture_size, caps.viewport_height, MAX_EXTENT});
        caps.combined_samplers = integer(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
        std::vector<std::string_view> extensions;
        const auto extension_count = integer(GL_NUM_EXTENSIONS);
        extensions.reserve(static_cast<std::size_t>(extension_count));
        for (GLint i = 0; i < extension_count; ++i) {
            const auto* ext = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (ext) extensions.emplace_back(reinterpret_cast<const char*>(ext));
        }
        state->primitives_query = caps.major > 3 || caps.minor >= 2 || has(extensions, "GL_EXT_geometry_shader") || has(extensions, "GL_OES_geometry_shader");
        state->window_rectangles = has(extensions, "GL_EXT_window_rectangles");
        state->reverse_pack_rows = has(extensions, "GL_ANGLE_pack_reverse_row_order");
        if (auto safe = state->can_change_program(); !safe) return safe;
        if (auto storage = state->initialize_storage(extensions); !storage) {
            state->release_gl();
            return storage;
        }
        auto raster = configure_raster(resolver, caps, extensions);
        if (raster) { state->raster = std::move(*raster); caps.slibs.available = true; }
        else caps.slibs.reason = std::move(raster.error());
        if (caps.major == 3 && caps.minor == 0) {
            caps.native.reason = "native requires OpenGL ES 3.1 or newer";
        } else if (preference == Backend::Slibs) {
            caps.native.reason = "native probe disabled by strict SLIBS preference";
        } else {
            auto native = state->native.configure(resolver, caps);
            if (native) native = state->native.probe();
            if (!native) caps.native.reason = std::move(native.error());
            else caps.native.available = true;
        }
        const bool native = preference != Backend::Slibs && caps.native.available;
        const bool slibs = preference != Backend::Native && caps.slibs.available;
        if (!native && !slibs) {
            state->release_gl();
            if (preference == Backend::Native) return std::unexpected("native: " + caps.native.reason);
            if (preference == Backend::Slibs) return std::unexpected("SLIBS: " + caps.slibs.reason);
            return std::unexpected("native: " + caps.native.reason + "; SLIBS: " + caps.slibs.reason);
        }
        auto& scope = state->scope;
        scope.units.resize(std::max(native ? caps.native.inputs : 0, slibs ? caps.slibs.inputs : 0));
        scope.images.resize(native ? caps.native.outputs : 0);
        scope.outputs.resize(std::max(native ? caps.native.outputs : 0, slibs ? caps.slibs.outputs : 0));
        state_ = std::move(state);
        return {};
    }
    const Capabilities& Device::capabilities() const noexcept {
        static const Capabilities empty;
        return state_ ? state_->caps : empty;
    }
    Statistics Device::statistics() const noexcept { return state_ ? state_->stats : Statistics{}; }
    Result<void> Device::release() {
        if (!state_) return {};
        if (!state_->current()) return std::unexpected("release requires the owner context and thread; abandon on context loss");
        state_->release_gl();
        abandon();
        return {};
    }
    void Device::abandon() noexcept {
        if (!state_) return;
        state_->alive = false;
        state_.reset();
    }

    Result<Kernel> Device::prepare(const KernelDesc& desc) {
        if (!state_) return std::unexpected("compute device is not initialized");
        auto& state = *state_;
        if (auto entered = state.enter(); !entered) return std::unexpected(std::move(entered.error()));
        const auto find = std::ranges::find_if(state.kernels, [&](const auto& kernel) { return kernel->desc == desc; });
        if (find != state.kernels.end()) {
            Kernel result;
            result.state_ = state_;
            result.program_ = *find;
            return result;
        }
        if (auto common = validate_kernel(desc); !common) return std::unexpected(std::move(common.error()));
        for (const auto* list : {&desc.inputs, &desc.outputs}) {
            for (const auto& slot : *list) {
                if (!state.caps.formats[format_index(slot.type)])
                    return std::unexpected("kernel '" + desc.name + "': field format is not framebuffer/transfer compatible for '" + slot.name + "'");
            }
        }
        const auto try_backend = [&](Backend route) -> Result<GLuint> {
            const auto& stage = route == Backend::Native ? state.caps.native : state.caps.slibs;
            if (auto limit = candidate_limit(stage, desc); !limit.empty()) return std::unexpected(std::move(limit));
            if (route == Backend::Native) return state.native.prepare(desc);
            if (auto formats = state.preflight_outputs(desc.outputs); !formats) return std::unexpected(std::move(formats.error()));
            return prepare_raster(desc);
        };
        std::string error;
        const auto validation = gl::compile_shader(GL_FRAGMENT_SHADER, kernel_source(desc, Backend::Slibs, true), error);
        if (!validation) return std::unexpected("kernel '" + desc.name + "': ES 3.00 portability check: " + error);
        glDeleteShader(validation);

        std::string fallback;
        Backend route = state.preference == Backend::Slibs ? Backend::Slibs : Backend::Native;
        auto compiled = try_backend(route);
        if (!compiled && state.preference == Backend::Auto) {
            fallback = std::move(compiled.error());
            route = Backend::Slibs;
            compiled = try_backend(route);
        }
        if (!compiled) {
            const std::string route_label = route == Backend::Native ? "native: " : "SLIBS: ";
            return std::unexpected("kernel '" + desc.name + "': " + (fallback.empty() ? "" : "native: " + fallback + "; ") + route_label + compiled.error());
        }
        auto program = std::make_shared<Program>();
        program->desc = desc;
        program->name = *compiled;
        program->backend = route;
        program->fallback = std::move(fallback);
        program->domain = glGetUniformLocation(program->name, "hc_domain");
        program->width = glGetUniformLocation(program->name, "hc_width");
        program->inputs.reserve(desc.inputs.size());
        for (const auto& input : desc.inputs)
            program->inputs.push_back({glGetUniformLocation(program->name, sampler_name(input.name).c_str()),
                                       glGetUniformLocation(program->name, size_name(input.name).c_str())});
        program->parameters.reserve(desc.parameters.size());
        for (const auto& parameter : desc.parameters)
            program->parameters.push_back(glGetUniformLocation(program->name, parameter.name.c_str()));
        state.kernels.push_back(program);
        ++state.stats.compiles;
        Kernel result;
        result.state_ = state_;
        result.program_ = std::move(program);
        return result;
    }
    Result<Batch> Device::begin(KernelSet kernels) {
        if (!state_) return std::unexpected("compute device is not initialized");
        auto& state = *state_;
        if (auto entered = state.enter(); !entered) return std::unexpected(std::move(entered.error()));
        if (kernels.empty()) return std::unexpected("compute batch requires at least one prepared kernel");
        auto& scope = state.scope;
        scope.samplers = scope.native_outputs = scope.raster_outputs = 0;
        scope.uses_samplers = false;
        for (auto& unit : scope.units) unit.used = false;
        for (const Kernel& kernel : kernels) {
            if (!kernel.program_ || kernel.state_.lock() != state_) return std::unexpected("batch kernel belongs to another device generation");
            const auto& program = *kernel.program_;
            scope.samplers = std::max(scope.samplers, static_cast<unsigned>(program.inputs.size()));
            for (std::size_t i = 0; i < program.inputs.size(); ++i) {
                if (program.inputs[i].sampler < 0) continue;
                scope.units[i].used = true;
                scope.uses_samplers = true;
            }
            auto& count = program.backend == Backend::Native ? scope.native_outputs : scope.raster_outputs;
            count = std::max(count, static_cast<unsigned>(program.desc.outputs.size()));
        }
        if (auto safe = state.can_change_program(); !safe) return std::unexpected(std::move(safe.error()));
        if (scope.raster_outputs && state.rectangles_active()) return std::unexpected("compute raster pass cannot borrow active EXT_window_rectangles state");
        state.drain_retirements();
        scope.program = integer(GL_CURRENT_PROGRAM);
        if (scope.uses_samplers) {
            scope.active_texture = integer(GL_ACTIVE_TEXTURE);
            for (unsigned i = 0; i < scope.samplers; ++i) {
                if (!scope.units[i].used) continue;
                glActiveTexture(GL_TEXTURE0 + i);
                scope.units[i] = {integer(GL_TEXTURE_BINDING_2D), integer(GL_SAMPLER_BINDING), true};
            }
        }
        if (scope.native_outputs) state.native.capture(std::span(scope.images).first(scope.native_outputs));
        if (scope.raster_outputs) begin_raster(*state.raster, scope.raster_outputs);
        scope.active = true;
        scope.faulted = false;
        Batch result;
        result.state_ = state_;
        return result;
    }
}
