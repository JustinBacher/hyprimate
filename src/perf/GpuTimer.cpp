#include "GpuTimer.hpp"

#include "gl/EglContext.hpp"

#include <array>
#include <cstring>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

namespace hyprimate::perf {
    namespace {
        constexpr unsigned CONTEXTS = 4;
        constexpr unsigned QUERIES = 128;
        enum class Stage { Free, Active, Pending };
        struct Slot {
            GLuint query = 0;
            Key key{};
            std::uint64_t session = 0;
            std::uint64_t token = 0;
            Stage stage = Stage::Free;
            bool invalid = false;
            bool abandoned = false;
        };
        struct Context {
            gl::EglContextId owner;
            // A lifetime witness. Unlike numeric query names, sync handles
            // do not routinely collide between independent GL object namespaces.
            GLsync witness = nullptr;
            bool discovered = false;
            bool supported = false;
            PFNGLGENQUERIESEXTPROC gen = nullptr;
            PFNGLDELETEQUERIESEXTPROC erase = nullptr;
            PFNGLBEGINQUERYEXTPROC begin = nullptr;
            PFNGLENDQUERYEXTPROC end = nullptr;
            PFNGLGETQUERYIVEXTPROC target = nullptr;
            PFNGLGETQUERYOBJECTUIVEXTPROC available = nullptr;
            PFNGLGETQUERYOBJECTUI64VEXTPROC result = nullptr;
            std::array<Slot, QUERIES> slots{};
        };
        std::array<Context, CONTEXTS> contexts;
        std::uint64_t next_token = 0;

        bool extension(const char *list, const char *name) noexcept {
            if (!list) return false;
            const auto length = std::strlen(name);
            for (const char *p = list; (p = std::strstr(p, name)); p += length)
                if ((p == list || p[-1] == ' ') && (p[length] == ' ' || !p[length]))
                    return true;
            return false;
        }

        void forget(Context &context) noexcept {
            for (const auto &slot : context.slots)
                if (slot.stage != Stage::Free) gpu_dropped(slot.session);
            // Never delete names after losing their context lifetime witness.
            context = {};
        }

        Context *current(std::uint64_t session) noexcept {
            const gl::EglContextId id{eglGetCurrentDisplay(), eglGetCurrentContext()};
            if (!id.valid() || eglQueryAPI() != EGL_OPENGL_ES_API) {
                gpu_status(session, GpuStatus::NoContext);
                return nullptr;
            }
            Context *empty = nullptr;
            for (auto &context : contexts) {
                if (context.owner == id) {
                    if (!context.witness || glIsSync(context.witness)) return &context;
                    forget(context);
                }
                if (!context.owner.valid() && !empty) empty = &context;
            }
            if (!empty) {
                gpu_dropped(session);
                return nullptr;
            }
            empty->owner = id;
            return empty;
        }

        bool discover(Context &context, std::uint64_t session) noexcept {
            if (!context.discovered) {
                context.discovered = true;
                const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
                const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
                // The compositor is GLES3; the lifetime witness also needs GLES3 sync objects.
                if (version && std::strstr(version, "OpenGL ES 3.") &&
                    extension(extensions, "GL_EXT_disjoint_timer_query")) {
#define LOAD(member, type, name) \
    context.member = reinterpret_cast<type>(eglGetProcAddress(name))
                    LOAD(gen, PFNGLGENQUERIESEXTPROC, "glGenQueriesEXT");
                    LOAD(erase, PFNGLDELETEQUERIESEXTPROC, "glDeleteQueriesEXT");
                    LOAD(begin, PFNGLBEGINQUERYEXTPROC, "glBeginQueryEXT");
                    LOAD(end, PFNGLENDQUERYEXTPROC, "glEndQueryEXT");
                    LOAD(target, PFNGLGETQUERYIVEXTPROC, "glGetQueryivEXT");
                    LOAD(available, PFNGLGETQUERYOBJECTUIVEXTPROC, "glGetQueryObjectuivEXT");
                    LOAD(result, PFNGLGETQUERYOBJECTUI64VEXTPROC, "glGetQueryObjectui64vEXT");
#undef LOAD
                    if (context.gen && context.erase && context.begin && context.end &&
                        context.target && context.available && context.result) {
                        GLint bits = 0;
                        context.target(GL_TIME_ELAPSED_EXT, GL_QUERY_COUNTER_BITS_EXT, &bits);
                        if (bits > 0) {
                            context.witness = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                            context.supported = context.witness != nullptr;
                        }
                    }
                }
            }
            gpu_status(session, context.supported ? GpuStatus::Available : GpuStatus::Unsupported);
            return context.supported;
        }

        bool disjoint(Context &context, std::uint64_t session) noexcept {
            GLint value = 0;
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &value);
            if (!value) return false;
            gpu_status(session, GpuStatus::Disjoint);
            for (auto &slot : context.slots) {
                if (slot.stage == Stage::Active) slot.invalid = true;
                if (slot.stage == Stage::Pending) {
                    gpu_dropped(slot.session);
                    slot.stage = Stage::Free;
                }
            }
            return true;
        }


        void retire_abandoned(Context &context) noexcept {
            GLint active = 0;
            context.target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
            for (auto &slot : context.slots) {
                if (slot.stage != Stage::Active || !slot.abandoned) continue;
                if (static_cast<GLuint>(active) == slot.query) {
                    context.end(GL_TIME_ELAPSED_EXT);
                    active = 0;
                }
                gpu_dropped(slot.session);
                slot.stage = Stage::Free;
            }
        }
        bool foreign_query(Context &context) noexcept {
            GLint active = 0;
            context.target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
            if (!active) return false;
            for (const auto &slot : context.slots)
                if (slot.stage == Stage::Active && slot.query == static_cast<GLuint>(active))
                    return false;
            // A caller may consume the read-to-clear disjoint flag while its query
            // runs. Our outstanding epoch can no longer be certified afterwards.
            for (auto &slot : context.slots) {
                if (slot.stage == Stage::Active) slot.invalid = true;
                if (slot.stage != Stage::Pending) continue;
                gpu_dropped(slot.session);
                slot.stage = Stage::Free;
            }
            return true;
        }

        void poll(Context &context, std::uint64_t session) noexcept {
            if (foreign_query(context)) return;
            if (disjoint(context, session)) return;
            struct Ready { Slot *slot; GLuint64 ns; };
            std::array<Ready, QUERIES> ready{};
            unsigned count = 0;
            for (auto &slot : context.slots) {
                if (slot.stage != Stage::Pending) continue;
                if (slot.session != session || !enabled()) {
                    gpu_dropped(slot.session);
                    slot.stage = Stage::Free;
                    continue;
                }
                GLuint available = GL_FALSE;
                context.available(slot.query, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
                if (!available) continue;
                GLuint64 ns = 0;
                context.result(slot.query, GL_QUERY_RESULT_EXT, &ns);
                ready[count++] = {&slot, ns};
            }
            // A disjoint between the first check and result retrieval invalidates the
            // entire outstanding epoch, including samples that were not yet available.
            if (disjoint(context, session)) return;
            for (unsigned i = 0; i < count; ++i) {
                gpu_sample(ready[i].slot->key, ready[i].slot->session, ready[i].ns);
                ready[i].slot->stage = Stage::Free;
            }
        }
    }  // namespace

    GpuScope::GpuScope(Key key) noexcept {
        if (!enabled()) return;  // No allocation, clock, EGL or GL call on the disabled path.
        const auto session = generation();
        auto *context = current(session);
        if (!context || !discover(*context, session)) return;
        retire_abandoned(*context);
        GLint caller = 0;
        context->target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &caller);
        if (caller) {
            (void)foreign_query(*context);
            gpu_dropped(session);
            return;
        }
        poll(*context, session);
        for (unsigned i = 0; i < QUERIES; ++i) {
            auto &slot = context->slots[i];
            if (slot.stage != Stage::Free) continue;
            if (!slot.query) context->gen(1, &slot.query);
            if (!slot.query) {
                gpu_dropped(session);
                return;
            }
            slot.key = key;
            slot.session = session;
            slot.token = ++next_token;
            if (!slot.token) slot.token = ++next_token;
            slot.invalid = false;
            slot.abandoned = false;
            slot.stage = Stage::Active;
            context->begin(GL_TIME_ELAPSED_EXT, slot.query);
            m_context = static_cast<unsigned>(context - contexts.data());
            m_slot = i;
            m_token = slot.token;
            return;
        }
        gpu_dropped(session);
    }

    GpuScope::~GpuScope() {
        if (!m_token) return;
        auto &context = contexts[m_context];
        auto &slot = context.slots[m_slot];
        if (slot.token != m_token || slot.stage != Stage::Active) return;
        // Do not change bindings or close an unrelated query when code inside a scope
        // switches context. release_gpu can retire the abandoned query in its owner.
        if (!context.owner.current() || !glIsSync(context.witness)) {
            slot.invalid = true;
            slot.abandoned = true;
            return;
        }
        GLint active = 0;
        context.target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
        if (static_cast<GLuint>(active) != slot.query) {
            gpu_dropped(slot.session);
            slot.stage = Stage::Free;
            return;
        }
        context.end(GL_TIME_ELAPSED_EXT);
        if (slot.invalid || !enabled() || slot.session != generation()) {
            gpu_dropped(slot.session);
            slot.stage = Stage::Free;
        } else {
            slot.stage = Stage::Pending;
        }
    }

    void poll_gpu() noexcept {
        // Do not discover contexts merely because a disabled frame hook ran.
        const gl::EglContextId id{eglGetCurrentDisplay(), eglGetCurrentContext()};
        for (auto &context : contexts) {
            if (!context.owner.valid() || context.owner != id || !context.supported) continue;
            if (!glIsSync(context.witness)) {
                forget(context);
                return;
            }
            poll(context, generation());
            retire_abandoned(context);
            return;
        }
    }

    void release_gpu() noexcept {
        for (auto &context : contexts) {
            if (!context.owner.valid()) continue;
            const gl::ScopedEglContext enter(context.owner);
            // Binding may fail temporarily (for example, another thread owns the
            // context). Retain bounded state for a later cleanup attempt. Destroyed
            // contexts can consequently occupy a slot until their owner is current
            // again and its lifetime witness can be checked.
            if (!enter) continue;
            if (context.witness && glIsSync(context.witness)) {
                GLint active = 0;
                context.target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
                for (auto &slot : context.slots) {
                    if (!slot.query) continue;
                    if (static_cast<GLuint>(active) == slot.query) {
                        context.end(GL_TIME_ELAPSED_EXT);
                        active = 0;
                    }
                    context.erase(1, &slot.query);
                }
                glDeleteSync(context.witness);
            }
            forget(context);
        }
    }
}  // namespace hyprimate::perf
