#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace hyprimate::workspace {
    // Compositor callbacks are serialized on the compositor thread. A scope pins
    // the owner and keeps every hook installed until the outermost callback exits.
    class WorkspaceHookLifetime {
      public:
        template <class Owner>
        class Scope {
          public:
            explicit Scope(Owner *owner)
                : m_owner(owner ? owner->shared_from_this() : nullptr),
                  m_lifetime(owner ? &owner->m_hook_lifetime : nullptr) {
                if (m_lifetime) {
                    ++m_lifetime->m_depth;
                }
            }
            ~Scope() {
                if (m_lifetime && --m_lifetime->m_depth == 0) {
                    m_lifetime->finish();
                }
            }
            Scope(const Scope &) = delete;
            Scope &operator=(const Scope &) = delete;
            Scope(Scope &&) = delete;
            Scope &operator=(Scope &&) = delete;

          private:
            std::shared_ptr<Owner> m_owner;
            WorkspaceHookLifetime *m_lifetime;
        };
        template <auto Shutdown, class Owner>
        void request(Owner &owner) {
            if (m_requested) {
                return;
            }
            m_requested = true;
            m_owner = &owner;
            m_shutdown = [](void *value) { (static_cast<Owner *>(value)->*Shutdown)(); };
            if (m_depth == 0) {
                finish();
            }
        }

        [[nodiscard]] bool busy() const {
            return m_depth != 0 || m_running;
        }
        void reset() {
            m_requested = false;
            m_owner = nullptr;
            m_shutdown = {};
        }

      private:
        void finish() {
            if (m_shutdown) {
                auto shutdown = std::exchange(m_shutdown, {});
                m_running = true;
                shutdown(m_owner);
                m_running = false;
            }
        }
        std::size_t m_depth = 0;
        bool m_requested = false;
        bool m_running = false;
        void *m_owner = nullptr;
        void (*m_shutdown)(void *) = nullptr;
    };
}  // namespace hyprimate::workspace
