#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>

#include <hyprland/src/helpers/memory/Memory.hpp>

namespace hyprimate::gl {
    // Hyprland native wrappers can unconditionally switch EGL and delete on destruction. Keep
    // one ordinary host reference until all consumers finish, then drop it inside its owner.
    // No plugin deleter or alternate control block escapes to the compositor.
    template<class T, class Owner, class Scope>
    class NativeRetirement {
      public:
        NativeRetirement() = default;
        NativeRetirement(const NativeRetirement &) = delete;
        NativeRetirement &operator=(const NativeRetirement &) = delete;
        void keep(const SP<T> &resource, const Owner &owner) {
            collect(); // Keep up with bursts of replacement allocations between frames.
            const auto buckets = m_entries.bucket_count();
            m_entries.try_emplace(resource.get(), owner, resource);
            if (buckets != m_entries.bucket_count()) m_cursor = m_entries.end();
        }
        bool current(const SP<T> &resource) noexcept {
            const auto it = m_entries.find(resource.get());
            return it != m_entries.end() && it->second.owner.ensure_current();
        }
        void drop_borrowed(SP<T> &resource, const Owner &owner) noexcept {
            if (!resource || resource.strongRef() > 1) { resource.reset(); return; }
            const Scope scope{owner};
            if (!scope) {
                try { keep(resource, owner); }
                catch (...) { abandon(resource); }
            }
            resource.reset();
        }
        // Limit idle-frame work; a sweep resumes at the last position. No EGL switch or
        // driver query for objects which still have consumers.
        void collect() noexcept {
            if (m_cursor == m_entries.end()) m_cursor = m_entries.begin();
            for (unsigned n = 0; n < 8 && m_cursor != m_entries.end(); ++n) {
                auto it = m_cursor++;
                if (it->second.resource.strongRef() != 1) continue;
                const Scope scope{it->second.owner};
                if (scope) m_entries.erase(it);
            }
        }
        // A host pass can still hold a reference at unload. Without an owner-aware deleter,
        // letting it become the last owner could delete a foreign name later.
        std::size_t finish(bool backend_gone) noexcept {
            std::size_t unavailable = 0;
            for (auto &[_, item] : m_entries) {
                if (!backend_gone) {
                    const Scope scope{item.owner};
                    if (!scope || item.resource.strongRef() > 1) { abandon(item.resource); ++unavailable; }
                    item.resource.reset();
                } else {
                    item.resource.reset();
                }
            }
            m_entries.clear();
            m_cursor = m_entries.end();
            return unavailable;
        }

      private:
        // No detach API on the host SP. In the unrecoverable unload/OOM case, leave one
        // reference outstanding without allocating or calling a foreign-context destructor.
        static void abandon(SP<T> &resource) noexcept {
            union Unreleased {
                SP<T> reference;
                explicit Unreleased(SP<T> &source) : reference(std::move(source)) {}
                ~Unreleased() {} // Deliberately do not drop the sole remaining strong reference.
            } outstanding(resource);
        }
        struct Entry { Owner owner; SP<T> resource; };
        std::unordered_map<const T*, Entry> m_entries;
        typename decltype(m_entries)::iterator m_cursor = m_entries.end();
    };
} // namespace hyprimate::gl
