#include "DevicePrivate.hpp"
#include "gl/Storage.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace hyprimate::compute {
    using namespace detail;
    namespace {
        constexpr std::size_t MAX_IDLE_BYTES = 16 * 1024 * 1024;
        constexpr unsigned STAGING_WORDS = 256 * 1024;
        constexpr std::array FORMATS{GL_R32I, GL_RGBA32I, GL_R32UI, GL_RGBA32UI};
        constexpr GLenum COLOR_ATTACHMENT = GL_COLOR_ATTACHMENT0;

        bool signed_format(GLenum format) { return format == GL_R32I || format == GL_RGBA32I; }
        GLenum transfer_type(GLenum format) { return signed_format(format) ? GL_INT : GL_UNSIGNED_INT; }

        struct TextureBinding {
            GLuint previous = static_cast<GLuint>(integer(GL_TEXTURE_BINDING_2D));
            ~TextureBinding() { glBindTexture(GL_TEXTURE_2D, previous); }
        };
        struct Attachment {
            GLenum target;
            GLuint previous;
            Attachment(GLenum target, GLuint fbo, GLuint texture)
                : target(target), previous(static_cast<GLuint>(integer(target == GL_READ_FRAMEBUFFER ? GL_READ_FRAMEBUFFER_BINDING : GL_DRAW_FRAMEBUFFER_BINDING))) {
                glBindFramebuffer(target, fbo);
                glFramebufferTexture2D(target, COLOR_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
                if (target == GL_READ_FRAMEBUFFER) glReadBuffer(COLOR_ATTACHMENT);
                else glDrawBuffers(1, &COLOR_ATTACHMENT);
            }
            ~Attachment() {
                glFramebufferTexture2D(target, COLOR_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
                glBindFramebuffer(target, previous);
            }
            [[nodiscard]] bool complete() const { return glCheckFramebufferStatus(target) == GL_FRAMEBUFFER_COMPLETE; }
        };
        struct ScissorState {
            State::SetIndexed enable;
            GLboolean enabled;
            explicit ScissorState(const State& state)
                : enable(state.enable_scissor),
                  enabled(state.scissor_enabled ? state.scissor_enabled(GL_SCISSOR_TEST, 0) : glIsEnabled(GL_SCISSOR_TEST)) {
                if (!enabled) return;
                if (state.disable_scissor) state.disable_scissor(GL_SCISSOR_TEST, 0);
                else glDisable(GL_SCISSOR_TEST);
            }
            ~ScissorState() {
                if (!enabled) return;
                if (enable) enable(GL_SCISSOR_TEST, 0);
                else glEnable(GL_SCISSOR_TEST);
            }
        };
        struct ClearState {
            State::ColorMask mask;
            std::array<GLboolean, 4> saved_mask{};
            ScissorState scissor;
            GLboolean discard = glIsEnabled(GL_RASTERIZER_DISCARD);
            explicit ClearState(const State& state) : mask(state.indexed_mask), scissor(state) {
                if (discard) glDisable(GL_RASTERIZER_DISCARD);
                if (mask) {
                    std::array<GLint, 4> flags{};
                    glGetIntegeri_v(GL_COLOR_WRITEMASK, 0, flags.data());
                    std::ranges::transform(flags, saved_mask.begin(), [](GLint flag) { return static_cast<GLboolean>(flag != 0); });
                    mask(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                } else {
                    glGetBooleanv(GL_COLOR_WRITEMASK, saved_mask.data());
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                }
            }
            ~ClearState() {
                const auto [r, g, b, a] = saved_mask;
                if (mask) mask(0, r, g, b, a);
                else glColorMask(r, g, b, a);
                if (discard) glEnable(GL_RASTERIZER_DISCARD);
            }
        };
        struct PackState {
            static constexpr std::array<GLenum, 4> FIELDS{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS};
            GLint pbo = integer(GL_PIXEL_PACK_BUFFER_BINDING);
            std::array<GLint, FIELDS.size()> saved{};
            static constexpr GLenum REVERSE_ROWS = 0x93A4; // ANGLE_pack_reverse_row_order
            GLint reverse = 0;
            bool restore_reverse;
            explicit PackState(bool reverse_rows) : restore_reverse(reverse_rows) {
                if (restore_reverse) {
                    reverse = integer(REVERSE_ROWS);
                    glPixelStorei(REVERSE_ROWS, GL_FALSE);
                }
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                for (std::size_t i = 0; i < FIELDS.size(); ++i) {
                    saved[i] = integer(FIELDS[i]);
                    glPixelStorei(FIELDS[i], i == 0 ? 1 : 0);
                }
            }
            ~PackState() {
                for (std::size_t i = 0; i < FIELDS.size(); ++i) glPixelStorei(FIELDS[i], saved[i]);
                if (restore_reverse) glPixelStorei(REVERSE_ROWS, reverse);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pbo));
            }
        };
        Result<GLuint> texture_2d(GLenum format, unsigned width, unsigned height) {
            TextureBinding save;
            GLuint name{};
            glGenTextures(1, &name);
            if (!name) return std::unexpected("cannot generate a compute field texture");
            glBindTexture(GL_TEXTURE_2D, name);
            if (!gl::allocate_texture_2d(format, static_cast<GLsizei>(width), static_cast<GLsizei>(height))) {
                glDeleteTextures(1, &name);
                return std::unexpected("cannot allocate immutable compute texture storage");
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return name;
        }
        // Issue rectangular strips over the x-fastest logical prefix; padding never becomes a CPU element.
        template<class Transfer> void strips(Layout layout, unsigned rows_per_strip, Transfer transfer) {
            const auto whole_rows = layout.elements / layout.width;
            for (unsigned row = 0; row < whole_rows;) {
                const auto rows = std::min(whole_rows - row, rows_per_strip);
                transfer(row, layout.width, rows);
                row += rows;
            }
            if (const auto tail = layout.elements % layout.width) transfer(whole_rows, tail, 1);
        }
    }

    State::~State() {
        for (auto* storage = deferred.load(std::memory_order_relaxed); storage;) {
            const auto next = storage->deferred_next;
            delete storage;
            storage = next;
        }
    }
    void State::defer_retirement(std::unique_ptr<Storage> owned) noexcept {
        auto* storage = owned.release();
        auto* head = deferred.load(std::memory_order_relaxed);
        do {
            storage->deferred_next = head;
        } while (!deferred.compare_exchange_weak(head, storage, std::memory_order_release, std::memory_order_relaxed));
    }
    void State::drain_retirements() noexcept {
        for (auto* storage = deferred.exchange(nullptr, std::memory_order_acquire); storage;) {
            auto* next = storage->deferred_next;
            storage->deferred_next = nullptr;
            recycle(std::unique_ptr<Storage>(storage));
            storage = next;
        }
    }

    Result<void> State::initialize_storage(std::span<const std::string_view> extensions) {
        const bool core = caps.major > 3 || caps.minor >= 2;
        const bool ext = std::ranges::find(extensions, std::string_view("GL_EXT_draw_buffers_indexed")) != extensions.end();
        const bool oes = std::ranges::find(extensions, std::string_view("GL_OES_draw_buffers_indexed")) != extensions.end();
        if (core || ext || oes) {
            if (resolver.resolve) {
                if (core) indexed_mask = reinterpret_cast<ColorMask>(resolver.resolve("glColorMaski", resolver.owner));
                if (!indexed_mask && ext) indexed_mask = reinterpret_cast<ColorMask>(resolver.resolve("glColorMaskiEXT", resolver.owner));
                if (!indexed_mask && oes) indexed_mask = reinterpret_cast<ColorMask>(resolver.resolve("glColorMaskiOES", resolver.owner));
            }
            if (!indexed_mask) return std::unexpected("driver advertises indexed color masks but the resolver cannot preserve them");
        }
        const bool viewport_oes = std::ranges::find(extensions, std::string_view("GL_OES_viewport_array")) != extensions.end();
        const bool viewport_nv = std::ranges::find(extensions, std::string_view("GL_NV_viewport_array")) != extensions.end();
        if (viewport_oes || viewport_nv) {
            const auto load = [&](const char* test, const char* enable, const char* disable) {
                if (!resolver.resolve) return false;
                scissor_enabled = reinterpret_cast<TestIndexed>(resolver.resolve(test, resolver.owner));
                enable_scissor = reinterpret_cast<SetIndexed>(resolver.resolve(enable, resolver.owner));
                disable_scissor = reinterpret_cast<SetIndexed>(resolver.resolve(disable, resolver.owner));
                return scissor_enabled && enable_scissor && disable_scissor;
            };
            const bool loaded = (viewport_oes && load("glIsEnablediOES", "glEnableiOES", "glDisableiOES")) ||
                                (viewport_nv && load("glIsEnablediNV", "glEnableiNV", "glDisableiNV"));
            if (!loaded) return std::unexpected("driver advertises indexed viewports but the resolver cannot preserve scissor indices");
        }
        glGenFramebuffers(1, &draw_fb);
        glGenFramebuffers(1, &read_fb);
        if (!draw_fb || !read_fb || !caps.width || !caps.height) return std::unexpected("cannot create private compute transfer framebuffers");
        for (std::size_t i = 0; i < FORMATS.size(); ++i) {
            const auto tex = texture_2d(FORMATS[i], 1, 1);
            if (!tex) continue;
            {
                const Attachment read(GL_READ_FRAMEBUFFER, read_fb, *tex);
                const Attachment draw(GL_DRAW_FRAMEBUFFER, draw_fb, *tex);
                caps.formats[i] = read.complete() && draw.complete();
            }
            glDeleteTextures(1, &*tex);
        }
        if (std::ranges::none_of(caps.formats, [](bool supported) { return supported; }))
            return std::unexpected("no portable integer field storage format is framebuffer complete");
        return {};
    }
    Result<void> State::preflight_outputs(std::span<const Slot> outputs) {
        if (outputs.size() == 1) return {}; // Every individual format was checked at initialization.
        const auto slot_format = [](const Slot& slot) { return detail::format(slot.type); };
        for (const auto& known : raster_formats)
            if (std::ranges::equal(known, outputs, {}, std::identity{}, slot_format)) return {};

        struct Probe {
            GLuint previous = static_cast<GLuint>(integer(GL_DRAW_FRAMEBUFFER_BINDING));
            std::vector<GLuint> names;
            ~Probe() {
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (names[i]) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, COLOR_ATTACHMENT + static_cast<GLenum>(i), GL_TEXTURE_2D, 0, 0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous);
                glDeleteTextures(static_cast<GLsizei>(names.size()), names.data());
            }
        } probe{.names = std::vector<GLuint>(outputs.size())};
        std::vector<GLenum> attachments(outputs.size());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            auto texture = texture_2d(slot_format(outputs[i]), 1, 1);
            if (!texture) return std::unexpected("SLIBS MRT probe: " + texture.error());
            probe.names[i] = *texture;
            attachments[i] = COLOR_ATTACHMENT + static_cast<GLenum>(i);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachments[i], GL_TEXTURE_2D, *texture, 0);
        }
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return std::unexpected("SLIBS MRT probe: output format tuple is not framebuffer complete");
        std::ranges::transform(outputs, attachments.begin(), slot_format);
        raster_formats.push_back(std::move(attachments));
        return {};
    }

    Result<std::unique_ptr<Storage>> State::acquire(Type type, Layout layout) {
        const auto sized_format = detail::format(type);
        const auto index = std::ranges::find(FORMATS, sized_format) - FORMATS.begin();
        if (!caps.formats[index]) return std::unexpected("requested compute storage format is not framebuffer renderable");
        if (rectangles_active()) return std::unexpected("cannot clear a compute field with active EXT_window_rectangles");
        drain_retirements();
        std::unique_ptr<Storage> storage;
        const auto match = std::ranges::find_if(idle, [&](const auto& candidate) {
            return candidate->format == sized_format && candidate->width == layout.width && candidate->height == layout.height;
        });
        if (match != idle.end()) {
            storage = std::move(*match);
            idle.erase(match);
            stats.idle_bytes -= storage->bytes;
            ++stats.pool_hits;
        } else {
            storage = std::make_unique<Storage>();
            const auto tex = texture_2d(sized_format, layout.width, layout.height);
            if (!tex) return std::unexpected(tex.error());
            storage->name = *tex;
            storage->format = sized_format;
            storage->width = layout.width;
            storage->height = layout.height;
            storage->bytes = layout.storage_bytes;
            names.insert(*tex);
            ++stats.allocations;
        }
        if (auto cleared = scrub(*storage); !cleared) {
            const auto name = storage->name;
            glDeleteTextures(1, &name);
            names.erase(name);
            return std::unexpected(std::move(cleared.error()));
        }
        stats.live_bytes += storage->bytes;
        return storage;
    }
    void State::recycle(std::unique_ptr<Storage> storage) noexcept {
        stats.live_bytes -= storage->bytes;
        const auto drop = [&](GLuint name) {
            glDeleteTextures(1, &name);
            names.erase(name);
        };
        if (storage->faulted || storage->bytes > MAX_IDLE_BYTES) {
            drop(storage->name);
            return;
        }
        while (stats.idle_bytes > MAX_IDLE_BYTES - storage->bytes) {
            const auto& oldest = idle.back();
            stats.idle_bytes -= oldest->bytes;
            drop(oldest->name);
            idle.pop_back();
        }
        const auto name = storage->name;
        const auto bytes = storage->bytes;
        try {
            idle.push_front(std::move(storage));
            stats.idle_bytes += bytes;
        } catch (const std::bad_alloc&) {
            drop(name);
        }
    }
    Result<void> State::scrub(Storage& storage) {
        const Attachment target(GL_DRAW_FRAMEBUFFER, draw_fb, storage.name);
        if (!target.complete()) return std::unexpected("compute clear framebuffer is not complete");
        const ClearState clear(*this);
        synchronize(write_hazard(storage, FRAMEBUFFER_BARRIER));
        if (signed_format(storage.format)) {
            constexpr std::array<GLint, 4> zero{};
            glClearBufferiv(GL_COLOR, 0, zero.data());
        } else {
            constexpr std::array<GLuint, 4> zero{};
            glClearBufferuiv(GL_COLOR, 0, zero.data());
        }
        storage.access = ++serial;
        storage.image_write = 0;
        return {};
    }
    Result<void> State::upload(Storage& storage, FieldDesc desc, Layout layout, std::span<const std::byte> data) {
        if (data.size() != layout.logical_bytes) return std::unexpected("upload size does not match field's logical bytes");
        const bool pad = desc.type.lanes == 2 || desc.type.lanes == 3;
        if (pad && !staging) staging = std::make_unique_for_overwrite<std::uint32_t[]>(STAGING_WORDS);
        const TextureBinding saved;
        const gl::ClientPixelUnpack unpack;
        glBindTexture(GL_TEXTURE_2D, storage.name);
        synchronize(write_hazard(storage, UPDATE_BARRIER));
        const auto channels = desc.type.lanes;
        const auto pixel_format = channels == 1 ? GL_RED_INTEGER : GL_RGBA_INTEGER;
        strips(layout, pad ? STAGING_WORDS / (4 * layout.width) : layout.height, [&](unsigned y, unsigned w, unsigned h) {
            const auto* logical = data.data() + std::size_t{y} * layout.width * channels * 4;
            const void* source = logical;
            if (pad) {
                for (unsigned i = 0; i < w * h; ++i) {
                    auto* physical = staging.get() + 4 * i;
                    std::memcpy(physical, logical + i * channels * 4, channels * 4);
                    std::fill(physical + channels, physical + 4, 0);
                }
                source = staging.get();
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h), pixel_format, transfer_type(storage.format), source);
        });
        storage.access = ++serial;
        storage.image_write = 0;
        stats.upload_bytes += layout.logical_bytes;
        return {};
    }
    Result<void> State::readback(Storage& storage, FieldDesc desc, Layout layout, std::span<std::byte> data) {
        if (data.size() != layout.logical_bytes) return std::unexpected("readback size does not match field's logical bytes");
        const Attachment source(GL_READ_FRAMEBUFFER, read_fb, storage.name);
        if (!source.complete()) return std::unexpected("compute read framebuffer is not complete");
        const PackState pack(reverse_pack_rows);
        const auto channels = desc.type.lanes;
        if (channels != 4 && !staging) staging = std::make_unique_for_overwrite<std::uint32_t[]>(STAGING_WORDS);
        synchronize(write_hazard(storage, FRAMEBUFFER_BARRIER));
        strips(layout, channels == 4 ? layout.height : STAGING_WORDS / (4 * layout.width), [&](unsigned y, unsigned w, unsigned h) {
            auto* logical = data.data() + std::size_t{y} * layout.width * channels * 4;
            void* target = channels == 4 ? static_cast<void*>(logical) : staging.get();
            glReadPixels(0, static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA_INTEGER, transfer_type(storage.format), target);
            if (channels != 4) {
                for (unsigned i = 0; i < w * h; ++i)
                    std::memcpy(logical + i * channels * 4, staging.get() + i * 4, channels * 4);
            }
        });
        storage.access = ++serial;
        stats.readback_bytes += layout.logical_bytes;
        return {};
    }
    Result<void> State::copy(Storage& source, Storage& target) {
        if (rectangles_active()) return std::unexpected("cannot blit a compute field with active EXT_window_rectangles");
        const Attachment read(GL_READ_FRAMEBUFFER, read_fb, source.name);
        const Attachment draw(GL_DRAW_FRAMEBUFFER, draw_fb, target.name);
        if (!read.complete() || !draw.complete()) return std::unexpected("compute blit framebuffer is not complete");
        const ScissorState scissor(*this);
        synchronize(write_hazard(source, FRAMEBUFFER_BARRIER) | write_hazard(target, FRAMEBUFFER_BARRIER));
        const GLint w = static_cast<GLint>(source.width), h = static_cast<GLint>(source.height);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        source.access = target.access = ++serial;
        target.image_write = 0;
        stats.copy_bytes += source.bytes;
        return {};
    }

    Field::Field() noexcept = default;
    Field::~Field() { reset(); }
    Field::Field(Field&&) noexcept = default;
    Field& Field::operator=(Field&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::move(other.state_);
            storage_ = std::move(other.storage_);
            desc_ = other.desc_;
            layout_ = other.layout_;
        }
        return *this;
    }
    void Field::reset() noexcept {
        if (!storage_) return;
        if (const auto state = state_.lock()) {
            if (state->current() && !state->scope.active) state->recycle(std::move(storage_));
            else state->defer_retirement(std::move(storage_));
        }
        storage_.reset();
        state_.reset();
    }
    void Field::abandon() noexcept {
        if (!storage_) return;
        if (const auto state = state_.lock()) state->defer_retirement(std::move(storage_));
        storage_.reset();
        state_.reset();
    }

    Result<Field> Device::allocate(FieldDesc desc) {
        if (!state_) return std::unexpected("compute device is not initialized");
        auto& state = *state_;
        if (auto entered = state.enter(); !entered) return std::unexpected(std::move(entered.error()));
        const auto layout = field_layout(desc, state.caps.width, state.caps.height, state.caps.max_field_bytes);
        if (!layout) return std::unexpected(layout.error());
        auto storage = state.acquire(desc.type, *layout);
        if (!storage) return std::unexpected(std::move(storage.error()));
        Field field;
        field.state_ = state_;
        field.storage_ = std::move(*storage);
        field.layout_ = *layout;
        field.desc_ = desc;
        return field;
    }
    Result<void> Device::upload(Field& field, std::span<const std::byte> data) {
        if (!state_) return std::unexpected("compute device is not initialized");
        if (auto entered = state_->enter(); !entered) return entered;
        if (!field.storage_ || field.storage_->faulted || field.state_.lock() != state_)
            return std::unexpected("upload field is faulted, expired or belongs to another device");
        return state_->upload(*field.storage_, field.desc_, field.layout_, data);
    }
    Result<void> Device::readback(const Field& field, std::span<std::byte> data) {
        if (!state_) return std::unexpected("compute device is not initialized");
        if (auto entered = state_->enter(); !entered) return entered;
        if (!field.storage_ || field.storage_->faulted || field.state_.lock() != state_)
            return std::unexpected("readback field is faulted, expired or belongs to another device");
        return state_->readback(*field.storage_, field.desc_, field.layout_, data);
    }
    Result<void> Device::copy(const Field& source, Field& target) {
        if (!state_) return std::unexpected("compute device is not initialized");
        if (auto entered = state_->enter(); !entered) return entered;
        if (!source.storage_ || !target.storage_ || source.storage_->faulted || target.storage_->faulted || source.state_.lock() != state_ || target.state_.lock() != state_)
            return std::unexpected("copy fields are faulted, expired or belong to another device");
        if (&source == &target || source.desc_ != target.desc_) return std::unexpected("copy requires distinct fields of equal type and logical domain");
        return state_->copy(*source.storage_, *target.storage_);
    }
    Result<SampledField> Device::sample(const Field& field) {
        if (!state_) return std::unexpected("compute device is not initialized");
        if (auto entered = state_->enter(); !entered) return std::unexpected(std::move(entered.error()));
        if (!field.storage_ || field.storage_->faulted || field.state_.lock() != state_)
            return std::unexpected("sampled field is faulted, expired or belongs to another device");
        auto& state = *state_;
        state.synchronize(state.write_hazard(*field.storage_, FETCH_BARRIER));
        field.storage_->access = ++state.serial;
        ++state.stats.samples;
        return SampledField{field.storage_->name, field.layout_.width, field.layout_.height, field.desc_};
    }
}
